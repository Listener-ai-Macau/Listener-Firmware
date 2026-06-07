[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PackageDir,
    [string]$ExpectedProject = "voice-keyboard-firmware"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$packagePath = (Resolve-Path -LiteralPath $PackageDir).Path
$errors = [System.Collections.Generic.List[string]]::new()

function Add-CheckError {
    param([Parameter(Mandatory = $true)][string]$Message)
    $script:errors.Add($Message)
}

function Require-True {
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Message
    )
    if (-not $Condition) {
        Add-CheckError $Message
    }
}

function Require-String {
    param(
        [AllowNull()][string]$Text,
        [Parameter(Mandatory = $true)][string]$Needle,
        [Parameter(Mandatory = $true)][string]$Description
    )
    if ([string]::IsNullOrEmpty($Text) -or -not $Text.Contains($Needle)) {
        Add-CheckError "$Description missing '$Needle'"
    }
}

function Require-ArrayContains {
    param(
        [AllowNull()]$Values,
        [Parameter(Mandatory = $true)][string]$Needle,
        [Parameter(Mandatory = $true)][string]$Description
    )
    $items = @($Values | ForEach-Object { [string]$_ })
    if ($items -notcontains $Needle) {
        Add-CheckError "$Description missing '$Needle'"
    }
}

function Get-ManifestArtifact {
    param([Parameter(Mandatory = $true)][string]$Role)
    return @($manifest.artifacts | Where-Object { $_.role -eq $Role } | Select-Object -First 1)[0]
}

function Get-ManifestPartition {
    param([Parameter(Mandatory = $true)][string]$Name)
    return @($manifest.flash.partition_table | Where-Object { $_.name -eq $Name } | Select-Object -First 1)[0]
}

$manifestPath = Join-Path $packagePath "manifest.json"
$flashingPath = Join-Path $packagePath "FLASHING.md"

foreach ($file in @(
    "manifest.json",
    "FLASHING.md",
    "bootloader.bin",
    "partition-table.bin",
    "$ExpectedProject.bin"
)) {
    $path = Join-Path $packagePath $file
    Require-True -Condition (Test-Path -LiteralPath $path) -Message "missing package file: $file"
}

if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "manifest.json is missing from $packagePath"
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$flashing = if (Test-Path -LiteralPath $flashingPath) {
    Get-Content -LiteralPath $flashingPath -Raw
} else {
    ""
}

Require-True -Condition ([int]$manifest.schema_version -eq 1) -Message "manifest schema_version must be 1"
Require-True -Condition ([string]$manifest.project -eq $ExpectedProject) -Message "manifest project must be $ExpectedProject"
Require-String -Text ([string]$manifest.version) -Needle "" -Description "manifest version"
Require-True -Condition ([string]$manifest.target -eq "esp32s3") -Message "manifest target must be esp32s3"
Require-String -Text ([string]$manifest.git_commit) -Needle "" -Description "manifest git_commit"

$identity = $manifest.ble_identity
Require-True -Condition ([string]$identity.name -eq "listener") -Message "BLE name must be listener"
Require-True -Condition ([string]$identity.appearance -eq "0x03C1") -Message "BLE appearance must be 0x03C1"
Require-True -Condition ([string]$identity.hid_service_uuid -eq "1812") -Message "HID service UUID must be 1812"
Require-True -Condition ([string]$identity.audio_service_uuid -eq "710af845-6d9f-6583-0c4d-9e5b3bc3091a") -Message "audio service UUID mismatch"
Require-True -Condition ([string]$identity.audio_notify_uuid -eq "710af845-6d9f-6583-0c4d-9e5b3bc3091b") -Message "audio notify UUID mismatch"
Require-True -Condition ([string]$identity.audio_control_uuid -eq "710af845-6d9f-6583-0c4d-9e5b3bc3091e") -Message "audio control UUID mismatch"
Require-True -Condition ([string]$identity.readiness_uuid -eq "710af845-6d9f-6583-0c4d-9e5b3bc3091c") -Message "readiness UUID mismatch"
Require-True -Condition ([string]$identity.capabilities_uuid -eq "710af845-6d9f-6583-0c4d-9e5b3bc3091d") -Message "capabilities UUID mismatch"

$dis = $identity.dis
Require-True -Condition ([string]$dis.manufacturer -eq "listener") -Message "DIS manufacturer must be listener"
Require-True -Condition ([string]$dis.model -eq "keyboard-v2") -Message "DIS model must be keyboard-v2"
Require-True -Condition ([string]$dis.hardware_revision -eq "esp32s3-wroom-1-n16r8") -Message "DIS hardware revision must be V2 N16R8"
Require-True -Condition ([string]$dis.firmware_revision -eq [string]$manifest.version) -Message "DIS firmware revision must match manifest version"
Require-True -Condition ([string]$dis.software_revision_protocol -eq "1") -Message "DIS protocol revision must be 1"

foreach ($token in @(
    "factory_ready",
    "pairable_on_boot",
    "post_degraded_boot",
    "board=voice-keyboard-v2-n16r8",
    "model=keyboard-v2",
    "fw_version="
)) {
    Require-String -Text ([string]$manifest.readiness) -Needle $token -Description "readiness contract"
}

foreach ($capability in @(
    "ble_hid_keyboard",
    "ble_audio_vka1",
    "ble_audio_control_v1",
    "usb_serial_text",
    "voice_record_toggle",
    "custom_keys_f13_f16",
    "custom_key_gestures_f13_f24",
    "post_status",
    "firmware_ota_v1",
    "flash_16mb",
    "psram_8mb_octal"
)) {
    Require-ArrayContains -Values $manifest.capabilities -Needle $capability -Description "capabilities"
}

Require-String -Text ([string]$manifest.diagnostics.post_failure_behavior) -Needle "degraded" -Description "POST diagnostic behavior"
Require-String -Text ([string]$manifest.diagnostics.human_observation_deferred) -Needle "hardware/human gates" -Description "physical-observation deferral"
foreach ($command in @(
    "~OTA:STATUS",
    "~DIS:GATT",
    "~OTA:GATT",
    "~DIAG:GATT",
    "~BOARD:STATUS",
    "~POWER:STATUS",
    "~DIAGLOG:COUNT",
    "~DIAGLOG:LAST:32"
)) {
    Require-ArrayContains -Values $manifest.diagnostics.serial_commands -Needle $command -Description "serial diagnostics"
}

$ota0 = Get-ManifestPartition -Name "ota_0"
$ota1 = Get-ManifestPartition -Name "ota_1"
$otadata = Get-ManifestPartition -Name "otadata"
Require-True -Condition ($null -ne $ota0) -Message "manifest partition evidence missing ota_0"
Require-True -Condition ($null -ne $ota1) -Message "manifest partition evidence missing ota_1"
Require-True -Condition ($null -ne $otadata) -Message "manifest partition evidence missing otadata"

Require-String -Text ([string]$manifest.flash.command) -Needle "write_flash" -Description "flash command"
Require-String -Text ([string]$manifest.flash.command) -Needle "0x0 bootloader.bin" -Description "bootloader flash offset"
Require-String -Text ([string]$manifest.flash.command) -Needle "0x8000 partition-table.bin" -Description "partition flash offset"
if ($null -ne $ota0) {
    Require-String -Text ([string]$manifest.flash.command) -Needle ([string]$ota0.offset) -Description "app flash offset from ota_0"
}

$artifactExpectations = @(
    @{ Role = "bootloader"; File = "bootloader.bin"; Offset = "0x0" },
    @{ Role = "partition_table"; File = "partition-table.bin"; Offset = "0x8000" },
    @{ Role = "app"; File = "$ExpectedProject.bin"; Offset = if ($null -ne $ota0) { [string]$ota0.offset } else { "" } }
)

foreach ($expected in $artifactExpectations) {
    $artifact = Get-ManifestArtifact -Role $expected.Role
    if ($null -eq $artifact) {
        Add-CheckError "manifest artifacts missing role $($expected.Role)"
        continue
    }

    Require-True -Condition ([string]$artifact.file -eq $expected.File) -Message "artifact $($expected.Role) file mismatch"
    if ($expected.Offset) {
        Require-True -Condition ([string]$artifact.offset -eq $expected.Offset) -Message "artifact $($expected.Role) offset mismatch"
    }

    $artifactPath = Join-Path $packagePath ([string]$artifact.file)
    if (-not (Test-Path -LiteralPath $artifactPath)) {
        Add-CheckError "artifact file missing: $($artifact.file)"
        continue
    }

    $fileInfo = Get-Item -LiteralPath $artifactPath
    Require-True -Condition ([int64]$artifact.size_bytes -eq [int64]$fileInfo.Length) -Message "artifact $($artifact.file) size mismatch"
    $hash = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Require-True -Condition ([string]$artifact.sha256 -eq $hash) -Message "artifact $($artifact.file) sha256 mismatch"
}

foreach ($needle in @(
    "First Power-On Contract",
    "BLE GAP name: listener",
    "BLE appearance: 0x03C1 keyboard",
    "HID service: 1812",
    "DIS manufacturer/model/hardware/firmware/protocol fields match manifest.json",
    "710af845-6d9f-6583-0c4d-9e5b3bc3091c",
    "710af845-6d9f-6583-0c4d-9e5b3bc3091d",
    "~OTA:STATUS",
    "~DIS:GATT",
    "~DIAGLOG:LAST:32",
    "physical BLE scan is not available"
)) {
    Require-String -Text $flashing -Needle $needle -Description "FLASHING.md"
}

if ($errors.Count -gt 0) {
    throw ("factory firmware package check failed:`n - " + ($errors -join "`n - "))
}

Write-Output "PASS: factory firmware package valid: $packagePath"
