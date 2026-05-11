$ErrorActionPreference = "Stop"

$project_root = Split-Path -Parent $PSScriptRoot

Write-Host "[1/2] build smoke test"
powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1")

Write-Host "[2/2] artifact check"
$required_files = @(
    (Join-Path $project_root "build\voice-keyboard-firmware.bin"),
    (Join-Path $project_root "build\bootloader\bootloader.bin"),
    (Join-Path $project_root "build\partition_table\partition-table.bin")
)

foreach ($file_path in $required_files) {
    if (-not (Test-Path $file_path)) {
        throw "missing artifact: $file_path"
    }
}

Write-Host "test passed"
