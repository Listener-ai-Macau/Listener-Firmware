param(
    [switch]$RequireTag
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$versionPath = Join-Path $projectRoot "VERSION"

if (-not (Test-Path -LiteralPath $versionPath)) {
    Write-Error "VERSION file is missing."
    exit 1
}

$version = (Get-Content -LiteralPath $versionPath -Raw).Trim()
if ([string]::IsNullOrWhiteSpace($version)) {
    Write-Error "VERSION file is empty."
    exit 1
}

if ($version -notmatch '^\d+\.\d+\.\d+$') {
    Write-Error "Release VERSION must be plain semver like 1.0.0; got '$version'."
    exit 1
}

$requiredFiles = @(
    "CMakeLists.txt",
    "tools\package_ota_firmware.ps1",
    "tools\package_factory_firmware.ps1"
)

foreach ($relativePath in $requiredFiles) {
    $path = Join-Path $projectRoot $relativePath
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Error "Required version source file is missing: $relativePath"
        exit 1
    }
    $content = Get-Content -LiteralPath $path -Raw
    if ($content -notmatch 'VERSION') {
        Write-Error "$relativePath does not reference the VERSION file."
        exit 1
    }
    if ($content -notmatch [regex]::Escape($version)) {
        Write-Error "$relativePath does not contain the current fallback version '$version'."
        exit 1
    }
}

if ($RequireTag) {
    $tag = "v$version"
    $match = (& git -C $projectRoot tag --list $tag 2>$null)
    if ($LASTEXITCODE -ne 0 -or @($match).Count -eq 0 -or @($match)[0] -ne $tag) {
        Write-Error "Required git tag '$tag' was not found."
        exit 1
    }
}

Write-Host "PASS: Listener Firmware release version is $version"
