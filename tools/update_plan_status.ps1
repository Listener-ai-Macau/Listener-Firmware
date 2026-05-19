# update_plan_status.ps1 — 更新任务步骤状态（支持 phases 格式）
# 用法:
#   .\tools\update_plan_status.ps1 -Plan descriptive-task-ids-workflow -StepId "1.1" -Status in_progress -Assignee Tai
#   .\tools\update_plan_status.ps1 -Plan descriptive-task-ids-workflow -StepId "1.1" -Status completed -ValidationResult "PASS: ..."
#   .\tools\update_plan_status.ps1 -Plan schematic-v1-adaptation -StepId "2.1" -Status blocked -BlockedReason "新硬件未到"
# 新任务必须使用描述性 task_slug（小写 kebab-case）。p13-p18 仅作为 legacy 状态源保留，不再递增创建 p19。
param(
    [Parameter(Mandatory=$true)]
    [string]$Plan,
    [Parameter(Mandatory=$true)]
    [string]$StepId,
    [Parameter(Mandatory=$true)]
    [ValidateSet("pending","in_progress","completed","blocked")]
    [string]$Status,
    [string]$Assignee,
    [string]$BlockedReason,
    [string]$ValidationResult,
    [int]$StaleHours = 4,
    [string]$PlansDir = "C:\Users\Billy\Desktop\listener\docs\plans",
    [int]$MutexTimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"

$mutex = [System.Threading.Mutex]::new($false, "Local\ListenerAiPlanStatusUpdate")
$hasMutex = $false

try {
    $hasMutex = $mutex.WaitOne([TimeSpan]::FromSeconds($MutexTimeoutSeconds))
    if (-not $hasMutex) {
        Write-Error "Timed out waiting for plan status update mutex after $MutexTimeoutSeconds seconds."
        exit 1
    }

    $allowedLegacyPlans = @("p13","p14","p15","p16","p17","p18")
    $isOpaqueNumberPlan = $Plan -match '^[pP]\d+(?:$|[-_])'
    $isDescriptiveSlug = $Plan -match '^[a-z][a-z0-9]*(?:-[a-z0-9]+)+$'
    $normalizedPlan = $Plan.ToLowerInvariant()
    $mappedTask = $null
    $indexFile = Join-Path $PlansDir "task_index.json"

    if ($isOpaqueNumberPlan) {
        $legacyId = ($Plan -replace '^([pP]\d+).*$', '$1').ToLowerInvariant()
        if ($allowedLegacyPlans -notcontains $legacyId) {
            Write-Error "New task IDs must be descriptive kebab-case slugs, not '$Plan'. Example: -Plan schematic-v1-adaptation"
            exit 1
        }
        Write-Warning "Using legacy task id '$Plan'. Do not create new p<number> tasks; use a descriptive task_slug for new work."
    } elseif (-not $isDescriptiveSlug) {
        Write-Error "Task id '$Plan' is not a valid descriptive task_slug. Use lowercase kebab-case, e.g. feature-docs-repair."
        exit 1
    }

    if (Test-Path $indexFile) {
        try {
            $index = Get-Content $indexFile -Raw | ConvertFrom-Json
            $mappedTask = $index.tasks | Where-Object {
                $_.task_slug -eq $Plan -or $_.legacy_id -eq $Plan
            } | Select-Object -First 1
        } catch {
            Write-Warning "Could not read task_index.json: $_"
        }
    }

    $statusFile = Join-Path $PlansDir "${Plan}_status.json"
    if (-not (Test-Path $statusFile) -and $mappedTask -and $mappedTask.status_file) {
        $statusFile = Join-Path $PlansDir $mappedTask.status_file
    }

    if (-not (Test-Path $statusFile)) {
        Write-Error "Status file not found: $statusFile"
        exit 1
    }

    $data = Get-Content $statusFile -Raw | ConvertFrom-Json

    if ($data.PSObject.Properties["plan"] -and $data.plan -ne $Plan) {
        if ($isOpaqueNumberPlan -or $mappedTask) {
            Write-Warning "Status file '$statusFile' declares plan '$($data.plan)', while command used '$Plan'. This is allowed for indexed legacy mappings only."
        } else {
            Write-Error "Plan mismatch: status file '$statusFile' declares plan '$($data.plan)', but command used '$Plan'."
            exit 1
        }
    }

    if ($Status -eq "in_progress" -and -not $Assignee) {
        Write-Error "Status 'in_progress' requires -Assignee."
        exit 1
    }

    if ($Status -eq "blocked" -and -not $BlockedReason) {
        Write-Error "Status 'blocked' requires -BlockedReason."
        exit 1
    }

    $stepMatches = @()
    $stepById = @{}
    foreach ($phase in $data.phases) {
        if ($phase.steps) {
            foreach ($step in $phase.steps) {
                if ($step.PSObject.Properties["id"] -and $step.id) {
                    $stepById[$step.id] = $step
                    if ($step.id -eq $StepId) {
                        $stepMatches += [PSCustomObject]@{
                            Step = $step
                            Phase = $phase
                        }
                    }
                }
            }
        }
    }

    if ($stepMatches.Count -eq 0) {
        Write-Error "Step '$StepId' not found in $statusFile"
        exit 1
    }

    if ($stepMatches.Count -gt 1) {
        Write-Error "Step '$StepId' appears more than once in $statusFile"
        exit 1
    }

    $stepObj = $stepMatches[0].Step
    $parentPhase = $stepMatches[0].Phase

    if ($Status -in @("in_progress", "completed") -and $stepObj.depends_on) {
        foreach ($dep in $stepObj.depends_on) {
            if (-not $stepById.ContainsKey($dep)) {
                Write-Error "Step '$StepId' depends on missing step '$dep'."
                exit 1
            }
            if ($stepById[$dep].status -ne "completed") {
                Write-Error "Step '$StepId' depends on '$dep', but '$dep' is '$($stepById[$dep].status)'. Complete dependencies before marking '$Status'."
                exit 1
            }
        }
    }

    if ($Status -eq "completed") {
        $existingValidation = $null
        if ($stepObj.PSObject.Properties["validation_result"]) {
            $existingValidation = $stepObj.validation_result
        }
        if (-not $ValidationResult -and -not $existingValidation) {
            Write-Error "Status 'completed' requires -ValidationResult unless validation_result already exists."
            exit 1
        }
    }

    # Check stale claim: if step is in_progress with claimed_at older than StaleHours, allow reclaim.
    if ($stepObj.status -eq "in_progress" -and $stepObj.assignee -and $Assignee -and $stepObj.assignee -ne $Assignee) {
        $isStale = $false
        if ($stepObj.PSObject.Properties["claimed_at"]) {
            try {
                $claimedAt = [DateTime]::Parse($stepObj.claimed_at)
                if (((Get-Date) - $claimedAt).TotalHours -ge $StaleHours) {
                    $isStale = $true
                    Write-Warning "Step '$StepId' claim by '$($stepObj.assignee)' is stale ($('{0:N1}' -f ((Get-Date) - $claimedAt).TotalHours)h old). Reclaiming for '$Assignee'."
                }
            } catch {
                # Invalid date, treat as stale.
                $isStale = $true
            }
        } else {
            $isStale = $true
        }

        if (-not $isStale) {
            Write-Error "Step '$StepId' is already in_progress by '$($stepObj.assignee)' (claimed recently). Cannot reassign to '$Assignee'. Use -StaleHours 0 to force."
            exit 1
        }
    }

    # Apply status change.
    $stepObj.status = $Status

    if ($ValidationResult) {
        $stepObj | Add-Member -NotePropertyName "validation_result" -NotePropertyValue $ValidationResult -Force
    }

    if ($Status -eq "in_progress") {
        $stepObj.assignee = $Assignee
        $stepObj | Add-Member -NotePropertyName "claimed_at" -NotePropertyValue (Get-Date).ToString("o") -Force
        $stepObj.PSObject.Properties.Remove("blocked_reason")
    }

    if ($Status -eq "completed") {
        $stepObj.assignee = $null
        $stepObj.PSObject.Properties.Remove("claimed_at")
        $stepObj.PSObject.Properties.Remove("blocked_reason")
    }

    if ($Status -eq "pending") {
        $stepObj.assignee = $null
        $stepObj.PSObject.Properties.Remove("claimed_at")
        $stepObj.PSObject.Properties.Remove("blocked_reason")
    }

    if ($Status -eq "blocked") {
        if ($Assignee) { $stepObj.assignee = $Assignee }
        $stepObj | Add-Member -NotePropertyName "blocked_reason" -NotePropertyValue $BlockedReason -Force
    }

    # Auto-derive phase status from step statuses. Keep completed steps in JSON so
    # dependencies and validation evidence remain auditable.
    if ($parentPhase -and $parentPhase.steps) {
        $allCompleted = $true
        $anyInProgress = $false
        $anyBlocked = $false

        foreach ($s in $parentPhase.steps) {
            if ($s.status -ne "completed") { $allCompleted = $false }
            if ($s.status -eq "in_progress") { $anyInProgress = $true }
            if ($s.status -eq "blocked") { $anyBlocked = $true }
        }

        if ($allCompleted) {
            $parentPhase.status = "completed"
            $parentPhase | Add-Member -NotePropertyName "summary" -NotePropertyValue "All steps completed" -Force
        } elseif ($anyBlocked) {
            $parentPhase.status = "blocked"
            $parentPhase.PSObject.Properties.Remove("summary")
        } elseif ($anyInProgress) {
            $parentPhase.status = "in_progress"
            $parentPhase.PSObject.Properties.Remove("summary")
        } else {
            $parentPhase.status = "pending"
            $parentPhase.PSObject.Properties.Remove("summary")
        }
    }

    $data.updated_at = (Get-Date).ToString("o")

    $tmpFile = "$statusFile.tmp"
    $data | ConvertTo-Json -Depth 10 | Set-Content $tmpFile -Encoding UTF8
    Move-Item -LiteralPath $tmpFile -Destination $statusFile -Force

    Write-Output "Step $StepId -> $Status$(if ($Assignee) { " (assignee: $Assignee)" })"
} finally {
    if ($hasMutex) {
        $mutex.ReleaseMutex() | Out-Null
    }
    $mutex.Dispose()
}
