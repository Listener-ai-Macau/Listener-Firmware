# resolve_ai_identity.ps1 - resolve the current AI collaboration identity.
param(
    [string]$ExplicitIdentity,
    [string]$ParameterName = "Assignee"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "ai_workflow_common.ps1")

Resolve-AiIdentity -ExplicitIdentity $ExplicitIdentity -ParameterName $ParameterName
