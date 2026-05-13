param(
    [string]$Target = "esp32s3"
)

$ErrorActionPreference = "Stop"

$project_root = Split-Path -Parent $PSScriptRoot
$idf_path = $env:ESP_IDF_PATH

if ([string]::IsNullOrWhiteSpace($idf_path)) {
    $idf_path = Join-Path $HOME "esp\esp-idf"
}

if (-not (Test-Path $idf_path)) {
    throw "ESP-IDF not found at $idf_path. Run tools/setup_windows.ps1 or set ESP_IDF_PATH."
}

$env:IDF_PATH = $idf_path
$env:IDF_TARGET = $Target

. (Join-Path $idf_path "export.ps1")
Set-Location $project_root
