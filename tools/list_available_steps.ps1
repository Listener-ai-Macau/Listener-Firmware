# list_available_steps.ps1 - list pending plan steps that can be claimed now.
param(
    [string]$Plan,
    [string]$Repo,
    [switch]$IncludeBlocked,
    [switch]$IncludeStale,
    [int]$StaleHours = 4,
    [string]$PlansDir = "C:\Users\Billy\Desktop\listener\docs\plans"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "ai_workflow_common.ps1")

function Get-StatusFiles {
    param([string]$RequestedPlan, [string]$Directory)

    if ($RequestedPlan) {
        $candidate = Join-Path $Directory "${RequestedPlan}_status.json"
        if (Test-Path $candidate) {
            return @($candidate)
        }

        $indexFile = Join-Path $Directory "task_index.json"
        if (Test-Path $indexFile) {
            $index = Get-Content $indexFile -Raw | ConvertFrom-Json
            $mappedTask = $index.tasks | Where-Object {
                $_.task_slug -eq $RequestedPlan -or $_.legacy_id -eq $RequestedPlan
            } | Select-Object -First 1
            if ($mappedTask -and $mappedTask.status_file) {
                $mappedFile = Join-Path $Directory $mappedTask.status_file
                if (Test-Path $mappedFile) {
                    return @($mappedFile)
                }
            }
        }

        throw "Status file not found for plan '$RequestedPlan'."
    }

    return @(Get-ChildItem (Join-Path $Directory "*_status.json") -File | ForEach-Object { $_.FullName })
}

$rows = @()
foreach ($file in (Get-StatusFiles -RequestedPlan $Plan -Directory $PlansDir)) {
    $data = Get-Content $file -Raw | ConvertFrom-Json
    $stepById = @{}
    foreach ($phase in $data.phases) {
        foreach ($step in @($phase.steps)) {
            if ($step.PSObject.Properties["id"] -and $step.id) {
                $stepById[[string]$step.id] = $step
            }
        }
    }

    foreach ($phase in $data.phases) {
        foreach ($step in @($phase.steps)) {
            if ($Repo) {
                $stepRepos = @([string]$step.repo -split "\+")
                if ($stepRepos -notcontains $Repo) {
                    continue
                }
            }

            $ready = $true
            foreach ($dep in @($step.depends_on)) {
                if (-not $stepById.ContainsKey([string]$dep) -or $stepById[[string]$dep].status -ne "completed") {
                    $ready = $false
                    break
                }
            }

            $claimState = Get-StepClaimState -Step $step -StaleHours $StaleHours
            $claimable = $claimState.Claimable -and $ready -and ($step.status -eq "pending" -or $IncludeStale)
            if (-not $claimable -and -not ($IncludeBlocked -and $step.status -eq "blocked")) {
                continue
            }

            $rows += [PSCustomObject]@{
                plan = $data.plan
                phase = $phase.id
                step = $step.id
                status = $step.status
                assignee = $step.assignee
                repo = $step.repo
                title = $step.title
                depends_on = (@($step.depends_on) -join ",")
                stale = $claimState.Stale
                stale_basis = $claimState.Basis
                stale_hours = if ($null -ne $claimState.HoursOld) { "{0:N1}" -f $claimState.HoursOld } else { "" }
            }
        }
    }
}

if (-not $rows) {
    Write-Output "No claimable steps found."
    exit 0
}

$rows | Sort-Object plan, phase, step | Format-Table -AutoSize
