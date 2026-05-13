param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [string]$Target = "esp32s3"
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "idf_env.ps1") -Target $Target
idf.py -p $Port flash
