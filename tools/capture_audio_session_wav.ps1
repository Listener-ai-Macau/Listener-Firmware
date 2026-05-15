param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [ValidateRange(1, 10)]
    [int]$CaptureSeconds = 3,
    [switch]$PhysicalKey,
    [switch]$NoResetBeforeCapture
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "idf_env.ps1")

$python_path = (Get-Command python -ErrorAction Stop).Path
$project_root = Split-Path -Parent $PSScriptRoot
$artifact_dir = Join-Path $project_root "tests\\artifacts\\audio"
New-Item -ItemType Directory -Force -Path $artifact_dir | Out-Null

$capture_timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$wav_path = Join-Path $artifact_dir ("capture_session_{0}_16k_mono.wav" -f $capture_timestamp)
$python_script = Join-Path $PSScriptRoot "capture_audio_wav.py"

$command = @(
    $python_script,
    "--port", $Port,
    "--mode", "toggle-session",
    "--capture-seconds", $CaptureSeconds,
    "--wav-path", $wav_path
)

if ($PhysicalKey) {
    $command += @("--trigger-mode", "physical-key")
}

if ($NoResetBeforeCapture) {
    $command += "--no-reset-before-capture"
}

& $python_path @command
