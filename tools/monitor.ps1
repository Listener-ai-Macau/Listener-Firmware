param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [string]$Target = "esp32s3"
)

$ErrorActionPreference = "Stop"

$previousIdfTarget = $env:IDF_TARGET
try {
    $env:IDF_TARGET = $Target
    pwsh -NoProfile -File (Join-Path $PSScriptRoot "idf.ps1") -p $Port monitor
} finally {
    $env:IDF_TARGET = $previousIdfTarget
}
