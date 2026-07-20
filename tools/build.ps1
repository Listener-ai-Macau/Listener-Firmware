param(
    [string]$Target = "esp32s3",
    [string]$BuildDir = $env:LISTENER_IDF_BUILD_DIR,
    [switch]$AllowStaleSdkconfig
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

function Test-PathUnder {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Parent
    )

    $fullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\')
    $fullParent = [System.IO.Path]::GetFullPath($Parent).TrimEnd('\')
    return $fullPath.Equals($fullParent, [System.StringComparison]::OrdinalIgnoreCase) -or
        $fullPath.StartsWith($fullParent + "\", [System.StringComparison]::OrdinalIgnoreCase)
}

function Remove-BuildDirSafely {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }

    $tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) "listener-idf-build"
    $withinProject = Test-PathUnder -Path $Path -Parent $project_root
    $withinListenerTemp = [System.IO.Path]::GetFullPath($Path).StartsWith(
        [System.IO.Path]::GetFullPath($tempRoot),
        [System.StringComparison]::OrdinalIgnoreCase)
    if (-not ($withinProject -or $withinListenerTemp)) {
        throw "Refusing to remove unexpected build directory: $Path"
    }

    Remove-Item -Recurse -Force -LiteralPath $Path
}

function Reset-StaleGeneratedSdkconfig {
    param([Parameter(Mandatory = $true)][string]$BuildTarget)

    if ($BuildTarget -ne "esp32s3") {
        return
    }

    $sdkconfigPath = Join-Path $project_root "sdkconfig"
    if (-not (Test-Path -LiteralPath $sdkconfigPath)) {
        return
    }

    $sdkconfigText = Get-Content -LiteralPath $sdkconfigPath -Raw
    $staleSignals = @(
        "CONFIG_LISTENER_BOARD_PROFILE_N4=y",
        'CONFIG_ESPTOOLPY_FLASHSIZE="4MB"',
        "# CONFIG_SPIRAM is not set",
        "CONFIG_AUDIO_CAPTURE_MIC_SPH0645=y",
        "CONFIG_AUDIO_CAPTURE_SPH0645_SLOT_LEFT=y",
        "CONFIG_AUDIO_CAPTURE_SPH0645_GAIN=4",
        "# CONFIG_AUDIO_CAPTURE_MIC_SPH0655_PDM is not set",
        "# CONFIG_AUDIO_CAPTURE_V2_MIC_INTERFACE_VALIDATED is not set",
        "CONFIG_AUDIO_CAPTURE_SPH0655_SLOT_RIGHT=y",
        "# CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION is not set"
    )
    $staleMatches = @($staleSignals | Where-Object { $sdkconfigText.Contains($_) })
    if ($staleMatches.Count -eq 0) {
        return
    }

    $message = "Generated sdkconfig does not match the N16R8 defaults: $($staleMatches -join ', ')"
    if ($AllowStaleSdkconfig) {
        Write-Warning "$message; continuing because -AllowStaleSdkconfig was set."
        return
    }

    $backupDir = Join-Path ([System.IO.Path]::GetTempPath()) "listener-sdkconfig-backups"
    New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
    $backupPath = Join-Path $backupDir ("voice-keyboard-firmware-sdkconfig-stale-{0}.bak" -f (Get-Date -Format "yyyyMMdd-HHmmss"))
    Move-Item -LiteralPath $sdkconfigPath -Destination $backupPath -Force
    Remove-BuildDirSafely -Path $build_dir
    Write-Warning "$message; moved stale sdkconfig to $backupPath and cleared build dir."
}

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

function Invoke-IdfCommand {
    param([string[]]$Arguments)

    $wrappedArgs = @(
        "-NoProfile",
        "-File",
        (Join-Path $PSScriptRoot "idf.ps1")
    ) + $Arguments
    Invoke-CheckedCommand -File "pwsh" -Arguments $wrappedArgs
}

function Invoke-IdfBuild {
    param(
        [string]$BuildTarget
    )

    Reset-StaleGeneratedSdkconfig -BuildTarget $BuildTarget

    $configuredTarget = Get-ConfiguredTarget
    if ($configuredTarget -ne $BuildTarget) {
        Write-Host "Configured target is '$configuredTarget'; switching to '$BuildTarget'."
        Invoke-IdfCommand -Arguments @("-B", $build_dir, "set-target", $BuildTarget)
    } else {
        Write-Host "Configured target already '$BuildTarget'; skipping set-target."
    }

    Write-Host "Using ESP-IDF build directory: $build_dir"
    Invoke-IdfCommand -Arguments @("-B", $build_dir, "build")
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

Invoke-CheckedCommand -File "pwsh" -Arguments @(
    "-NoProfile",
    "-File",
    (Join-Path $PSScriptRoot "sync_clangd_db.ps1"),
    "-BuildDir",
    $build_dir
)
