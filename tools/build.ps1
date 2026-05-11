$ErrorActionPreference = "Stop"

$project_root = Split-Path -Parent $PSScriptRoot
$idf_path = Join-Path (Split-Path -Parent $project_root) "esp-idf"
$python_path = "C:\Users\Billy\AppData\Local\Programs\Python\Python311"

$env:PATH = "$python_path;$env:PATH"
$env:IDF_TARGET = "esp32c3"
. (Join-Path $idf_path "export.ps1")

Set-Location $project_root
if (Test-Path (Join-Path $project_root "build")) {
    Remove-Item -Recurse -Force (Join-Path $project_root "build")
}
idf.py set-target esp32c3
idf.py build
powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "sync_clangd_db.ps1")
