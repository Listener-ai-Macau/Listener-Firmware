param(
    [string]$Target = "esp32s3",
    [string]$BuildDir = $env:LISTENER_IDF_USB_LS_BUILD_DIR,
    [string]$Sdkconfig = $env:LISTENER_IDF_USB_LS_SDKCONFIG,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$project_root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "idf_env.ps1") -Target $Target

function Get-ProjectHash {
    param([Parameter(Mandatory = $true)][string]$ProjectRoot)

    $sha1 = [System.Security.Cryptography.SHA1]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($ProjectRoot.ToLowerInvariant())
        $hashBytes = $sha1.ComputeHash($bytes)
        return -join ($hashBytes[0..5] | ForEach-Object { $_.ToString("x2") })
    } finally {
        $sha1.Dispose()
    }
}

function Resolve-DebugBuildDir {
    param([Parameter(Mandatory = $true)][string]$ProjectRoot)

    if (-not [string]::IsNullOrWhiteSpace($BuildDir)) {
        return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDir)
    }

    $hash = Get-ProjectHash -ProjectRoot $ProjectRoot
    return Join-Path ([System.IO.Path]::GetTempPath()) "listener-idf-build-usb-light-sleep-debug-$hash"
}

function Resolve-DebugSdkconfig {
    param([Parameter(Mandatory = $true)][string]$DebugBuildDir)

    if (-not [string]::IsNullOrWhiteSpace($Sdkconfig)) {
        return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Sdkconfig)
    }

    return Join-Path $DebugBuildDir "sdkconfig"
}

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

function Remove-DebugBuildDirSafely {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }

    $tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) "listener-idf-build-usb-light-sleep-debug-"
    $projectDebugRoot = Join-Path $project_root "build-usb-light-sleep-debug"
    $withinProjectDebug = Test-PathUnder -Path $Path -Parent $projectDebugRoot
    $withinListenerTemp = [System.IO.Path]::GetFullPath($Path).StartsWith(
        [System.IO.Path]::GetFullPath($tempRoot),
        [System.StringComparison]::OrdinalIgnoreCase)
    if (-not ($withinProjectDebug -or $withinListenerTemp)) {
        throw "Refusing to remove unexpected debug build directory: $Path"
    }

    Remove-Item -Recurse -Force -LiteralPath $Path
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

$buildDirResolved = Resolve-DebugBuildDir -ProjectRoot $project_root
$sdkconfigResolved = Resolve-DebugSdkconfig -DebugBuildDir $buildDirResolved
$debugDefaults = Join-Path $project_root "sdkconfig.defaults.usb-light-sleep-debug"
$baseDefaults = Join-Path $project_root "sdkconfig.defaults"

foreach ($path in @($baseDefaults, $debugDefaults)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required sdkconfig defaults file is missing: $path"
    }
}

if ($Clean.IsPresent) {
    Remove-DebugBuildDirSafely -Path $buildDirResolved
}

New-Item -ItemType Directory -Force -Path $buildDirResolved | Out-Null
$defaultsArg = "$baseDefaults;$debugDefaults"

Write-Host "Using USB light-sleep debug build directory: $buildDirResolved"
Write-Host "Using USB light-sleep debug sdkconfig: $sdkconfigResolved"
Write-Host "Using SDKCONFIG_DEFAULTS: $defaultsArg"

Invoke-CheckedCommand -File "pwsh" -Arguments @(
    "-NoProfile",
    "-File",
    (Join-Path $PSScriptRoot "idf.ps1"),
    "-B",
    $buildDirResolved,
    "-D",
    "IDF_TARGET=$Target",
    "-D",
    "SDKCONFIG=$sdkconfigResolved",
    "-D",
    "SDKCONFIG_DEFAULTS=$defaultsArg",
    "reconfigure"
)

Invoke-CheckedCommand -File "pwsh" -Arguments @(
    "-NoProfile",
    "-File",
    (Join-Path $PSScriptRoot "idf.ps1"),
    "-B", $buildDirResolved,
    "build"
)
