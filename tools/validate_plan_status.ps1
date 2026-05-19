# validate_plan_status.ps1 — 校验计划状态 JSON 的结构和一致性
# 用法:
#   .\tools\validate_plan_status.ps1                      # 校验所有计划
#   .\tools\validate_plan_status.ps1 -Plan p15            # 只校验指定计划
#   .\tools\validate_plan_status.ps1 -StaleHours 4        # 自定义过期阈值
#   .\tools\validate_plan_status.ps1 -Fix                 # 自动修复可修复的问题
param(
    [string]$Plan,
    [int]$StaleHours = 4,
    [switch]$Fix,
    [string]$PlansDir = "C:\Users\Billy\Desktop\listener\docs\plans"
)

$ErrorActionPreference = "Continue"
$issues = @()
$fixed = @()
$allowedLegacyPlans = @("p13","p14","p15","p16","p17","p18")
$deprecatedStepFields = @("parallel_group", "write_paths", "push_gate_report", "evidence", "base_branch")
$indexFile = Join-Path $PlansDir "task_index.json"

function Add-Issue {
    param([string]$Level, [string]$Message, [string]$PlanName, [string]$StepId)
    $script:issues += [PSCustomObject]@{
        Level = $Level
        Plan = $PlanName
        Step = $StepId
        Message = $Message
    }
}

function Test-BlankRequiredValue {
    param($Object, [string]$PropertyName)

    if (-not $Object.PSObject.Properties[$PropertyName]) {
        return $true
    }

    $value = $Object.$PropertyName
    if ($null -eq $value) {
        return $true
    }

    if ($value -is [string]) {
        return [string]::IsNullOrWhiteSpace($value)
    }

    return $false
}

# Find status files.
$files = @()
if ($Plan) {
    $candidate = Join-Path $PlansDir "${Plan}_status.json"
    if (Test-Path $candidate) {
        $files = @($candidate)
    } else {
        $indexFile = Join-Path $PlansDir "task_index.json"
        if (Test-Path $indexFile) {
            try {
                $index = Get-Content $indexFile -Raw | ConvertFrom-Json
                $mappedTask = $index.tasks | Where-Object {
                    $_.task_slug -eq $Plan -or $_.legacy_id -eq $Plan
                } | Select-Object -First 1
                if ($mappedTask -and $mappedTask.status_file) {
                    $mappedFile = Join-Path $PlansDir $mappedTask.status_file
                    if (Test-Path $mappedFile) { $files = @($mappedFile) }
                }
            } catch {
                Add-Issue "ERROR" "Could not read task_index.json: $_" $Plan ""
            }
        }
    }
} else {
    $files = Get-ChildItem (Join-Path $PlansDir "*_status.json") -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }
}

if (-not $files) {
    Write-Error "No status files found in $PlansDir"
    exit 1
}

if (-not $Plan -and (Test-Path $indexFile)) {
    try {
        $index = Get-Content $indexFile -Raw | ConvertFrom-Json
        if (-not $index.PSObject.Properties["tasks"] -or -not $index.tasks) {
            Add-Issue "ERROR" "task_index.json missing tasks array" "task_index" ""
        } else {
            $seenSlugs = @{}
            foreach ($task in $index.tasks) {
                if (-not $task.task_slug) {
                    Add-Issue "ERROR" "task_index entry missing task_slug" "task_index" ""
                    continue
                }
                if ($seenSlugs.ContainsKey($task.task_slug)) {
                    Add-Issue "ERROR" "Duplicate task_slug in task_index: $($task.task_slug)" "task_index" ""
                } else {
                    $seenSlugs[$task.task_slug] = $true
                }

                if ($task.legacy_id) {
                    if ($allowedLegacyPlans -notcontains ([string]$task.legacy_id).ToLowerInvariant()) {
                        Add-Issue "ERROR" "task_index legacy_id '$($task.legacy_id)' is not in allowed legacy set" "task_index" $task.task_slug
                    }
                } elseif ($task.task_slug -notmatch '^[a-z][a-z0-9]*(?:-[a-z0-9]+)+$') {
                    Add-Issue "ERROR" "task_index task_slug '$($task.task_slug)' must be descriptive lowercase kebab-case" "task_index" $task.task_slug
                }

                $taskStatus = if ($task.PSObject.Properties["status"]) { [string]$task.status } else { "" }
                $isArchivedTask = $taskStatus -in @("completed", "superseded", "archived")

                if (-not $task.status_file) {
                    Add-Issue "ERROR" "task_index '$($task.task_slug)' missing status_file" "task_index" $task.task_slug
                } else {
                    $taskStatusPath = Join-Path $PlansDir $task.status_file
                    if (-not (Test-Path $taskStatusPath)) {
                        $level = if ($isArchivedTask) { "WARN" } else { "ERROR" }
                        Add-Issue $level "task_index '$($task.task_slug)' status_file not found: $($task.status_file)" "task_index" $task.task_slug
                    }
                }
            }
        }
    } catch {
        Add-Issue "ERROR" "Could not parse task_index.json: $_" "task_index" ""
    }
} elseif (-not $Plan) {
    Add-Issue "WARN" "task_index.json not found" "task_index" ""
}

foreach ($file in $files) {
    $planName = [System.IO.Path]::GetFileNameWithoutExtension($file) -replace '_status$', ''
    Write-Output "`n=== $planName ==="

    try {
        $data = Get-Content $file -Raw | ConvertFrom-Json
    } catch {
        Add-Issue "ERROR" "JSON parse failed: $_" $planName ""
        continue
    }

    $declaredPlan = if ($data.PSObject.Properties["plan"]) { [string]$data.plan } else { $planName }
    $isLegacyFile = $planName -in $allowedLegacyPlans -or $declaredPlan -match '^p(13|14|15|16|17|18)(?:$|[-_])'
    $isDescriptiveSlug = $declaredPlan -match '^[a-z][a-z0-9]*(?:-[a-z0-9]+)+$'

    if (-not $isLegacyFile -and -not $isDescriptiveSlug) {
        Add-Issue "ERROR" "Plan name '$declaredPlan' must be descriptive lowercase kebab-case" $planName ""
    }

    # Schema check: top-level fields.
    foreach ($req in @("plan", "updated_at", "phases")) {
        if (-not $data.PSObject.Properties[$req]) {
            Add-Issue "ERROR" "Missing required field: $req" $planName ""
        }
    }

    if (-not $data.phases) { continue }

    $stepById = @{}
    $duplicateStepIds = @{}
    foreach ($phase in $data.phases) {
        if ($phase.steps) {
            foreach ($step in $phase.steps) {
                if ($step.PSObject.Properties["id"] -and $step.id) {
                    if ($stepById.ContainsKey($step.id)) {
                        $duplicateStepIds[$step.id] = $true
                    } else {
                        $stepById[$step.id] = $step
                    }
                }
            }
        }
    }

    foreach ($dup in $duplicateStepIds.Keys) {
        Add-Issue "ERROR" "Duplicate step id: $dup" $planName $dup
    }

    foreach ($phase in $data.phases) {
        # Phase-level checks
        if (-not $phase.id) { Add-Issue "ERROR" "Phase missing id" $planName "" }
        if (-not $phase.title) { Add-Issue "WARN" "Phase missing title" $planName "" }

        $phaseStatus = $phase.status
        # phase status is optional (auto-derived by update_plan_status.ps1)
        # if present, validate consistency with steps below

        # If phase has steps, check them
        if ($phase.steps) {
            $allCompleted = $true
            $anyInProgress = $false

            foreach ($step in $phase.steps) {
                $sid = $step.id

                # Required step fields
                foreach ($req in @("id", "title", "repo", "status", "assignee", "depends_on", "acceptance", "validation_commands", "validation_result")) {
                    $isNullableRequired = $req -in @("assignee", "validation_result")
                    $isArrayRequired = $req -in @("depends_on", "acceptance", "validation_commands")

                    if (-not $step.PSObject.Properties[$req]) {
                        if ($isNullableRequired) {
                            Add-Issue "ERROR" "Step $sid missing required nullable field: $req" $planName $sid
                        } else {
                            Add-Issue "ERROR" "Step $sid missing required field: $req" $planName $sid
                        }
                    } elseif (-not $isNullableRequired -and -not $isArrayRequired -and (Test-BlankRequiredValue $step $req)) {
                        Add-Issue "ERROR" "Step $sid has blank required field: $req" $planName $sid
                    } elseif ($req -eq "depends_on" -and $null -eq $step.depends_on) {
                        Add-Issue "ERROR" "Step $sid depends_on must be an array; use [] when there are no dependencies" $planName $sid
                    } elseif ($req -in @("acceptance", "validation_commands") -and (-not $step.$req -or $step.$req.Count -eq 0)) {
                        Add-Issue "ERROR" "Step $sid must include at least one $req item" $planName $sid
                    }
                }

                foreach ($arrayField in @("depends_on", "acceptance", "validation_commands")) {
                    if ($step.PSObject.Properties[$arrayField] -and $null -ne $step.$arrayField) {
                        $value = $step.$arrayField
                        if ($value -is [string]) {
                            Add-Issue "ERROR" "Step $sid field $arrayField must be an array, not a string" $planName $sid
                        }
                    }
                }

                foreach ($field in $deprecatedStepFields) {
                    if ($step.PSObject.Properties[$field]) {
                        $level = if ($isLegacyFile) { "WARN" } else { "ERROR" }
                        Add-Issue $level "Step $sid uses deprecated field: $field" $planName $sid
                        if ($Fix -and -not $isLegacyFile) {
                            $step.PSObject.Properties.Remove($field)
                            $fixed += "Removed deprecated field $field from $planName/$sid"
                        }
                    }
                }

                if ($step.status -notin @("pending", "in_progress", "completed", "blocked")) {
                    Add-Issue "ERROR" "Step $sid has invalid status: $($step.status)" $planName $sid
                }

                if ($step.depends_on) {
                    foreach ($dep in $step.depends_on) {
                        if (-not $stepById.ContainsKey($dep)) {
                            Add-Issue "ERROR" "Step $sid depends on missing step: $dep" $planName $sid
                        }
                    }
                }

                # Status consistency
                if ($step.status -eq "in_progress") {
                    $anyInProgress = $true
                    if (-not $step.assignee) {
                        Add-Issue "WARN" "Step $sid is in_progress but has no assignee" $planName $sid
                    }

                    # Stale claim check
                    if ($step.PSObject.Properties["claimed_at"]) {
                        try {
                            $claimedAt = [DateTime]::Parse($step.claimed_at)
                            $hoursAgo = ((Get-Date) - $claimedAt).TotalHours
                            if ($hoursAgo -ge $StaleHours) {
                                Add-Issue "WARN" ("Step $sid claimed by '$($step.assignee)' $('{0:N1}' -f $hoursAgo)h ago (threshold: ${StaleHours}h)") $planName $sid
                            }
                        } catch {
                            Add-Issue "WARN" "Step $sid has invalid claimed_at: $($step.claimed_at)" $planName $sid
                        }
                    } else {
                        Add-Issue "WARN" "Step $sid is in_progress but has no claimed_at" $planName $sid
                        if ($Fix -and $step.assignee) {
                            $step | Add-Member -NotePropertyName "claimed_at" -NotePropertyValue (Get-Date).ToString("o") -Force
                            $fixed += "Added claimed_at to $planName/$sid"
                        }
                    }
                }

                if ($step.status -ne "completed") { $allCompleted = $false }

                # Blocked without reason
                if ($step.status -eq "blocked" -and -not $step.blocked_reason) {
                    Add-Issue "WARN" "Step $sid is blocked but has no blocked_reason" $planName $sid
                }

                # Completed but still has assignee
                if ($step.status -eq "completed" -and $step.assignee) {
                    Add-Issue "WARN" "Step $sid is completed but still has assignee: $($step.assignee)" $planName $sid
                    if ($Fix) {
                        $step.assignee = $null
                        $fixed += "Cleared assignee on completed step $planName/$sid"
                    }
                }

                if ($step.status -eq "completed" -and -not $step.validation_result) {
                    Add-Issue "ERROR" "Step $sid is completed but has no validation_result" $planName $sid
                }

                if ($step.status -eq "completed" -and $step.depends_on) {
                    foreach ($dep in $step.depends_on) {
                        if ($stepById.ContainsKey($dep) -and $stepById[$dep].status -ne "completed") {
                            Add-Issue "ERROR" "Step $sid is completed but dependency $dep is $($stepById[$dep].status)" $planName $sid
                        }
                    }
                }
            }

            # Phase vs step status consistency (only if phase has explicit status)
            if (-not $phaseStatus) { continue }

            if ($allCompleted -and $phaseStatus -ne "completed") {
                Add-Issue "WARN" "Phase $($phase.id) all steps completed but phase status is '$phaseStatus'" $planName ""
                if ($Fix) {
                    $phase.status = "completed"
                    $fixed += "Set phase $($phase.id) status to completed"
                }
            }

            if ($anyInProgress -and $phaseStatus -eq "completed") {
                Add-Issue "ERROR" "Phase $($phase.id) has in_progress steps but status is 'completed'" $planName ""
            }
        }
    }

    # Save fixes
    if ($Fix -and $fixed.Count -gt 0) {
        $data.updated_at = (Get-Date).ToString("o")
        $data | ConvertTo-Json -Depth 10 | Set-Content $file -Encoding UTF8
    }
}

# Summary
Write-Output "`n========== RESULTS =========="
if ($issues.Count -eq 0) {
    Write-Output "All plans valid. No issues found."
} else {
    $errors = $issues | Where-Object { $_.Level -eq "ERROR" }
    $warns = $issues | Where-Object { $_.Level -eq "WARN" }

    if ($errors) {
        Write-Output "`nERRORS ($($errors.Count)):"
        $errors | ForEach-Object { Write-Output "  [$($_.Plan)/$($_.Step)] $($_.Message)" }
    }
    if ($warns) {
        Write-Output "`nWARNINGS ($($warns.Count)):"
        $warns | ForEach-Object { Write-Output "  [$($_.Plan)/$($_.Step)] $($_.Message)" }
    }

    if ($fixed.Count -gt 0) {
        Write-Output "`nFIXED ($($fixed.Count)):"
        $fixed | ForEach-Object { Write-Output "  $_" }
    }
}

# Exit code
if ($issues | Where-Object { $_.Level -eq "ERROR" }) { exit 1 }
exit 0
