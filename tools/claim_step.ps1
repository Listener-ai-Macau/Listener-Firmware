# claim_step.ps1 - branch-aware wrapper for claiming an AI collaboration step.
param(
    [Parameter(Mandatory=$true)]
    [string]$Plan,
    [Parameter(Mandatory=$true)]
    [string]$StepId,
    [string]$Assignee = $(if ($env:AI_AGENT_ID) { $env:AI_AGENT_ID } elseif ($env:TAI_AGENT_ID) { $env:TAI_AGENT_ID } else { $env:USERNAME }),
    [string]$PlansDir = "C:\Users\Billy\Desktop\listener\docs\plans",
    [string]$RepoRoot = $(Resolve-Path (Join-Path $PSScriptRoot "..")),
    [switch]$NoBranch,
    [switch]$DryRun,
    [switch]$AllowDirty,
    [switch]$SkipValidation,
    [int]$StaleHours = 4
)

$ErrorActionPreference = "Stop"

function Get-StatusFile {
    param([string]$RequestedPlan, [string]$Directory)

    $candidate = Join-Path $Directory "${RequestedPlan}_status.json"
    if (Test-Path $candidate) {
        return $candidate
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
                return $mappedFile
            }
        }
    }

    throw "Status file not found for plan '$RequestedPlan'."
}

function Invoke-Git {
    param([string[]]$Arguments)
    & git -C $RepoRoot @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE."
    }
}

if (-not $SkipValidation) {
    $validateScript = Join-Path $PSScriptRoot "validate_plan_status.ps1"
    & pwsh -NoProfile -File $validateScript -Plan $Plan | Write-Output
    if ($LASTEXITCODE -ne 0) {
        throw "validate_plan_status.ps1 failed for '$Plan'. Fix plan status before claiming."
    }
}

$statusFile = Get-StatusFile -RequestedPlan $Plan -Directory $PlansDir
$data = Get-Content $statusFile -Raw | ConvertFrom-Json
$stepById = @{}
$target = $null

foreach ($phase in $data.phases) {
    foreach ($step in @($phase.steps)) {
        if ($step.PSObject.Properties["id"] -and $step.id) {
            $stepById[[string]$step.id] = $step
            if ([string]$step.id -eq $StepId) {
                $target = $step
            }
        }
    }
}

if (-not $target) {
    throw "Step '$StepId' not found in '$statusFile'."
}

if ($target.status -ne "pending") {
    throw "Step '$StepId' is '$($target.status)', not pending."
}

if ($target.assignee) {
    throw "Step '$StepId' already has assignee '$($target.assignee)'."
}

foreach ($dep in @($target.depends_on)) {
    if (-not $stepById.ContainsKey([string]$dep)) {
        throw "Step '$StepId' depends on missing step '$dep'."
    }
    if ($stepById[[string]$dep].status -ne "completed") {
        throw "Step '$StepId' depends on '$dep', but '$dep' is '$($stepById[[string]$dep].status)'."
    }
}

$normalizedAssignee = $Assignee.ToLowerInvariant()
$stepBranch = "ai/$normalizedAssignee-$Plan-$StepId"
$featureBranch = "feature/$Plan"

if (-not $NoBranch) {
    $dirty = (& git -C $RepoRoot status --porcelain)
    if ($dirty -and -not $AllowDirty) {
        throw "Working tree is dirty. Commit/stash unrelated work or pass -AllowDirty after confirming it is safe."
    }

    $existingFeature = (& git -C $RepoRoot branch --list $featureBranch).Trim()
    $existingRemoteFeature = (& git -C $RepoRoot branch -r --list "origin/$featureBranch").Trim()
    if (-not $existingFeature) {
        if ($existingRemoteFeature) {
            if ($DryRun) {
                Write-Output "Would create local $featureBranch tracking origin/$featureBranch."
            } else {
                Invoke-Git @("switch", "-c", $featureBranch, "--track", "origin/$featureBranch")
            }
        } elseif ($DryRun) {
            Write-Output "Would create $featureBranch from current HEAD."
        } else {
            Invoke-Git @("switch", "-c", $featureBranch)
        }
    } else {
        if ($DryRun) {
            Write-Output "Would switch to $featureBranch."
        } else {
            Invoke-Git @("switch", $featureBranch)
        }
    }

    $existingStep = (& git -C $RepoRoot branch --list $stepBranch).Trim()
    if ($existingStep) {
        if ($DryRun) {
            Write-Output "Would switch to existing $stepBranch."
        } else {
            Invoke-Git @("switch", $stepBranch)
        }
    } else {
        if ($DryRun) {
            Write-Output "Would create $stepBranch from $featureBranch."
        } else {
            Invoke-Git @("switch", "-c", $stepBranch)
        }
    }
}

$updateScript = Join-Path $PSScriptRoot "update_plan_status.ps1"
$updateArgs = @(
    "-ExecutionPolicy", "Bypass",
    "-File", $updateScript,
    "-Plan", $Plan,
    "-StepId", $StepId,
    "-Status", "in_progress",
    "-Assignee", $Assignee,
    "-StaleHours", $StaleHours
)

if ($DryRun) {
    Write-Output "Would claim $Plan/$StepId for $Assignee."
    Write-Output "Would run: powershell $($updateArgs -join ' ')"
    exit 0
}

& powershell @updateArgs
if ($LASTEXITCODE -ne 0) {
    throw "update_plan_status.ps1 failed with exit code $LASTEXITCODE."
}

Write-Output "Claimed $Plan/$StepId on $(if ($NoBranch) { 'current branch' } else { $stepBranch })."
