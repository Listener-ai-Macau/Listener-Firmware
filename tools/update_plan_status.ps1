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
    [string]$PlansDir = "C:\Users\Billy\Desktop\listener\docs\plans"
)

$ErrorActionPreference = "Stop"

$statusFile = Join-Path $PlansDir "${Plan}_status.json"

if (-not (Test-Path $statusFile)) {
    Write-Error "Status file not found: $statusFile"
    exit 1
}

$data = Get-Content $statusFile -Raw | ConvertFrom-Json

# Find step by id (format: "phase.step" e.g. "2.1")
$stepObj = $null
foreach ($phase in $data.phases) {
    if ($phase.steps) {
        $found = $phase.steps | Where-Object { $_.id -eq $StepId }
        if ($found) { $stepObj = $found; break }
    }
}

if (-not $stepObj) {
    Write-Error "Step '$StepId' not found in $statusFile"
    exit 1
}

# Check conflict: step already claimed by someone else
if ($stepObj.status -eq "in_progress" -and $stepObj.assignee -and $Assignee -and $stepObj.assignee -ne $Assignee) {
    Write-Error "Step '$StepId' is already in_progress by '$($stepObj.assignee)'. Cannot reassign to '$Assignee'."
    exit 1
}

$stepObj.status = $Status
if ($Assignee) { $stepObj.assignee = $Assignee }
if ($Status -eq "completed" -or $Status -eq "pending") { $stepObj.assignee = $null }
if ($BlockedReason) {
    $stepObj | Add-Member -NotePropertyName "blocked_reason" -NotePropertyValue $BlockedReason -Force
} else {
    $stepObj.PSObject.Properties.Remove("blocked_reason")
}

$data.updated_at = (Get-Date).ToString("o")

$data | ConvertTo-Json -Depth 10 | Set-Content $statusFile -Encoding UTF8

Write-Output "Step $StepId -> $Status$(if ($Assignee) { " (assignee: $Assignee)" })"
