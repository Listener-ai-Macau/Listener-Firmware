[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDir,
    [Parameter(Mandatory = $true)]
    [string]$InstalledTypePreflightSummaryPath
)

$ErrorActionPreference = "Stop"

function Resolve-NormalizedPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    return (Resolve-Path -LiteralPath $Path).Path.TrimEnd('\\').ToLowerInvariant()
}

function Require-Equal {
    param(
        [Parameter(Mandatory = $true)]$Actual,
        [Parameter(Mandatory = $true)]$Expected,
        [Parameter(Mandatory = $true)][string]$Description
    )

    if ([string]$Actual -cne [string]$Expected) {
        throw "$Description. Expected '$Expected', got '$Actual'."
    }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$packageRoot = Resolve-NormalizedPath -Path $PackageDir
$manifestPath = Join-Path $packageRoot "ota_manifest.json"
$firmwarePath = Join-Path $packageRoot "firmware_ota.bin"

foreach ($requiredPath in @($manifestPath, $firmwarePath, $InstalledTypePreflightSummaryPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required current OTA pre-upgrade evidence is missing: $requiredPath"
    }
}

$sourceHead = (& git -C $repoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($sourceHead)) {
    throw "Unable to resolve the Firmware source commit."
}
$sourceDirty = @(& git -C $repoRoot status --porcelain)
if ($sourceDirty.Count -ne 0) {
    throw "Current OTA pre-upgrade evidence requires a clean Firmware source tree."
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$summary = Get-Content -LiteralPath $InstalledTypePreflightSummaryPath -Raw | ConvertFrom-Json
$firmwareSha256 = (Get-FileHash -LiteralPath $firmwarePath -Algorithm SHA256).Hash.ToLowerInvariant()
$summaryManifestPath = Resolve-NormalizedPath -Path $summary.staged_package.manifest_path
$summaryFirmwarePath = Resolve-NormalizedPath -Path $summary.staged_package.firmware_path

Require-Equal -Actual $manifest.firmware.git_commit -Expected $sourceHead -Description "OTA manifest commit is not the current Firmware HEAD"
if ([bool]$manifest.firmware.git_dirty) {
    throw "OTA manifest marks the Firmware source tree dirty."
}
Require-Equal -Actual $manifest.firmware.sha256 -Expected $firmwareSha256 -Description "OTA manifest SHA256 does not match firmware_ota.bin"
Require-Equal -Actual $summary.result -Expected "PASS" -Description "Installed Type OTA preflight did not pass"
Require-Equal -Actual $summary.installed_type.path -Expected "C:\Program Files\Listener Type\listener-type.exe" -Description "OTA preflight did not use the installed Program Files Type"
Require-Equal -Actual $summaryManifestPath -Expected (Resolve-NormalizedPath -Path $manifestPath) -Description "Preflight summary references a different OTA manifest"
Require-Equal -Actual $summaryFirmwarePath -Expected (Resolve-NormalizedPath -Path $firmwarePath) -Description "Preflight summary references a different OTA binary"
Require-Equal -Actual $summary.staged_package.source_commit -Expected $sourceHead -Description "Preflight summary source commit is stale"
Require-Equal -Actual $summary.staged_package.firmware_sha256 -Expected $firmwareSha256 -Description "Preflight summary SHA256 is stale"
Require-Equal -Actual $summary.raw_report.status -Expected "PASS" -Description "Installed Type OTA report did not pass"
Require-Equal -Actual $summary.raw_report.mode -Expected "preflight" -Description "Installed Type OTA report was not a preflight"
if (-not [bool]$summary.raw_report.packageValid -or -not [bool]$summary.preflight.connected) {
    throw "Installed Type OTA preflight did not validate a connected package."
}
Require-Equal -Actual $summary.raw_report.preflight.hardwareRevision -Expected $manifest.requirements.hardware_revision -Description "Connected Listener hardware revision does not match the OTA manifest"
if (@($summary.preflight.capabilities) -notcontains "denzic_ota_v1") {
    throw "Installed Type OTA preflight did not confirm the denzic_ota_v1 capability."
}
if (@($summary.preflight.blockers).Count -ne 0 -or @($summary.raw_report.errors).Count -ne 0) {
    throw "Installed Type OTA preflight reported blockers or errors."
}

Write-Host "PASS: current OTA pre-upgrade evidence matches Firmware HEAD, package SHA, installed Type, and connected Listener capability."
