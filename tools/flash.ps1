param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [string]$Target = "esp32s3",
    [string]$BuildDir = $env:LISTENER_IDF_BUILD_DIR
)

$ErrorActionPreference = "Stop"

$project_root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "idf_env.ps1") -Target $Target

function Get-ShortBuildDir {
    param([Parameter(Mandatory = $true)][string]$ProjectRoot)

    if (-not [string]::IsNullOrWhiteSpace($BuildDir)) {
        return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDir)
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
Write-Host "Using ESP-IDF build directory: $build_dir"
idf.py -B $build_dir -p $Port flash
