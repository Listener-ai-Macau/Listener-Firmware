[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [string]$DeviceName = "listener",
    [string]$Cases = "auto",
    [int]$CaptureSeconds = 5,
    [int]$LongCaptureSeconds = 30,
    [int]$RoundCount = 3,
    [string]$BluetoothAddress = "",
    [switch]$NoResetBeforeCapture,
    [switch]$RestartPanAdapter,
    [int]$A1RoundCount = 6
)

$ErrorActionPreference = "Stop"

$pythonScript = Join-Path $PSScriptRoot "verify_audio_ble_product_matrix.py"
$arguments = @(
    $pythonScript,
    "--port", $Port,
    "--device-name", $DeviceName,
    "--cases", $Cases,
    "--capture-seconds", "$CaptureSeconds",
    "--long-capture-seconds", "$LongCaptureSeconds",
    "--round-count", "$RoundCount",
    "--a1-round-count", "$A1RoundCount"
)

if ($NoResetBeforeCapture) {
    $arguments += "--no-reset-before-capture"
}

if (-not [string]::IsNullOrWhiteSpace($BluetoothAddress)) {
    $arguments += @("--bluetooth-address", $BluetoothAddress)
}

if ($RestartPanAdapter) {
    $arguments += "--restart-pan-adapter"
}

python @arguments
