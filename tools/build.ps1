param(
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

function Get-ConfiguredTarget {
    $sdkconfigPath = Join-Path $project_root "sdkconfig"
    if (-not (Test-Path $sdkconfigPath)) {
        return $null
    }

    $line = Select-String -Path $sdkconfigPath -Pattern '^CONFIG_IDF_TARGET="(.+)"$' | Select-Object -First 1
    if ($null -eq $line) {
        return $null
    }

    return $line.Matches[0].Groups[1].Value
}

function Invoke-CheckedCommand {
    param(
        [string]$File,
        [string[]]$Arguments
    )

    & $File @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$File $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

function Invoke-IdfBuild {
    param(
        [string]$BuildTarget
    )

    $configuredTarget = Get-ConfiguredTarget
    if ($configuredTarget -ne $BuildTarget) {
        Write-Host "Configured target is '$configuredTarget'; switching to '$BuildTarget'."
        Invoke-CheckedCommand -File "idf.py" -Arguments @("-B", $build_dir, "set-target", $BuildTarget)
    } else {
        Write-Host "Configured target already '$BuildTarget'; skipping set-target."
    }

    Write-Host "Using ESP-IDF build directory: $build_dir"
    Invoke-CheckedCommand -File "idf.py" -Arguments @("-B", $build_dir, "build")
}

try {
    Invoke-IdfBuild -BuildTarget $Target
} catch {
    Write-Warning "Initial build failed; retrying once after removing build directory."
    if (Test-Path $build_dir) {
        Remove-Item -Recurse -Force $build_dir
    }
    Invoke-IdfBuild -BuildTarget $Target
}

Invoke-CheckedCommand -File "powershell" -Arguments @(
    "-ExecutionPolicy",
    "Bypass",
    "-File",
    (Join-Path $PSScriptRoot "sync_clangd_db.ps1"),
    "-BuildDir",
    $build_dir
)
