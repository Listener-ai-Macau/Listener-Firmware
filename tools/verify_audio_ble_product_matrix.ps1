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
    [int]$A1RoundCount = 6,
    [int]$A2LongSentenceCount = 14,
    [double]$InterSessionGapMinSeconds = 0.0,
    [double]$InterSessionGapMaxSeconds = 1.0
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
    "--a1-round-count", "$A1RoundCount",
    "--full-chain-long-sentence-count", "$A2LongSentenceCount",
    "--inter-session-gap-min-seconds", "$InterSessionGapMinSeconds",
    "--inter-session-gap-max-seconds", "$InterSessionGapMaxSeconds"
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
