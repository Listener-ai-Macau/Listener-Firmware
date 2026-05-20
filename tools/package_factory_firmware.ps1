param(
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build"),
    [string]$OutputRoot = (Join-Path $PSScriptRoot "..\.cache\factory_firmware"),
    [string]$Port = "COM3",
    [string]$Baud = "460800"
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$project_root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$build_path = (Resolve-Path $BuildDir).Path
$output_root_path = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputRoot)

$description_path = Join-Path $build_path "project_description.json"
$project_name = "voice-keyboard-firmware"
$project_version = $null
$target = "esp32s3"

if (Test-Path $description_path) {
    $description = Get-Content -Raw $description_path | ConvertFrom-Json
    if ($description.project_name) { $project_name = [string]$description.project_name }
    if ($description.project_version) { $project_version = [string]$description.project_version }
    if ($description.target) { $target = [string]$description.target }
}

if (-not $project_version) {
    $project_version = (& git -C $project_root describe --tags --always --dirty 2>$null)
    if (-not $project_version) { $project_version = "0.1.0-dev" }
}

$safe_version = $project_version -replace '[^A-Za-z0-9_.-]', '_'
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$package_name = "listener-factory-$safe_version-$timestamp"
$package_dir = Join-Path $output_root_path $package_name
New-Item -ItemType Directory -Force $package_dir | Out-Null

$inputs = @(
    [ordered]@{
        role = "bootloader"
        offset = "0x0"
        source = Join-Path $build_path "bootloader\bootloader.bin"
        file = "bootloader.bin"
    },
    [ordered]@{
        role = "partition_table"
        offset = "0x8000"
        source = Join-Path $build_path "partition_table\partition-table.bin"
        file = "partition-table.bin"
    },
    [ordered]@{
        role = "app"
        offset = "0x10000"
        source = Join-Path $build_path "$project_name.bin"
        file = "$project_name.bin"
    }
)

$artifacts = @()
foreach ($input_item in $inputs) {
    if (-not (Test-Path $input_item.source)) {
        throw "Missing firmware artifact: $($input_item.source). Run idf.py build first."
    }

    $destination = Join-Path $package_dir $input_item.file
    Copy-Item -LiteralPath $input_item.source -Destination $destination -Force
    $hash = Get-FileHash -LiteralPath $destination -Algorithm SHA256
    $file_info = Get-Item -LiteralPath $destination

    $artifacts += [ordered]@{
        role = $input_item.role
        file = $input_item.file
        offset = $input_item.offset
        size_bytes = $file_info.Length
        sha256 = $hash.Hash.ToLowerInvariant()
    }
}

$git_commit = (& git -C $project_root rev-parse HEAD 2>$null)
if (-not $git_commit) { $git_commit = "unknown" }
$git_status = @(& git -C $project_root status --porcelain 2>$null)
$git_dirty = $git_status.Count -gt 0

$manifest = [ordered]@{
    schema_version = 1
    created_at_utc = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
    project = $project_name
    version = $project_version
    target = $target
    git_commit = $git_commit
    git_dirty = $git_dirty
    ble_identity = [ordered]@{
        name = "listener"
        appearance = "0x03C1"
        hid_service_uuid = "1812"
        audio_service_uuid = "710af845-6d9f-6583-0c4d-9e5b3bc3091a"
        audio_notify_uuid = "710af845-6d9f-6583-0c4d-9e5b3bc3091b"
        readiness_uuid = "710af845-6d9f-6583-0c4d-9e5b3bc3091c"
        capabilities_uuid = "710af845-6d9f-6583-0c4d-9e5b3bc3091d"
        dis = [ordered]@{
            manufacturer = "listener"
            model = "keyboard-v1"
            hardware_revision = "esp32s3-devkit"
            firmware_revision = $project_version
            software_revision_protocol = "1"
        }
    }
    readiness = "factory_ready;pairable_on_boot;post_degraded_boot"
    capabilities = @(
        "ble_hid_keyboard",
        "ble_audio_vka1",
        "usb_serial_text",
        "key1_record_toggle",
        "post_status"
    )
    flash = [ordered]@{
        chip = $target
        port = $Port
        baud = $Baud
        command = "python `$env:IDF_PATH\components\esptool_py\esptool\esptool.py --chip $target -p $Port -b $Baud --before=default_reset --after=hard_reset write_flash 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 $project_name.bin"
    }
    artifacts = $artifacts
}

$manifest_path = Join-Path $package_dir "manifest.json"
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifest_path -Encoding UTF8

$flash_doc = @"
# Listener Factory Firmware Package

Version: $project_version

Target: $target

BLE name: listener

Appearance: 0x03C1 keyboard

DIS firmware revision: $project_version

DIS software revision / protocol: 1

Readiness characteristic: 710af845-6d9f-6583-0c4d-9e5b3bc3091c

Capabilities characteristic: 710af845-6d9f-6583-0c4d-9e5b3bc3091d

## Flash

Run this command from this package directory:

````powershell
python `$env:IDF_PATH\components\esptool_py\esptool\esptool.py --chip $target -p $Port -b $Baud --before=default_reset --after=hard_reset write_flash `
    0x0 .\bootloader.bin `
    0x8000 .\partition-table.bin `
    0x10000 .\$project_name.bin
````

## SHA256

"@

foreach ($artifact in $artifacts) {
    $flash_doc += "- $($artifact.file) @ $($artifact.offset): $($artifact.sha256) ($($artifact.size_bytes) bytes)`n"
}

$flash_doc += @"

## First Power-On Contract

After flashing and reset, the device advertises as listener without any serial command. POST failures are logged and the firmware continues into degraded BLE mode so status remains observable.
"@

Set-Content -LiteralPath (Join-Path $package_dir "FLASHING.md") -Value $flash_doc -Encoding UTF8

Write-Host "Factory firmware package: $package_dir"
Write-Host "Manifest: $manifest_path"
foreach ($artifact in $artifacts) {
    Write-Host "$($artifact.role) $($artifact.file) $($artifact.offset) sha256=$($artifact.sha256)"
}
