param(
    [switch]$RequireTag
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$toolPath = Join-Path $projectRoot "third_party\denzic-platform\tools\release_gate\version_check.py"
$configPath = Join-Path $PSScriptRoot "release_version_gate.json"

$toolArgs = @($toolPath, "--config", $configPath)
if ($RequireTag) {
    $toolArgs += "--require-tag"
}

& python @toolArgs
exit $LASTEXITCODE
