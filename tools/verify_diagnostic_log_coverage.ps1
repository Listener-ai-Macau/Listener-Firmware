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

foreach ($name in @(
    "DIAG_SRC_SYSTEM",
    "DIAG_SRC_SELF_TEST",
    "DIAG_SRC_BLE_HID",
    "DIAG_SRC_BLE_GAP",
    "DIAG_SRC_OTA",
    "DIAG_SRC_HEALTH",
    "DIAG_SRC_POWER",
    "DIAG_SRC_BOARD",
    "DIAG_SRC_STATUS_LED",
    "DIAG_SRC_KEYBOARD",
    "DIAG_SRC_VOICE_KEY",
    "DIAG_SRC_AUDIO",
    "DIAG_SRC_VOICE_REC",
    "DIAG_SRC_BLE_AUDIO"
)) {
    if (-not $sourceNameToValue.ContainsKey($name)) {
        Add-CheckError "$eventsPath missing source used by source mask contract: $name"
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
    "DIAG_POWER_USB_DETECT",
    "DIAG_POWER_CHARGE_STATE",
    "DIAG_POWER_HOLD_STATE",
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
Assert-Contains -RelativePath "main/main.c" -Pattern "DIAG_POWER_WAKE" -Description "power wake diag event"
Assert-Contains -RelativePath "main/main.c" -Pattern "esp_sleep_get_wakeup_cause" -Description "power wake source capture"
Assert-Contains -RelativePath "main/main.c" -Pattern "esp_sleep_get_ext1_wakeup_status" -Description "power wake GPIO mask capture"

Assert-Contains -RelativePath "components/diag_log/diag_log.c" -Pattern 'DIAG_SRC_SYSTEM,\s*"system",\s*true' -Description "source mask default includes system"
Assert-Contains -RelativePath "components/diag_log/diag_log.c" -Pattern 'DIAG_SRC_SELF_TEST,\s*"self_test",\s*true' -Description "source mask default includes self_test"
Assert-Contains -RelativePath "components/diag_log/diag_log.c" -Pattern 'DIAG_SRC_BLE_HID,\s*"ble_hid",\s*true' -Description "source mask default includes ble_hid"
Assert-Contains -RelativePath "components/diag_log/diag_log.c" -Pattern 'DIAG_SRC_BLE_GAP,\s*"ble_gap",\s*true' -Description "source mask default includes ble_gap"
Assert-Contains -RelativePath "components/diag_log/diag_log.c" -Pattern 'DIAG_SRC_OTA,\s*"ota",\s*true' -Description "source mask default includes ota"
Assert-Contains -RelativePath "components/diag_log/diag_log.c" -Pattern 'DIAG_SRC_HEALTH,\s*"health",\s*true' -Description "source mask default includes health"
Assert-Contains -RelativePath "components/diag_log/diag_log.c" -Pattern 'DIAG_SRC_POWER,\s*"power",\s*true' -Description "source mask default includes power"
Assert-Contains -RelativePath "components/diag_log/diag_log.c" -Pattern 'DIAG_SRC_BOARD,\s*"board",\s*true' -Description "source mask default includes board"
Assert-Contains -RelativePath "components/diag_log/diag_log.c" -Pattern 'DIAG_SRC_STATUS_LED,\s*"status_led",\s*true' -Description "source mask default includes status_led"
Assert-Contains -RelativePath "components/diag_log/diag_log.c" -Pattern 'DIAG_SRC_KEYBOARD,\s*"keyboard",\s*false' -Description "source mask default disables keyboard INFO"
Assert-Contains -RelativePath "components/diag_log/diag_log.c" -Pattern 'DIAG_SRC_VOICE_KEY,\s*"voice_key",\s*false' -Description "source mask default disables voice_key INFO"
Assert-Contains -RelativePath "components/diag_log/diag_log.c" -Pattern 'DIAG_SRC_AUDIO,\s*"audio",\s*false' -Description "source mask default disables audio INFO"
Assert-Contains -RelativePath "components/diag_log/diag_log.c" -Pattern 'DIAG_SRC_VOICE_REC,\s*"voice_rec",\s*false' -Description "source mask default disables voice_rec INFO"
Assert-Contains -RelativePath "components/diag_log/diag_log.c" -Pattern 'DIAG_SRC_BLE_AUDIO,\s*"ble_audio",\s*false' -Description "source mask default disables ble_audio INFO"
Assert-Contains -RelativePath "components/diag_log/diag_log.c" -Pattern 'severity\s*>=\s*DIAG_SEV_WARN' -Description "WARN and ERROR bypass source mask"
Assert-Contains -RelativePath "components/diag_log/diag_log.c" -Pattern 'strcmp\(cmd_buffer,\s*"SOURCES"\)' -Description "DIAGLOG SOURCES command"
Assert-Contains -RelativePath "components/diag_log/diag_log.c" -Pattern 'strncmp\(cmd_buffer,\s*"ENABLE"' -Description "DIAGLOG ENABLE command"
Assert-Contains -RelativePath "components/diag_log/diag_log.c" -Pattern 'strncmp\(cmd_buffer,\s*"DISABLE"' -Description "DIAGLOG DISABLE command"
Assert-Contains -RelativePath "components/diag_log/diag_log.c" -Pattern 'diag_log_dump_last_by_source\(n,\s*source\)' -Description "source-filtered bounded tail command"
Assert-Contains -RelativePath "ports/esp32/diag_log_platform/diag_log_flash.c" -Pattern "diag_log_platform_dump_last_by_source" -Description "platform source-filtered bounded tail"
Assert-Contains -RelativePath "ports/esp32/diag_log_platform/diag_log_flash.c" -Pattern "DIAG_LOG_DUMP_PACE_EVENTS" -Description "diag_log export pacing batch"
Assert-Contains -RelativePath "ports/esp32/diag_log_platform/diag_log_flash.c" -Pattern "watchdog_platform_feed_current_task" -Description "diag_log export watchdog feed"
Assert-Contains -RelativePath "ports/esp32/diag_log_platform/diag_log_flash.c" -Pattern "snapshot_sector_events" -Description "diag_log export sector snapshot"
Assert-Contains -RelativePath "ports/esp32/diag_log_platform/diag_log_flash.c" -Pattern "status_led" -Description "status_led source name"
Assert-Contains -RelativePath "components/power_manager/power_manager.c" -Pattern "DIAG_POWER_USB_DETECT" -Description "USB detect transition diag"
Assert-Contains -RelativePath "components/power_manager/power_manager.c" -Pattern "DIAG_POWER_CHARGE_STATE" -Description "charge state transition diag"
Assert-Contains -RelativePath "components/power_manager/power_manager.c" -Pattern "DIAG_POWER_HOLD_STATE" -Description "PWR_HOLD transition diag"

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

Assert-Contains -RelativePath "tools/decode_diag_log.py" -Pattern "duplicate diag_log source id" -Description "duplicate source id rejection"
Assert-Contains -RelativePath "tools/decode_diag_log.py" -Pattern "duplicate diag_log event id" -Description "duplicate event id rejection"
Assert-Contains -RelativePath "tools/decode_diag_log.py" -Pattern "input_debug_summary" -Description "input debug summary"
Assert-Contains -RelativePath "tools/decode_diag_log.py" -Pattern "kbd_custom_key" -Description "KEY1-KEY4 summary source"
Assert-Contains -RelativePath "tools/decode_diag_log.py" -Pattern "kbd_ec11_detent" -Description "EC11 direction summary source"
Assert-Contains -RelativePath "tools/decode_diag_log.py" -Pattern "vkey_press" -Description "EC11 press summary source"
Assert-Contains -RelativePath "tools/collect_ai_diagnostics.ps1" -Pattern "TemporaryEnableSources" -Description "temporary source enable collection option"
Assert-Contains -RelativePath "tools/collect_ai_diagnostics.ps1" -Pattern "DISABLE" -Description "source cleanup disable path"
Assert-Contains -RelativePath "tools/collect_ai_diagnostics.ps1" -Pattern "source_state_path" -Description "final source state artifact"
Assert-Contains -RelativePath "tools/collect_ai_diagnostics.ps1" -Pattern "~DIAGLOG:SOURCES \+ ~DIAGLOG:ENABLE/DISABLE \+ ~DIAGLOG:LAST:N\[:source\]" -Description "bounded command path manifest"
Assert-Contains -RelativePath "tools/dump_diag_log.ps1" -Pattern '~DIAGLOG:LAST:\{0\}' -Description "bounded dump default"
Assert-Contains -RelativePath "tools/dump_diag_log.ps1" -Pattern '\[switch\]\$Full' -Description "explicit full dump switch"

foreach ($relativePath in @(
    "tools/verify_diagnostic_log_coverage.ps1",
    "tools/collect_ai_diagnostics.ps1",
    "tools/dump_diag_log.ps1",
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
