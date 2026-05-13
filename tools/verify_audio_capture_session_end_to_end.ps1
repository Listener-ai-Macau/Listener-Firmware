param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$Baud = 115200,
    [int]$BootTimeoutSeconds = 12,
    [int]$ExportTimeoutSeconds = 45,
    [double]$CaptureSeconds = 4.0,
    [double]$PlaybackDelaySeconds = 0.25
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "idf_env.ps1")

$python_path = (Get-Command python -ErrorAction Stop).Path
$project_root = Split-Path -Parent $PSScriptRoot
$artifact_dir = Join-Path $project_root "tests\\artifacts\\audio"
New-Item -ItemType Directory -Force -Path $artifact_dir | Out-Null

$python_script = Join-Path $PSScriptRoot "verify_audio_capture_session_end_to_end.py"

& $python_path $python_script `
    --port $Port `
    --baud $Baud `
    --boot-timeout-seconds $BootTimeoutSeconds `
    --export-timeout-seconds $ExportTimeoutSeconds `
    --capture-seconds $CaptureSeconds `
    --playback-delay-seconds $PlaybackDelaySeconds `
    --artifacts-dir $artifact_dir
