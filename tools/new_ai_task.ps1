# new_ai_task.ps1 - create a minimal AI collaboration plan/status pair and index entry.
param(
    [Parameter(Mandatory=$true)]
    [string]$Plan,
    [Parameter(Mandatory=$true)]
    [string]$Title,
    [Parameter(Mandatory=$true)]
    [string]$StepTitle,
    [ValidateSet("firmware", "Listener-Type", "firmware+Listener-Type")]
    [string]$Repo = "firmware",
    [string[]]$Acceptance = @("Validation command passes"),
    [string[]]$ValidationCommands = @("pwsh -NoProfile -File tools\validate_plan_status.ps1 -Plan <task_slug>"),
    [string]$PlansDir = "C:\Users\Billy\Desktop\listener\docs\plans",
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

if ($Plan -notmatch '^[a-z][a-z0-9]*(?:-[a-z0-9]+)+$') {
    throw "Plan must be descriptive lowercase kebab-case, e.g. feature-docs-repair."
}

$planFile = Join-Path $PlansDir "${Plan}_plan.md"
$statusFile = Join-Path $PlansDir "${Plan}_status.json"
$indexFile = Join-Path $PlansDir "task_index.json"

if ((Test-Path $planFile) -or (Test-Path $statusFile)) {
    throw "Plan or status file already exists for '$Plan'."
}

if (Test-Path $indexFile) {
    $index = Get-Content $indexFile -Raw | ConvertFrom-Json
    if ($index.tasks | Where-Object { $_.task_slug -eq $Plan }) {
        throw "task_index already contains '$Plan'."
    }
} else {
    $index = [PSCustomObject]@{
        schema_version = 1
        updated_at = (Get-Date).ToString("yyyy-MM-dd")
        naming_rule = "New tasks use descriptive lowercase kebab-case task_slug values. Do not create new p<number> task IDs."
        tasks = @()
    }
}

$commands = @($ValidationCommands | ForEach-Object { $_ -replace '<task_slug>', $Plan })
$planMarkdown = @"
# $Title

## 目标

$Title

## Precedent Review

- Reuse existing repository patterns and tools before adding new mechanisms.

## 步骤

| id | title | repo | depends_on |
|---|---|---|---|
| 1.1 | $StepTitle | $Repo | [] |

## 每步验收

### 1.1 $StepTitle

$(@($Acceptance | ForEach-Object { "- $_" }) -join "`n")
"@

$status = [ordered]@{
    plan = $Plan
    updated_at = (Get-Date).ToString("o")
    phases = @(
        [ordered]@{
            id = 1
            title = "执行"
            status = "pending"
            steps = @(
                [ordered]@{
                    id = "1.1"
                    title = $StepTitle
                    repo = $Repo
                    status = "pending"
                    assignee = $null
                    depends_on = @()
                    acceptance = @($Acceptance)
                    validation_commands = @($commands)
                    validation_result = $null
                }
            )
        }
    )
}

if ($DryRun) {
    Write-Output "Would create: $planFile"
    Write-Output "Would create: $statusFile"
    Write-Output "Would update: $indexFile"
    Write-Output ($status | ConvertTo-Json -Depth 10)
    exit 0
}

if (-not (Test-Path $PlansDir)) {
    New-Item -ItemType Directory -Path $PlansDir -Force | Out-Null
}

$planMarkdown | Set-Content $planFile -Encoding UTF8
$status | ConvertTo-Json -Depth 10 | Set-Content $statusFile -Encoding UTF8

$tasks = @($index.tasks)
$tasks += [PSCustomObject]@{
    task_slug = $Plan
    legacy_id = $null
    status = "pending"
    status_file = "${Plan}_status.json"
    plan_files = @("${Plan}_plan.md")
    notes = $Title
}
$index | Add-Member -NotePropertyName "tasks" -NotePropertyValue $tasks -Force
$index.updated_at = (Get-Date).ToString("yyyy-MM-dd")
$index | ConvertTo-Json -Depth 10 | Set-Content $indexFile -Encoding UTF8

Write-Output "Created AI task '$Plan'."
