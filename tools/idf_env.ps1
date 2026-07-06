param(
    [string]$Target = "esp32s3"
)

$ErrorActionPreference = "Stop"

$env:PYTHONUTF8 = "1"
$env:PYTHONIOENCODING = "utf-8"

try {
    $utf8_no_bom = [System.Text.UTF8Encoding]::new($false)
    [Console]::InputEncoding = $utf8_no_bom
    [Console]::OutputEncoding = $utf8_no_bom
    $OutputEncoding = $utf8_no_bom
} catch {
}

$project_root = Split-Path -Parent $PSScriptRoot
function Resolve-IdfPath {
    $candidates = [System.Collections.Generic.List[string]]::new()
    foreach ($candidate in @(
        $env:ESP_IDF_PATH,
        $env:IDF_PATH,
        (Join-Path $HOME "esp\esp-idf")
    )) {
        if (-not [string]::IsNullOrWhiteSpace($candidate)) {
            $candidates.Add($candidate) | Out-Null
        }
    }

    $espressifFrameworks = Join-Path $env:USERPROFILE "esp\esp-idf"
    if (-not [string]::IsNullOrWhiteSpace($espressifFrameworks)) {
        $candidates.Add($espressifFrameworks) | Out-Null
    }

    $programDataFrameworks = "C:\Espressif\frameworks"
    if (Test-Path -LiteralPath $programDataFrameworks) {
        Get-ChildItem -LiteralPath $programDataFrameworks -Directory -Filter "esp-idf*" -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending |
            ForEach-Object { $candidates.Add($_.FullName) | Out-Null }
    }

    foreach ($candidate in @($candidates | Select-Object -Unique)) {
        $exportScript = Join-Path $candidate "export.ps1"
        if (Test-Path -LiteralPath $exportScript) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    return $null
}

$idf_path = Resolve-IdfPath
if ([string]::IsNullOrWhiteSpace($idf_path)) {
    throw "ESP-IDF export.ps1 was not found. Run tools/setup_windows.ps1 or set ESP_IDF_PATH/IDF_PATH to the ESP-IDF checkout."
}

$env:IDF_PATH = $idf_path
$env:ESP_IDF_PATH = $idf_path
$env:IDF_TARGET = $Target

$exportScript = Join-Path $idf_path "export.ps1"
. $exportScript

try {
    & python -c "import esp_idf_monitor" | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "python import exited with $LASTEXITCODE"
    }
} catch {
    throw "ESP-IDF Python environment is incomplete after export.ps1; missing module esp_idf_monitor. Run tools/setup_windows.ps1, then retry through tools/build.ps1/tools/flash.ps1/tools/idf.ps1 instead of raw idf.py. ESP-IDF path: $idf_path"
}
Set-Location $project_root
