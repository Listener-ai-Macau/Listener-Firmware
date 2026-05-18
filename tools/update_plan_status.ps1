# update_plan_status.ps1 — 更新任务步骤状态（支持 phases 格式）
# 用法:
#   .\tools\update_plan_status.ps1 -Plan p13 -StepId "2.1" -Status in_progress -Assignee codex
#   .\tools\update_plan_status.ps1 -Plan p13 -StepId "2.1" -Status completed
#   .\tools\update_plan_status.ps1 -Plan p13 -StepId "2.4" -Status blocked -BlockedReason "需人工推送"
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
    [int]$StaleHours = 4,
    [string]$PlansDir = "C:\Users\Billy\Desktop\listener\docs\plans"
)

$ErrorActionPreference = "Stop"

$statusFile = Join-Path $PlansDir "${Plan}_status.json"

if (-not (Test-Path $statusFile)) {
    Write-Error "Status file not found: $statusFile"
    exit 1
}

$data = Get-Content $statusFile -Raw | ConvertFrom-Json

# Find step and its parent phase
$stepObj = $null
$parentPhase = $null
foreach ($phase in $data.phases) {
    if ($phase.steps) {
        $found = $phase.steps | Where-Object { $_.id -eq $StepId }
        if ($found) { $stepObj = $found; $parentPhase = $phase; break }
    }
}

if (-not $stepObj) {
    Write-Error "Step '$StepId' not found in $statusFile"
    exit 1
}

# Check stale claim: if step is in_progress with claimed_at older than StaleHours, allow reclaim
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
            # Invalid date, treat as stale
            $isStale = $true
        }
    }

    if (-not $isStale) {
        Write-Error "Step '$StepId' is already in_progress by '$($stepObj.assignee)' (claimed recently). Cannot reassign to '$Assignee'. Use -StaleHours 0 to force."
        exit 1
    }
}

# Apply status change
$stepObj.status = $Status

if ($Status -eq "in_progress") {
    if ($Assignee) { $stepObj.assignee = $Assignee }
    $stepObj | Add-Member -NotePropertyName "claimed_at" -NotePropertyValue (Get-Date).ToString("o") -Force
}

if ($Status -eq "completed") {
    $stepObj.assignee = $null
    $stepObj.PSObject.Properties.Remove("claimed_at")
}

if ($Status -eq "pending") {
    $stepObj.assignee = $null
    $stepObj.PSObject.Properties.Remove("claimed_at")
}

if ($Status -eq "blocked") {
    if ($Assignee) { $stepObj.assignee = $Assignee }
}

if ($BlockedReason) {
    $stepObj | Add-Member -NotePropertyName "blocked_reason" -NotePropertyValue $BlockedReason -Force
} else {
    $stepObj.PSObject.Properties.Remove("blocked_reason")
}

# Auto-derive phase status from step statuses
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
        # Collapse completed phase: keep summary, remove expanded steps
        if (-not $parentPhase.PSObject.Properties["summary"]) {
            $parentPhase | Add-Member -NotePropertyName "summary" -NotePropertyValue "All steps completed" -Force
        }
        $parentPhase.PSObject.Properties.Remove("steps")
    } elseif ($anyInProgress -or $anyBlocked) {
        $parentPhase.status = "in_progress"
    } else {
        $parentPhase.status = "pending"
    }
}

$data.updated_at = (Get-Date).ToString("o")

$data | ConvertTo-Json -Depth 10 | Set-Content $statusFile -Encoding UTF8

Write-Output "Step $StepId -> $Status$(if ($Assignee) { " (assignee: $Assignee)" })"

# Branch cleanup: if step completed, try to delete the step branch
if ($Status -eq "completed") {
    $agentName = if ($Assignee) { $Assignee.ToLower() } else { "" }
    $planShort = $Plan -replace '^p', ''
    $possibleBranches = @(
        "ai/$agentName-$($Plan)-$StepId"
        "ai/$agentName-p$planShort-$StepId"
    )
    # Also try with dots replaced
    $stepDash = $StepId -replace '\.', '-'
    $possibleBranches += @(
        "ai/$agentName-$($Plan)-$stepDash"
        "ai/$agentName-p$planShort-$stepDash"
    )

    foreach ($br in $possibleBranches) {
        try {
            $branchExists = git branch --list $br 2>$null
            if ($branchExists) {
                # Check if merged into feature branch
                $featureBranch = git branch --list "feature/*$($Plan)*" 2>$null | Select-Object -First 1
                if ($featureBranch) {
                    $featureName = $featureBranch.Trim().TrimStart('*').Trim()
                    git checkout $featureName 2>$null
                    git branch -d $br 2>$null
                    if ($LASTEXITCODE -eq 0) {
                        Write-Output "Cleaned up step branch: $br"
                    }
                } else {
                    # No feature branch, try deleting against current HEAD
                    git branch -d $br 2>$null
                    if ($LASTEXITCODE -eq 0) {
                        Write-Output "Cleaned up step branch: $br"
                    }
                }
            }
        } catch {
            # Non-fatal, branch cleanup is best-effort
        }
    }
}
