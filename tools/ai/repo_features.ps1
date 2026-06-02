param(
    [switch]$Json,
    [switch]$Check,
    [switch]$UpdateFromAccepted,
    [string]$Plan,
    [string]$StepId,
    [string]$Commit,
    [string]$RepoRoot
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$scriptDir = Split-Path -Parent $PSCommandPath
$defaultRepoRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)
$resolvedRepoRoot = if ($RepoRoot) { $RepoRoot } else { $defaultRepoRoot }
$acceptedLogPath = Join-Path $scriptDir "repo_features.accepted.jsonl"

function New-FeatureSnapshot {
    return [ordered]@{
        schema_version = 1
        repository = "voice-keyboard-firmware"
        purpose = "ESP32-S3 firmware for Listener voice keyboard hardware."
        stack = @(
            "ESP-IDF C firmware on ESP32-S3",
            "NimBLE BLE stack",
            "FreeRTOS tasks, GPIO polling, I2S microphone capture, flash-backed diagnostics"
        )
        responsibilities = @(
            "Expose BLE HID keyboard behavior for the physical keys.",
            "Capture microphone audio and stream Listener BLE audio to the desktop app.",
            "Own device-side voice key state, serial commands, diagnostics, system health, and recovery evidence.",
            "Own firmware OTA slot/rollback primitives, BLE OTA GATT bridge, pending-verify checks, and OTA diagnostics used by desktop update flows.",
            "Provide build, flash, serial monitor, BLE HID, BLE audio, and diagnostic-log validation tools.",
            "Provide AI-readable firmware diagnostic bundles that preserve raw diag_log events and decode schema names from firmware headers."
        )
        major_features = @(
            "BLE HID keyboard fallback for logical KEY1-KEY4 custom keys using safe non-text gestures: single-click F13-F16, double-click F17-F20, and long-press F21-F24.",
            "BLE audio upload path for 16 kHz microphone audio sessions consumed by Listener-Type.",
            "Voice key control for start/stop recording flow, including serial VREC commands.",
            "diag_log flash ring buffer for boot, BLE, audio, health, and error events that survive reboot.",
            "BLE diagnostic log GATT export service for paginated CRC-tagged firmware log pulls by desktop diagnostics.",
            "AI-readable diag_log JSON bundle tooling for deterministic event, argument, severity, boot-segment, and summary fields.",
            "Firmware OTA v1 using ESP-IDF otadata/ota_0/ota_1 slots, partition-derived flash offsets, BLE GATT control/data bridge, official rollback, pending verify, blockers, and diag_log OTA events.",
            "system_health heartbeat and resource checks for heap, task, BLE, and disconnect conditions.",
            "N4 board profile with 4 MB flash, no PSRAM, EC11 push recording control on GPIO35, KEY1/2/3/4 HID gesture map on GPIO45/48/47/21, and static checks rejecting stale V2/N16R8 defaults.",
            "power_manager low-power state machine for connected idle, disconnected idle, overnight sleep, N4 KEY4/GPIO21 wake diagnostics, production wake-policy blockers, and power blockers.",
            "V2 board diagnostics for ~BOARD:STATUS and ~LED:STATUS, including USB/charger provisional status, battery ADC, raw 3.3V and LED/5V current telemetry, LED resource mapping, and hardware blocker policy strings.",
            "V2 current telemetry and low-power report tooling for TPS63020_I_ADC/GPIO10, SY7088_I_ADC/GPIO9, and ~POWER:STATUS sleep drain evidence; readings are telemetry-only and do not drive firmware power-control decisions.",
            "V2 safety gates keep PWR_HOLD/GPIO46 undriven, LED calibration commands blocked until VDD_LED sign-off, current mA/mW uncalibrated, and CLK/GPIO48 DOUT/GPIO47 microphone capture degraded until validated.",
            "POST and degraded boot reporting for NVS, BLE, audio, heap, and board assumptions."
        )
        key_paths = @(
            [ordered]@{ path = "main/"; purpose = "Application startup, POST, BLE/audio/keyboard initialization." },
            [ordered]@{ path = "components/keyboard/"; purpose = "Physical key scanning and keyboard events." },
            [ordered]@{ path = "components/hid_keyboard/"; purpose = "Cross-platform HID keyboard abstraction." },
            [ordered]@{ path = "components/diag_log/"; purpose = "Diagnostic event schema and ring-buffer API." },
            [ordered]@{ path = "components/power_manager/"; purpose = "Low-power state machine, sleep blockers, overnight deep sleep, wake/status diagnostics." },
            [ordered]@{ path = "components/battery_monitor/"; purpose = "Shared battery voltage and level reading for HID and power diagnostics." },
            [ordered]@{ path = "docs/features/low_power_wake_policy.md"; purpose = "Firmware wake policy contract for N4 KEY4/GPIO21 deep-sleep wake, EC11-KEY/GPIO35 recording-key limitation, and production primary voice/wake requirements." },
            [ordered]@{ path = "tools/decode_diag_log.py"; purpose = "Offline decoder for ~DIAGLOG JSONL into stable AI-readable JSON bundles." },
            [ordered]@{ path = "tools/collect_ai_diagnostics.ps1"; purpose = "Collect recent serial diag_log events or decode saved JSONL into raw and decoded artifacts under tests/artifacts." },
            [ordered]@{ path = "tools/collect_v2_current_telemetry.ps1"; purpose = "Legacy V2 current telemetry helper; not part of the active N4 hardware path." },
            [ordered]@{ path = "ports/esp32/ble_diag_log/"; purpose = "BLE GATT service for paginated firmware diag_log export with per-chunk CRC." },
            [ordered]@{ path = "components/firmware_ota/"; purpose = "ESP-IDF OTA manager, rollback/pending-verify handling, blockers, and OTA serial diagnostics." },
            [ordered]@{ path = "ports/esp32/ble_firmware_ota/"; purpose = "NimBLE firmware OTA service with control/data characteristics and BLE abort integration." },
            [ordered]@{ path = "components/system_health/"; purpose = "Health status, heartbeat, and fault reporting." },
            [ordered]@{ path = "components/voice_recording_control/"; purpose = "Voice key state machine and serial control contract." },
            [ordered]@{ path = "ports/esp32/ble_hid*"; purpose = "ESP32 BLE HID service, GAP, pairing, and host connection." },
            [ordered]@{ path = "ports/esp32/ble_audio_stream*"; purpose = "ESP32 BLE audio transport and notifications." },
            [ordered]@{ path = "ports/esp32/audio_capture*"; purpose = "I2S microphone capture path." },
            [ordered]@{ path = "partitions.csv"; purpose = "N4 4 MB flash layout including OTA app slots and diag_log partition." },
            [ordered]@{ path = "tools/verify_v2_board_profile_static.ps1"; purpose = "Static N4 board profile, memory, pin, partition, and package identity check." },
            [ordered]@{ path = "tools/"; purpose = "Build, flash, monitor, BLE, audio, and diagnostic validation scripts." }
        )
        hardware_assumptions = @(
            "Default active board is ESP32-S3-WROOM-1-N4 with 4 MB flash and no PSRAM until the new N16R8 hardware arrives.",
            "Microphone path is SPH0645-style I2S digital audio at the product capture rate.",
            "Physical key GPIO mapping and voice key GPIO live in board pin configuration, not desktop code.",
            "N4 EC11-KEY/GPIO35 controls recording; KEY1/KEY2/KEY3/KEY4 use GPIO45/GPIO48/GPIO47/GPIO21 and fall back to F13-F24 gesture usages; EC11 encoder uses GPIO36/GPIO38/GPIO37/GPIO35.",
            "N4 deep-sleep wake uses KEY4/GPIO21; EC11-KEY/GPIO35 recording key is not RTC deep-sleep wake capable.",
            "Production V2 hardware must provide an RTC-capable primary voice/wake input; EC11-KEY_IO/GPIO11 is provisional until isolation, leakage, pull policy, and false-wake behavior are signed off.",
            "N4 PWR_HOLD, RGB LEDs, and V2 current-sense telemetry are not populated in the active profile.",
            "Real BLE, flash, serial, or audio capture validation requires a workflow hardware lock."
        )
        boundaries = @(
            "Desktop ASR, text polish, insertion, and settings UI live in Listener-Type.",
            "Industrial design, enclosure constraints, review renders, and manufacturing package live in voice-keyboard-design.",
            "Workflow claim, review, and cross-repo status live in ai-collaboration-workflow."
        )
        validation_commands = @(
            "pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check",
            "python -m compileall -q tools",
            "pwsh -NoProfile -File .\tools\build.ps1",
            "pwsh -NoProfile -File .\tools\flash.ps1 -Port <COMx>",
            "pwsh -NoProfile -File .\tools\monitor.ps1 -Port <COMx>",
            "pwsh -NoProfile -File .\tools\dump_diag_log.ps1 -Port <COMx>",
            "pwsh -NoProfile -File .\tools\verify_v2_board_profile_static.ps1",
            "pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1",
            "pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1",
            "pwsh -NoProfile -File .\tools\verify_ble_audio_backpressure_static.ps1",
            "python .\tools\verify_ble_diag_log_gatt_contract.py",
            "pwsh -NoProfile -File .\tools\verify_ble_diag_log_audio_concurrency.ps1 -Port <COMx> -BluetoothAddress <addr>",
            "pwsh -NoProfile -File .\tools\collect_ai_diagnostics.ps1 -InputJsonl <diag_log.jsonl> -OutputDir .\tests\artifacts\ai_diagnostics",
            "pwsh -NoProfile -File .\tools\verify_ble_hid.ps1",
            "pwsh -NoProfile -File .\tools\verify_physical_custom_key_hid.ps1 -Port <COMx>",
            "pwsh -NoProfile -File .\tools\verify_audio_ble_product_matrix.ps1",
            "python .\tools\verify_ble_ota_gatt_contract.py",
            "powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\verify_ble_ota_gatt_discovery.ps1 -DeviceName listener -BluetoothAddress <addr>",
            "git diff --check"
        )
        update_policy = "Record accepted changes only when they alter important firmware capabilities, hardware assumptions, BLE/audio/HID contracts, diagnostic behavior, or validation entry points."
    }
}

function Get-AcceptedChanges {
    if (-not (Test-Path $acceptedLogPath)) {
        return @()
    }

    $changes = @()
    foreach ($line in Get-Content -Path $acceptedLogPath) {
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }
        try {
            $changes += ($line | ConvertFrom-Json)
        } catch {
            $changes += [ordered]@{ parse_error = $_.Exception.Message; raw = $line }
        }
    }
    return $changes
}

function Get-FeatureSnapshot {
    $snapshot = New-FeatureSnapshot
    $snapshot["recent_accepted_changes"] = @(Get-AcceptedChanges)
    return $snapshot
}

function Test-FeatureSnapshot {
    param([Parameter(Mandatory = $true)]$Snapshot)

    $errors = @()
    $jsonText = $Snapshot | ConvertTo-Json -Depth 10
    $scriptText = Get-Content -Path $PSCommandPath -Raw

    foreach ($term in @("ESP32-S3", "N4", "no PSRAM", "BLE HID", "BLE audio", "diag_log", "system_health", "voice key", "build", "flash", "serial")) {
        if ($jsonText -notmatch [regex]::Escape($term)) {
            $errors += "missing required firmware feature term: $term"
        }
    }

    if (@($Snapshot["major_features"]).Count -lt 5) {
        $errors += "major_features must contain at least 5 entries"
    }
    if (@($Snapshot["key_paths"]).Count -lt 8) {
        $errors += "key_paths must contain at least 8 entries"
    }
    if (@($Snapshot["validation_commands"]).Count -lt 6) {
        $errors += "validation_commands must contain at least 6 entries"
    }
    if ($scriptText.Length -gt 17000) {
        $errors += "script is too long: $($scriptText.Length) characters"
    }

    return $errors
}

function Write-HumanSnapshot {
    param([Parameter(Mandatory = $true)]$Snapshot)

    Write-Output "# $($Snapshot["repository"])"
    Write-Output $Snapshot["purpose"]
    Write-Output ""
    Write-Output "Repo root: $resolvedRepoRoot"

    foreach ($section in @(
        @{ title = "Stack"; key = "stack" },
        @{ title = "Responsibilities"; key = "responsibilities" },
        @{ title = "Major Features"; key = "major_features" },
        @{ title = "Hardware Assumptions"; key = "hardware_assumptions" },
        @{ title = "Boundaries"; key = "boundaries" },
        @{ title = "Validation"; key = "validation_commands" }
    )) {
        Write-Output ""
        Write-Output "## $($section.title)"
        foreach ($item in $Snapshot[$section.key]) {
            Write-Output "  - $item"
        }
    }

    Write-Output ""
    Write-Output "## Key Paths"
    foreach ($entry in $Snapshot["key_paths"]) {
        Write-Output ("  - {0}: {1}" -f $entry["path"], $entry["purpose"])
    }

    if (@($Snapshot["recent_accepted_changes"]).Count -gt 0) {
        Write-Output ""
        Write-Output "## Recent Accepted Important Changes"
        foreach ($change in $Snapshot["recent_accepted_changes"]) {
            Write-Output ("  - {0}/{1} {2}" -f $change.plan, $change.step_id, $change.commit)
        }
    }

    Write-Output ""
    Write-Output "Update policy: $($Snapshot["update_policy"])"
}

if ($UpdateFromAccepted) {
    if (-not $Plan -or -not $StepId -or -not $Commit) {
        throw "-UpdateFromAccepted requires -Plan, -StepId, and -Commit."
    }

    $record = [ordered]@{
        recorded_at = (Get-Date).ToString("o")
        plan = $Plan
        step_id = $StepId
        commit = $Commit
        repo_root = $resolvedRepoRoot
        note = "Keep this entry only if the accepted work changed important firmware capabilities, hardware contracts, diagnostics, or validation."
    }
    $record | ConvertTo-Json -Depth 6 -Compress | Add-Content -Path $acceptedLogPath -Encoding UTF8
    Write-Output "Recorded accepted feature update hint: $acceptedLogPath"
    exit 0
}

$snapshot = Get-FeatureSnapshot

if ($Check) {
    $errors = Test-FeatureSnapshot -Snapshot $snapshot
    if (@($errors).Count -gt 0) {
        throw ("repo_features check failed:`n - " + ($errors -join "`n - "))
    }
    Write-Output "PASS: firmware repo feature script is present, concise, and covers ESP32-S3 BLE HID/audio diagnostics."
    exit 0
}

if ($Json) {
    $snapshot | ConvertTo-Json -Depth 10
    exit 0
}

Write-HumanSnapshot -Snapshot $snapshot
