[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [string]$DeviceName = "listener",
    [string]$Cases = "auto",
    [int]$CaptureSeconds = 5,
    [int]$LongCaptureSeconds = 30,
    [int]$RoundCount = 3,
    [int]$IdleSeconds = 30,
    [int]$SoakRoundCount = 5,
    [string]$BluetoothAddress = "",
    [switch]$NoResetBeforeCapture,
    [switch]$RestartPanAdapter
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
    "--idle-seconds", "$IdleSeconds",
    "--soak-round-count", "$SoakRoundCount"
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
