# Compatibility proxy for the shared AI collaboration workflow scripts.
param(
    [Parameter(Mandatory=$true)]
    [string]$ScriptName,
    [Parameter(ValueFromRemainingArguments=$true)]
    [object[]]$ForwardArgs
)

$ErrorActionPreference = "Stop"

$workflowRepo = [Environment]::GetEnvironmentVariable("AI_WORKFLOW_REPO")
if ([string]::IsNullOrWhiteSpace($workflowRepo)) {
    $installedWorkflowRepo = 'C:\Users\Billy\Desktop\listener\ai-collaboration-workflow'
    if (Test-Path $installedWorkflowRepo) {
        $workflowRepo = (Resolve-Path $installedWorkflowRepo).Path
    } else {
        $candidate = Join-Path $PSScriptRoot "..\..\ai-collaboration-workflow"
        if (Test-Path $candidate) {
            $workflowRepo = (Resolve-Path $candidate).Path
        } else {
            throw "AI_WORKFLOW_REPO is not set and shared workflow repo was not found at '$installedWorkflowRepo' or '$candidate'."
        }
    }
}

$target = Join-Path $workflowRepo "scripts\$ScriptName"
if (-not (Test-Path $target)) {
    throw "Shared AI workflow script not found: $target"
}

& pwsh -NoProfile -File $target @ForwardArgs
exit $LASTEXITCODE
