[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$CaptureSeconds = 10,
    [int]$RoundCount = 10,
    [string]$DeviceName = "Listener Keyboard"
)

$ErrorActionPreference = "Stop"

$pythonScript = Join-Path $PSScriptRoot "verify_audio_ble_upload_multi_round.py"
python $pythonScript --port $Port --capture-seconds $CaptureSeconds --round-count $RoundCount --device-name $DeviceName
