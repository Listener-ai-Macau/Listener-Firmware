[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$errors = [System.Collections.Generic.List[string]]::new()

function Add-CheckError {
    param([Parameter(Mandatory = $true)][string]$Message)
    $script:errors.Add($Message)
}

function Read-RepoText {
    param([Parameter(Mandatory = $true)][string]$RelativePath)

    $path = Join-Path $repoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path)) {
        Add-CheckError "missing file: $RelativePath"
        return ""
    }
    return Get-Content -LiteralPath $path -Raw
}

function Assert-Contains {
    param(
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $text = Read-RepoText -RelativePath $RelativePath
    if ($text -notmatch $Pattern) {
        Add-CheckError "$RelativePath missing $Description"
    }
}

function Assert-NotContains {
    param(
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $text = Read-RepoText -RelativePath $RelativePath
    if ($text -match $Pattern) {
        Add-CheckError "$RelativePath still contains $Description"
    }
}

function Convert-DefineValue {
    param([Parameter(Mandatory = $true)][string]$RawValue)

    if ($RawValue -match "^0x") {
        return [uint32][Convert]::ToUInt32($RawValue.Substring(2), 16)
    }
    return [uint32]$RawValue
}

$eventsPath = "components/diag_log/include/diag_log_events.h"
$events = Read-RepoText -RelativePath $eventsPath
$sourceNameToValue = @{}
$sourceValueToNames = @{}

foreach ($match in [regex]::Matches($events, "(?m)^\s*#define\s+(DIAG_SRC_[A-Z0-9_]+)\s+(0x[0-9A-Fa-f]+|\d+)\b")) {
    $name = $match.Groups[1].Value
    $value = Convert-DefineValue -RawValue $match.Groups[2].Value
    $sourceNameToValue[$name] = $value
    $key = $value.ToString()
    if (-not $sourceValueToNames.ContainsKey($key)) {
        $sourceValueToNames[$key] = [System.Collections.Generic.List[string]]::new()
    }
    $sourceValueToNames[$key].Add($name)
}

foreach ($entry in $sourceValueToNames.GetEnumerator()) {
    $names = @($entry.Value)
    if ($names.Count -gt 1) {
        Add-CheckError ("duplicate diag source id {0}: {1}" -f $entry.Key, ($names -join ", "))
    }
}

foreach ($name in @("DIAG_SRC_OTA", "DIAG_SRC_POWER", "DIAG_SRC_BLE_AUDIO")) {
    if (-not $sourceNameToValue.ContainsKey($name)) {
        Add-CheckError "$eventsPath missing $name"
    }
}

if ($sourceNameToValue.ContainsKey("DIAG_SRC_OTA") -and
    $sourceNameToValue.ContainsKey("DIAG_SRC_POWER") -and
    $sourceNameToValue["DIAG_SRC_OTA"] -eq $sourceNameToValue["DIAG_SRC_POWER"]) {
    Add-CheckError "DIAG_SRC_OTA and DIAG_SRC_POWER must not share a source id"
}

foreach ($macro in @(
    "DIAG_OTA_PARTITION",
    "DIAG_BAUD_NOTIFY_STATE",
    "DIAG_POWER_SLEEP_ENTRY",
    "DIAG_POWER_SLEEP_BLOCKED",
    "DIAG_POWER_WAKE",
    "DIAG_POWER_BLOCKER_CHANGE",
    "DIAG_POWER_STATUS",
    "DIAG_GAP_RECOVERY",
    "DIAG_BAUD_REPLAY"
)) {
    if ($events -notmatch "(?m)^\s*#define\s+$macro\b") {
        Add-CheckError "$eventsPath missing $macro"
    }
}

Assert-Contains -RelativePath "components/firmware_ota/firmware_ota.c" -Pattern "firmware_ota_log_partition_event" -Description "OTA partition diag event logging"
Assert-Contains -RelativePath "components/firmware_ota/firmware_ota.c" -Pattern "DIAG_OTA_PARTITION_(RUNNING|BOOT|UPDATE|NEXT)" -Description "OTA partition role events"
Assert-Contains -RelativePath "components/firmware_ota/firmware_ota.c" -Pattern "running_offset" -Description "OTA running offset status"
Assert-Contains -RelativePath "components/firmware_ota/firmware_ota.c" -Pattern "update_offset" -Description "OTA update offset status"
Assert-Contains -RelativePath "components/firmware_ota/include/firmware_ota.h" -Pattern "uint32_t\s+running_offset" -Description "OTA status running offset field"
Assert-Contains -RelativePath "components/firmware_ota/include/firmware_ota.h" -Pattern "uint32_t\s+update_size" -Description "OTA status update size field"
Assert-Contains -RelativePath "main/main.c" -Pattern "DIAG_POWER_WAKE" -Description "power reset diag event"
Assert-Contains -RelativePath "main/main.c" -Pattern "esp_reset_reason" -Description "power reset reason capture"
Assert-Contains -RelativePath "main/main.c" -Pattern "board_get_v2_power_hold_snapshot" -Description "PWR_HOLD boot diagnostic capture"
Assert-Contains -RelativePath "main/main.c" -Pattern "power cold-boot status" -Description "cold boot serial diagnostic"

Assert-Contains -RelativePath "tools/esp_idf_ci.ps1" -Pattern "Get-IdfPartitionTable" -Description "partition table parser"
Assert-Contains -RelativePath "tools/esp_idf_ci.ps1" -Pattern "Write-OtaPartitionEvidence" -Description "OTA partition evidence output"
Assert-Contains -RelativePath "tools/esp_idf_ci.ps1" -Pattern '\$appOffset\s*=\s*\$ota0\.Offset' -Description "app offset derived from ota_0"
Assert-NotContains -RelativePath "tools/esp_idf_ci.ps1" -Pattern '0x10000\s+\$bin' -Description "hard-coded app flash offset"

Assert-Contains -RelativePath "tools/package_factory_firmware.ps1" -Pattern "partition_table\s*=" -Description "manifest partition table evidence"
Assert-Contains -RelativePath "tools/package_factory_firmware.ps1" -Pattern '\$app_offset\s*=\s*\$ota0\.offset' -Description "package app offset derived from ota_0"
Assert-NotContains -RelativePath "tools/package_factory_firmware.ps1" -Pattern '0x10000\s+(\.\\)?\$project_name\.bin' -Description "hard-coded package app flash offset"

Assert-Contains -RelativePath "ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c" -Pattern "DIAG_BAUD_NOTIFY_STATE" -Description "BLE audio notify state diag event"
Assert-Contains -RelativePath "ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c" -Pattern "DIAG_BAUD_REPLAY" -Description "BLE audio replay diag event"
Assert-Contains -RelativePath "ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c" -Pattern "BLE_AUDIO_NOTIFY_STATE_DISABLED_ABORT" -Description "notify-disabled suspended state"
Assert-Contains -RelativePath "ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c" -Pattern "audio transport link suspended: reason=notify_disabled" -Description "notify-disabled recovery suspension log"
Assert-NotContains -RelativePath "ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c" -Pattern "notify_disabled_session_abort" -Description "obsolete notify-disabled immediate abort"
Assert-Contains -RelativePath "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c" -Pattern "DIAG_GAP_RECOVERY" -Description "BLE recovery diag event logging"
Assert-Contains -RelativePath "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c" -Pattern "recovery: clearing pairing bonds" -Description "BLE recovery serial action log"
Assert-Contains -RelativePath "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c" -Pattern "recovery: pairing reset complete" -Description "BLE recovery completion serial log"
Assert-Contains -RelativePath "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c" -Pattern "ble_hid_gap_prepare_shutdown_disconnect" -Description "hardware shutdown BLE disconnect preparation"

Assert-Contains -RelativePath "tools/decode_diag_log.py" -Pattern "duplicate diag_log source id" -Description "duplicate source id rejection"
Assert-Contains -RelativePath "tools/decode_diag_log.py" -Pattern "duplicate diag_log event id" -Description "duplicate event id rejection"

foreach ($relativePath in @(
    "tools/verify_diagnostic_log_coverage.ps1",
    "tools/esp_idf_ci.ps1",
    "tools/package_factory_firmware.ps1",
    "tools/ai/repo_features.ps1"
)) {
    $fullPath = Join-Path $repoRoot $relativePath
    if (-not (Test-Path -LiteralPath $fullPath)) {
        Add-CheckError "missing PowerShell file for parse check: $relativePath"
        continue
    }
    $tokens = $null
    $parseErrors = $null
    [System.Management.Automation.Language.Parser]::ParseFile($fullPath, [ref]$tokens, [ref]$parseErrors) | Out-Null
    foreach ($parseError in @($parseErrors)) {
        Add-CheckError ("{0}:{1}: {2}" -f $relativePath, $parseError.Extent.StartLineNumber, $parseError.Message)
    }
}

if ($errors.Count -gt 0) {
    throw ("diagnostic log coverage check failed:`n - " + ($errors -join "`n - "))
}

Write-Output "PASS: diagnostic log coverage checks passed."
