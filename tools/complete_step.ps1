# complete_step.ps1 - finish a claimed step and optionally merge its branch.
param(
    [Parameter(Mandatory=$true)]
    [string]$Plan,
    [Parameter(Mandatory=$true)]
    [string]$StepId,
    [Parameter(Mandatory=$true)]
    [string]$ValidationResult,
    [string]$RepoRoot = $(Resolve-Path (Join-Path $PSScriptRoot "..")),
    [switch]$NoBranch,
    [switch]$DryRun,
    [switch]$AllowDirty
)

$ErrorActionPreference = "Stop"

function Invoke-Git {
    param([string[]]$Arguments)
    & git -C $RepoRoot @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE."
    }
}

if ($ValidationResult -notmatch '^(PASS|BLOCKED):') {
    throw "ValidationResult must start with 'PASS:' or 'BLOCKED:'."
}

$currentBranch = (& git -C $RepoRoot branch --show-current).Trim()
$featureBranch = "feature/$Plan"
$stepBranchPattern = "^ai/[^/]+-$([regex]::Escape($Plan))-$([regex]::Escape($StepId))$"

if (-not $NoBranch) {
    $dirty = (& git -C $RepoRoot status --porcelain)
    if ($dirty -and -not $AllowDirty) {
        throw "Working tree is dirty. Commit/stash unrelated work or pass -AllowDirty after confirming it is safe."
    }

    if ($currentBranch -notmatch $stepBranchPattern) {
        throw "Current branch '$currentBranch' is not the expected step branch pattern ai/<agent>-$Plan-$StepId. Use -NoBranch for status-only completion."
    }
}

$updateScript = Join-Path $PSScriptRoot "update_plan_status.ps1"
$validateScript = Join-Path $PSScriptRoot "validate_plan_status.ps1"

if ($DryRun) {
    Write-Output "Would mark $Plan/$StepId completed with validation result."
    Write-Output "Would run: powershell -ExecutionPolicy Bypass -File $updateScript -Plan $Plan -StepId $StepId -Status completed -ValidationResult `"$ValidationResult`""
    Write-Output "Would run: pwsh -NoProfile -File $validateScript -Plan $Plan"
    if (-not $NoBranch) {
        Write-Output "Would switch to $featureBranch, merge $currentBranch, and delete $currentBranch."
    }
    exit 0
}

& powershell -ExecutionPolicy Bypass -File $updateScript -Plan $Plan -StepId $StepId -Status completed -ValidationResult $ValidationResult
if ($LASTEXITCODE -ne 0) {
    throw "update_plan_status.ps1 failed with exit code $LASTEXITCODE."
}

& pwsh -NoProfile -File $validateScript -Plan $Plan
if ($LASTEXITCODE -ne 0) {
    throw "validate_plan_status.ps1 failed for '$Plan'."
}

if (-not $NoBranch) {
    $existingFeature = (& git -C $RepoRoot branch --list $featureBranch)
    if (-not $existingFeature) {
        throw "Feature branch '$featureBranch' not found."
    }

    Invoke-Git @("switch", $featureBranch)
    Invoke-Git @("merge", "--no-ff", $currentBranch, "-m", "Merge $Plan step $StepId")
    Invoke-Git @("branch", "-d", $currentBranch)
}

Write-Output "Completed $Plan/$StepId."
