param(
    [string]$Target = "esp32s3"
)

$ErrorActionPreference = "Stop"

$buildScript = Join-Path $PSScriptRoot "build.ps1"
& $buildScript -Target $Target
