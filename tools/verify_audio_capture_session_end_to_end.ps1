param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$CaptureSeconds = 4,
    [switch]$PhysicalKey,
    [switch]$NoResetBeforeCapture
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "idf_env.ps1")

$python_path = (Get-Command python -ErrorAction Stop).Path
$project_root = Split-Path -Parent $PSScriptRoot
$artifact_dir = Join-Path $project_root "tests\\artifacts\\audio"
New-Item -ItemType Directory -Force -Path $artifact_dir | Out-Null

$python_script = Join-Path $PSScriptRoot "verify_audio_capture_session_end_to_end.py"

$command = @(
    $python_script,
    "--port", $Port,
    "--capture-seconds", $CaptureSeconds,
    "--artifacts-dir", $artifact_dir
)

if ($PhysicalKey) {
    $command += @("--trigger-mode", "physical-key")
}

if ($NoResetBeforeCapture) {
    $command += "--no-reset-before-capture"
}

& $python_path @command
