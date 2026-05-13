param(
    [string]$Target = "esp32s3"
)

$ErrorActionPreference = "Stop"

$project_root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "idf_env.ps1") -Target $Target

if (Test-Path (Join-Path $project_root "build")) {
    Remove-Item -Recurse -Force (Join-Path $project_root "build")
}
idf.py set-target $Target
idf.py build
powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "sync_clangd_db.ps1")
