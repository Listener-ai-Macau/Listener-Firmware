param(
    [string]$Port = "",
    [string]$Target = "esp32s3"
)

$ErrorActionPreference = "Stop"

$previousIdfTarget = $env:IDF_TARGET
try {
    $env:IDF_TARGET = $Target
    if ([string]::IsNullOrWhiteSpace($Port)) {
        pwsh -NoProfile -File (Join-Path $PSScriptRoot "idf.ps1") openocd
    } else {
        pwsh -NoProfile -File (Join-Path $PSScriptRoot "idf.ps1") -p $Port gdb
    }
} finally {
    $env:IDF_TARGET = $previousIdfTarget
}
