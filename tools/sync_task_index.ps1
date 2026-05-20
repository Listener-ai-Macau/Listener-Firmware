# Compatibility wrapper. The implementation lives in ..\ai-collaboration-workflow\scripts.
$proxy = Join-Path $PSScriptRoot "ai_workflow_proxy.ps1"
& $proxy -ScriptName $MyInvocation.MyCommand.Name @args
exit $LASTEXITCODE
