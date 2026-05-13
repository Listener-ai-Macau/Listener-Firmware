[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$CaptureSeconds = 4,
    [string]$DeviceName = "Listener Keyboard"
)

$ErrorActionPreference = "Stop"

$pythonScript = Join-Path $PSScriptRoot "capture_audio_ble_wav.py"
python $pythonScript --port $Port --capture-seconds $CaptureSeconds --device-name $DeviceName
