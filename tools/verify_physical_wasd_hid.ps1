param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$TimeoutSeconds = 45,
    [int]$Baud = 115200,
    [string]$OutputPath = "",
    [switch]$ResetBeforeRead
)

$ErrorActionPreference = "Stop"
Write-Warning "verify_physical_wasd_hid.ps1 is a legacy wrapper. Use verify_physical_custom_key_hid.ps1 for KEY1-KEY4 F13/F14/F15/F16 fallback validation."

$script = Join-Path $PSScriptRoot "verify_physical_custom_key_hid.ps1"
$forwardArgs = @(
    "-Port", $Port,
    "-TimeoutSeconds", $TimeoutSeconds,
    "-Baud", $Baud
)
if ($OutputPath) {
    $forwardArgs += @("-OutputPath", $OutputPath)
}
if ($ResetBeforeRead.IsPresent) {
    $forwardArgs += "-ResetBeforeRead"
}

& $script @forwardArgs
exit $LASTEXITCODE
