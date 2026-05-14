[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$CaptureSeconds = 12,
    [string]$DeviceName = "Listener Keyboard"
)

$ErrorActionPreference = "Stop"

$pythonScript = Join-Path $PSScriptRoot "verify_audio_ble_upload_end_to_end.py"
python $pythonScript --port $Port --capture-seconds $CaptureSeconds --device-name $DeviceName
