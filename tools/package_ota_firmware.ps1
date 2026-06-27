param(
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build"),
    [string]$OutputRoot = (Join-Path $PSScriptRoot "..\.cache\ota_firmware"),
    [ValidateSet("stable", "development")]
    [string]$Channel = "stable",
    [string]$MinDesktopVersion = "1.0.1",
    [ValidateRange(1, 500)]
    [int]$GattChunkBytes = 500
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$project_root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$build_path = (Resolve-Path $BuildDir).Path
$output_root_path = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputRoot)

# --- Resolve project metadata ---
$description_path = Join-Path $build_path "project_description.json"
$project_name = "voice-keyboard-firmware"
$project_version = $null
$target = "esp32s3"
$esp_app_version_max_chars = 31

if (Test-Path $description_path) {
    $description = Get-Content -Raw $description_path | ConvertFrom-Json
    if ($description.project_name) { $project_name = [string]$description.project_name }
    if ($description.project_version) { $project_version = [string]$description.project_version }
    if ($description.target) { $target = [string]$description.target }
}

function Get-SourceVersion {
    $version_path = Join-Path $project_root "VERSION"
    if (Test-Path -LiteralPath $version_path) {
        $version = (Get-Content -LiteralPath $version_path -Raw).Trim()
        if (-not [string]::IsNullOrWhiteSpace($version)) {
            return $version
        }
    }
    $version = (& git -C $project_root describe --tags --always --dirty 2>$null)
    if ($version) { return $version }
    return "1.0.1"
}

if (-not $project_version) {
    $project_version = Get-SourceVersion
}

function Test-ReleaseVersionString {
    param(
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$ReleaseChannel
    )

    if ($ReleaseChannel -eq "development") {
        return
    }

    if ($Version -match '(?i)(^|[._+-])(dirty|dev)([._+-]|$)') {
        Write-Error "Release channel $ReleaseChannel requires a clean release version; got '$Version'. Use -Channel development for local builds."
        exit 1
    }
}

Test-ReleaseVersionString -Version $project_version -ReleaseChannel $Channel

$ota_version = $project_version.Trim()
if ($ota_version.Length -gt $esp_app_version_max_chars) {
    $truncated_version = $ota_version.Substring(0, $esp_app_version_max_chars) -replace '[._+-]+$', ''
    Write-Warning "Project version '$project_version' exceeds ESP app descriptor / BLE OTA control limit ($esp_app_version_max_chars chars); using '$truncated_version' in ota_manifest.json."
    $ota_version = $truncated_version
}

# --- Dirty tree check ---
$git_commit = (& git -C $project_root rev-parse HEAD 2>$null)
if (-not $git_commit) { $git_commit = "unknown" }
$git_status = @(& git -C $project_root status --porcelain 2>$null)
$git_dirty = $git_status.Count -gt 0

if ($git_dirty -and $Channel -ne "development") {
    Write-Error "Working tree is dirty ($($git_status.Count) changed files). Commit or stash changes, or use -Channel development.`n$($git_status -join "`n")"
    exit 1
}

# --- Locate app binary ---
$app_bin_source = Join-Path $build_path "$project_name.bin"
if (-not (Test-Path $app_bin_source)) {
    throw "Missing firmware binary: $app_bin_source. Run build first."
}

# --- Create output directory ---
$safe_version = $ota_version -replace '[^A-Za-z0-9_.-]', '_'
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$package_name = "listener-ota-$safe_version-$timestamp"
$package_dir = Join-Path $output_root_path $package_name
New-Item -ItemType Directory -Force $package_dir | Out-Null

# --- Copy and hash app binary ---
$ota_bin_name = "firmware_ota.bin"
$ota_bin_dest = Join-Path $package_dir $ota_bin_name
Copy-Item -LiteralPath $app_bin_source -Destination $ota_bin_dest -Force
$ota_hash = (Get-FileHash -LiteralPath $ota_bin_dest -Algorithm SHA256).Hash.ToLowerInvariant()
$ota_size = (Get-Item -LiteralPath $ota_bin_dest).Length

# --- Generate OTA manifest ---
$manifest = [ordered]@{
    schema_version = 2
    created_at_utc = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
    channel = $Channel
    firmware = [ordered]@{
        project = $project_name
        version = $ota_version
        git_commit = $git_commit
        git_dirty = $git_dirty
        target = $target
        file = $ota_bin_name
        size_bytes = $ota_size
        sha256 = $ota_hash
    }
    requirements = [ordered]@{
        hardware_revision = "keyboard-v2-n16r8"
        protocol_version = 1
        min_desktop_version = $MinDesktopVersion
        gatt_chunk_bytes = $GattChunkBytes
    }
    ble_identity = [ordered]@{
        name = "listener"
        appearance = "0x03C1"
        hid_service_uuid = "1812"
        audio_service_uuid = "710af845-6d9f-6583-0c4d-9e5b3bc3091a"
        audio_notify_uuid = "710af845-6d9f-6583-0c4d-9e5b3bc3091b"
        audio_control_uuid = "710af845-6d9f-6583-0c4d-9e5b3bc3091e"
        readiness_uuid = "710af845-6d9f-6583-0c4d-9e5b3bc3091c"
        capabilities_uuid = "710af845-6d9f-6583-0c4d-9e5b3bc3091d"
        dis = [ordered]@{
            manufacturer = "listener"
            model = "keyboard-v2"
            hardware_revision = "esp32s3-wroom-1-n16r8"
            firmware_revision = $ota_version
            software_revision_protocol = "1"
        }
    }
    rollback = [ordered]@{
        supported = $true
        method = "esp_idf_bootloader_rollback"
        instructions = "If the new firmware fails pending-verify self-check (POST, BLE readiness, keyboard), the bootloader automatically rolls back to the previous partition on next reboot."
    }
    recovery = [ordered]@{
        factory_reflash = "Use package_factory_firmware.ps1 to restore factory image via USB/serial."
        serial_commands = "~OTA:STATUS to check current OTA state; ~OTA:BLOCKER to inspect update blockers; ~OTA:ABORT to cancel an in-progress update."
    }
}

$manifest_path = Join-Path $package_dir "ota_manifest.json"
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifest_path -Encoding UTF8

# --- Also generate factory package ---
$factory_output = Join-Path $package_dir "factory"
$factory_result = @(& pwsh -NoProfile -File (Join-Path $PSScriptRoot "package_factory_firmware.ps1") -BuildDir $build_path -OutputRoot $factory_output 2>&1)
if ($LASTEXITCODE -ne 0) {
    $factory_message = "Factory package generation failed for OTA package channel $Channel.`n$($factory_result -join "`n")"
    if ($Channel -eq "development") {
        Write-Warning $factory_message
    } else {
        Write-Error $factory_message
        exit 1
    }
} else {
    $factory_package = Get-ChildItem -LiteralPath $factory_output -Directory | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $factory_package) {
        Write-Error "Factory package generation did not produce a package directory under $factory_output."
        exit 1
    }

    $required_factory_files = @(
        "manifest.json",
        "FLASHING.md",
        "bootloader.bin",
        "partition-table.bin",
        "$project_name.bin"
    )
    foreach ($required_file in $required_factory_files) {
        $required_path = Join-Path $factory_package.FullName $required_file
        if (-not (Test-Path -LiteralPath $required_path)) {
            Write-Error "Factory package is incomplete; missing $required_file in $($factory_package.FullName)."
            exit 1
        }
    }
}

# --- Generate release bundle zip for customer-facing/manual update flows ---
$zip_path = "$package_dir.zip"
if (Test-Path -LiteralPath $zip_path) {
    Remove-Item -LiteralPath $zip_path -Force
}

$zip_inputs = @(
    $manifest_path,
    $ota_bin_dest
)
$factory_dir = Join-Path $package_dir "factory"
if (Test-Path -LiteralPath $factory_dir) {
    $zip_inputs += $factory_dir
}
Compress-Archive -LiteralPath $zip_inputs -DestinationPath $zip_path -Force

# --- Summary ---
Write-Host "OTA firmware package: $package_dir"
Write-Host "Firmware release zip package: $zip_path"
Write-Host "OTA manifest: $manifest_path"
Write-Host "OTA binary: $ota_bin_name ($ota_size bytes) sha256=$ota_hash"
if ($factory_package) {
    Write-Host "Factory package: $($factory_package.FullName)"
}
Write-Host "Channel: $Channel  Version: $ota_version  Source version: $project_version  Git dirty: $git_dirty"
