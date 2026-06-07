param(
    [Parameter(Mandatory = $true)]
    [string]$Port
)

$ErrorActionPreference = "Continue"
Set-StrictMode -Version Latest

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
. (Join-Path $repoRoot "tools\idf_env.ps1")

$python = (Get-Command python -ErrorAction Stop).Path
$esptool = Join-Path $env:IDF_PATH "components\esptool_py\esptool\esptool.py"
$beforeModes = @("default_reset", "usb_reset", "no_reset")
$baudRates = @(115200, 460800)
$anyPass = $false

Write-Host "bootloader_probe_agent=oai3"
Write-Host "bootloader_probe_port=$Port"
Write-Host "bootloader_probe_started_at=$(Get-Date -Format o)"
Write-Host "bootloader_probe_esptool=$esptool"

foreach ($before in $beforeModes) {
    foreach ($baud in $baudRates) {
        Write-Host "=== esptool chip_id before=$before baud=$baud begin $(Get-Date -Format o) ==="
        $global:LASTEXITCODE = 0
        & $python $esptool `
            --chip esp32s3 `
            -p $Port `
            -b $baud `
            --connect-attempts 2 `
            --before $before `
            --after no_reset `
            chip_id
        $exitCode = if ($null -eq $global:LASTEXITCODE) { 0 } else { [int]$global:LASTEXITCODE }
        Write-Host "=== esptool chip_id before=$before baud=$baud exit_code=$exitCode $(Get-Date -Format o) ==="
        if ($exitCode -eq 0) {
            $anyPass = $true
        }
    }
}

if ($anyPass) {
    Write-Host "bootloader_probe_result=PASS"
    exit 0
}

Write-Host "bootloader_probe_result=FAIL"
Write-Host "bootloader_probe_failure_class=hardware"
Write-Host "bootloader_probe_failure=no esptool reset mode returned ESP32-S3 serial data"
exit 1
