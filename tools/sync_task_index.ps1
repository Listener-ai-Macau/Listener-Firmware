# sync_task_index.ps1 - synchronize task_index.json status values from status files.
param(
    [string]$PlansDir = "C:\Users\Billy\Desktop\listener\docs\plans",
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "ai_workflow_common.ps1")

$indexFile = Join-Path $PlansDir "task_index.json"
if (-not (Test-Path $indexFile)) {
    throw "task_index.json not found: $indexFile"
}

$index = Get-Content $indexFile -Raw | ConvertFrom-Json
$changes = @()

foreach ($task in @($index.tasks)) {
    if (-not $task.status_file) {
        continue
    }

    $statusFile = Join-Path $PlansDir $task.status_file
    if (-not (Test-Path $statusFile)) {
        continue
    }

    $statusData = Get-Content $statusFile -Raw | ConvertFrom-Json
    $derivedStatus = Get-DerivedPlanStatus -PlanData $statusData
    if ($task.status -ne $derivedStatus) {
        $changes += [PSCustomObject]@{
            task_slug = $task.task_slug
            old_status = $task.status
            new_status = $derivedStatus
        }
        if (-not $DryRun) {
            $task.status = $derivedStatus
        }
    }
}

if (-not $changes) {
    Write-Output "task_index.json already in sync."
    exit 0
}

$changes | Format-Table -AutoSize

if ($DryRun) {
    Write-Output "Dry run only; task_index.json not modified."
    exit 0
}

$index.updated_at = (Get-Date).ToString("yyyy-MM-dd")
$index | ConvertTo-Json -Depth 10 | Set-Content $indexFile -Encoding UTF8
Write-Output "Synchronized task_index.json."
