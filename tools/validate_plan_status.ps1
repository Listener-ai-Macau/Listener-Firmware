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

function Add-Issue {
    param([string]$Level, [string]$Message, [string]$PlanName, [string]$StepId)
    $script:issues += [PSCustomObject]@{
        Level = $Level
        Plan = $PlanName
        Step = $StepId
        Message = $Message
    }
}

# Find status files
$files = if ($Plan) {
    @(Join-Path $PlansDir "${Plan}_status.json")
} else {
    Get-ChildItem (Join-Path $PlansDir "*_status.json") -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }
}

if (-not $files) {
    Write-Error "No status files found in $PlansDir"
    exit 1
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

    # Schema check: top-level fields
    foreach ($req in @("plan", "updated_at", "phases")) {
        if (-not $data.PSObject.Properties[$req]) {
            Add-Issue "ERROR" "Missing required field: $req" $planName ""
        }
    }

    if (-not $data.phases) { continue }

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
                foreach ($req in @("id", "title", "repo", "status")) {
                    if (-not $step.PSObject.Properties[$req] -or -not $step.$req) {
                        Add-Issue "ERROR" "Step $sid missing required field: $req" $planName $sid
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
