param(
    [string]$Target = "esp32s3"
)

$ErrorActionPreference = "Stop"

$project_root = Split-Path -Parent $PSScriptRoot

function Get-ShortBuildDir {
    param([Parameter(Mandatory = $true)][string]$ProjectRoot)

    if (-not [string]::IsNullOrWhiteSpace($env:LISTENER_IDF_BUILD_DIR)) {
        return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($env:LISTENER_IDF_BUILD_DIR)
    }

    if ($ProjectRoot.Length -lt 80) {
        return Join-Path $ProjectRoot "build"
    }

    $sha1 = [System.Security.Cryptography.SHA1]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($ProjectRoot.ToLowerInvariant())
        $hashBytes = $sha1.ComputeHash($bytes)
        $hash = -join ($hashBytes[0..5] | ForEach-Object { $_.ToString("x2") })
    } finally {
        $sha1.Dispose()
    }
    return Join-Path ([System.IO.Path]::GetTempPath()) "listener-idf-build-$hash"
}

$build_dir = Get-ShortBuildDir -ProjectRoot $project_root

Write-Host "[1/2] build smoke test"
pwsh -NoProfile -File (Join-Path $PSScriptRoot "build.ps1") -Target $Target -BuildDir $build_dir

Write-Host "[2/2] artifact check"
$required_files = @(
    (Join-Path $build_dir "voice-keyboard-firmware.bin"),
    (Join-Path $build_dir "bootloader\bootloader.bin"),
    (Join-Path $build_dir "partition_table\partition-table.bin")
)

foreach ($file_path in $required_files) {
    if (-not (Test-Path $file_path)) {
        throw "missing artifact: $file_path"
    }
}

Write-Host "test passed"
