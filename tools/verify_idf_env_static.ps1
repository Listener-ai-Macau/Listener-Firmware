param()

$ErrorActionPreference = "Stop"

$toolsRoot = $PSScriptRoot

function Read-ToolFile {
    param([Parameter(Mandatory = $true)][string]$Name)
    return Get-Content -LiteralPath (Join-Path $toolsRoot $Name) -Raw
}

function Assert-Contains {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Description
    )
    if ($Text -notmatch $Pattern) {
        throw "Missing IDF environment contract: $Description"
    }
}

$idfEnv = Read-ToolFile "idf_env.ps1"
$build = Read-ToolFile "build.ps1"
$flash = Read-ToolFile "flash.ps1"
$idf = Read-ToolFile "idf.ps1"

Assert-Contains $idfEnv '\$env:ESP_IDF_PATH' 'ESP_IDF_PATH is accepted'
Assert-Contains $idfEnv '\$env:IDF_PATH' 'IDF_PATH is accepted'
Assert-Contains $idfEnv 'export\.ps1' 'ESP-IDF export script is dot-sourced'
Assert-Contains $idfEnv 'import esp_idf_monitor' 'ESP-IDF Python monitor module is verified after export'
Assert-Contains $idfEnv 'tools/setup_windows\.ps1' 'setup_windows guidance is printed for incomplete ESP-IDF installs'
Assert-Contains $idfEnv '\$env:ESP_IDF_PATH\s*=\s*\$idf_path' 'resolved ESP-IDF path is exported for child tools'
Assert-Contains $build 'idf_env\.ps1' 'build script enters the shared ESP-IDF environment'
Assert-Contains $flash 'idf_env\.ps1' 'flash script enters the shared ESP-IDF environment'
Assert-Contains $idf 'idf_env\.ps1' 'raw idf wrapper enters the shared ESP-IDF environment'

Write-Host "PASS: ESP-IDF environment wrapper accepts ESP_IDF_PATH/IDF_PATH, verifies esp_idf_monitor, and is used by build/flash/idf wrappers."
