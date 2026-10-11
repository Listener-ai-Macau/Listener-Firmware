param(
    [switch]$RequireTag
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$toolPath = Join-Path $projectRoot "third_party\denzic-platform\tools\release_gate\version_check.py"
$configPath = Join-Path $PSScriptRoot "release_version_gate.json"

$version = (Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim()
# The shared gate accepts stable versions only. Customer RCs retain their full
# identity in VERSION, the ESP image descriptor, OTA manifest and Git tag.
if ($version -match '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)-rc\.?[1-9][0-9]*$') {
    $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
    foreach ($source in $config.must_contain) {
        $content = Get-Content -LiteralPath (Join-Path $projectRoot $source.path) -Raw
        if (-not $content.Contains([string]$source.reference)) {
            throw "$($source.path) must consume the canonical VERSION file."
        }
    }
    if ($RequireTag) {
        $tag = "v$version"
        $found = (& git -C $projectRoot tag --list $tag) -join ''
        if ($LASTEXITCODE -ne 0 -or $found.Trim() -ne $tag) { throw "Missing release tag $tag" }
    }
    Write-Output "PASS: Listener Firmware release version is $version"
    exit 0
}

$toolArgs = @($toolPath, "--config", $configPath)
if ($RequireTag) {
    $toolArgs += "--require-tag"
}

& python @toolArgs
exit $LASTEXITCODE
