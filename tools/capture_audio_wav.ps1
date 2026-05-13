param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [ValidateRange(1, 5)]
    [int]$DurationSeconds = 3,
    [int]$Baud = 115200,
    [int]$BootTimeoutSeconds = 12,
    [int]$ExportTimeoutSeconds = 45
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "idf_env.ps1")

$python_path = (Get-Command python -ErrorAction Stop).Path
$project_root = Split-Path -Parent $PSScriptRoot
$artifact_dir = Join-Path $project_root "tests\\artifacts\\audio"
New-Item -ItemType Directory -Force -Path $artifact_dir | Out-Null

$capture_timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$wav_path = Join-Path $artifact_dir ("capture_{0}_16k_mono.wav" -f $capture_timestamp)
$python_script = Join-Path $PSScriptRoot "capture_audio_wav.py"

& $python_path $python_script `
    --port $Port `
    --duration-seconds $DurationSeconds `
    --baud $Baud `
    --boot-timeout-seconds $BootTimeoutSeconds `
    --export-timeout-seconds $ExportTimeoutSeconds `
    --wav-path $wav_path
