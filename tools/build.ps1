param(
    [string]$Target = "esp32s3"
)

$ErrorActionPreference = "Stop"

$project_root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "idf_env.ps1") -Target $Target

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

function Invoke-IdfBuild {
    param(
        [string]$BuildTarget
    )

    $configuredTarget = Get-ConfiguredTarget
    if ($configuredTarget -ne $BuildTarget) {
        Write-Host "Configured target is '$configuredTarget'; switching to '$BuildTarget'."
        idf.py set-target $BuildTarget
    } else {
        Write-Host "Configured target already '$BuildTarget'; skipping set-target."
    }

    idf.py build
}

try {
    Invoke-IdfBuild -BuildTarget $Target
} catch {
    $build_dir = Join-Path $project_root "build"
    Write-Warning "Initial build failed; retrying once after removing build directory."
    if (Test-Path $build_dir) {
        Remove-Item -Recurse -Force $build_dir
    }
    Invoke-IdfBuild -BuildTarget $Target
}

powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "sync_clangd_db.ps1")
