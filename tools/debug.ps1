param(
    [string]$Port = "",
    [string]$Target = "esp32s3"
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "idf_env.ps1") -Target $Target

if ([string]::IsNullOrWhiteSpace($Port)) {
    idf.py openocd
} else {
    idf.py -p $Port gdb
}
