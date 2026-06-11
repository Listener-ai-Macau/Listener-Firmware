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
            "FreeRTOS tasks, GPIO polling, digital microphone capture, flash-backed diagnostics"
        )
        responsibilities = @(
            "Expose BLE HID keyboard behavior for the physical keys.",
            "Capture microphone audio and stream Listener BLE audio to the desktop app.",
            "Own device-side voice key state, serial commands, diagnostics, system health, and recovery evidence.",
            "Own firmware OTA slot/rollback primitives, BLE OTA GATT bridge, pending-verify checks, and OTA diagnostics used by desktop update flows.",
            "Provide build, flash, serial monitor, BLE HID, BLE audio, and diagnostic-log validation tools.",
            "Provide AI-readable firmware diagnostic bundles that preserve raw diag_log events, decode schema names from firmware headers, and summarize key parameter highlights."
        )
        major_features = @(
            "BLE HID keyboard fallback for logical KEY1-KEY4 custom keys using safe non-text gestures: single-click F13-F16, double-click F17-F20, and long-press F21-F24.",
            "BLE audio upload path for 16 kHz microphone audio sessions consumed by Listener-Type, including executable transport invariants for epoch, replay, backpressure, and stale GATT events.",
            "Voice key control for start/stop recording flow, including serial VREC commands, quiet BLE-audio pending retries, and gold REC status feedback.",
            "diag_log flash ring buffer for boot, BLE, audio, health, power, board, LED, WARN, and ERROR events, with runtime INFO masks for noisy sources.",
            "Default-off ~DIAGLOG:INPUTDBG records temporary KEY1-KEY4 and EC11 input traces for hardware bring-up.",
            "BLE diagnostic log GATT export service for paginated CRC-tagged firmware log pulls by desktop diagnostics.",
            "AI-readable diag_log JSON bundle tooling for deterministic event, argument, severity, boot-segment, source-count, warning/error, parameter-highlight, KEY1-KEY4, and EC11 input summary fields.",
            "Firmware OTA v1 using ESP-IDF otadata/ota_0/ota_1 slots, partition-derived flash offsets, BLE GATT control/data bridge, official rollback, pending verify, blockers, and diag_log OTA events.",
            "system_health heartbeat and resource checks for heap, task, BLE, and disconnect conditions.",
            "V2 N16R8 board profile with 16 MB flash, 8 MB Octal PSRAM, EC11 push power-on/runtime-custom/recovery on GPIO18, KEY1/2/3/4 HID gesture map on GPIO38/39/40/41, four-zone WS2812 resources, and static checks rejecting stale N4 defaults.",
            "power_manager low-power state machine for connected idle, disconnected idle, USB/VBUS automatic hardware-shutdown blocking, and long-idle PWR_HOLD/GPIO11 low-active hold/release-high hardware shutdown with reset/cold-boot diagnostics.",
            "Persisted ~DEVICE:SETTINGS contract for plugged/battery brightness, battery-only auto-shutdown timeout, and BLE name used by Listener-Type.",
            "V2 board diagnostics cover ~BOARD:STATUS, ~LED:STATUS, USB/charger state, protected battery percent, LED resources, brightness cap, status RGB, and blocker policies.",
            "V2 current telemetry reports TPS63020/SY7088 battery-side branch current on GPIO10/GPIO9 for the current N16R8 board; future revised boards may mark those sensors absent and still never use telemetry for power decisions.",
            "V2 safety gates keep real PWR_HOLD/GPIO11 power-off and LED VDD validation hardware-gated; N16R8 uses SPH0655 PDM on GPIO48/GPIO47.",
            "POST and degraded boot reporting for NVS, BLE, audio, heap, and board assumptions."
        )
        key_paths = @(
            [ordered]@{ path = "main/"; purpose = "Application startup, POST, BLE/audio/keyboard initialization." },
            [ordered]@{ path = "components/keyboard/"; purpose = "Physical key scanning and keyboard events." },
            [ordered]@{ path = "components/hid_keyboard/"; purpose = "Cross-platform HID keyboard abstraction." },
            [ordered]@{ path = "components/diag_log/"; purpose = "Diagnostic event schema, source mask API, runtime source commands, default-off input debug mode, and ring-buffer API." },
            [ordered]@{ path = "components/power_manager/"; purpose = "Low-power state machine, shutdown blockers, PWR_HOLD/GPIO11 hardware shutdown, and diagnostics." },
            [ordered]@{ path = "components/device_settings/"; purpose = "Persisted Type-facing board settings and ~DEVICE:SETTINGS command contract." },
            [ordered]@{ path = "components/battery_monitor/"; purpose = "Battery voltage and protected level: 3000mV empty, 4200mV full, 2700mV danger marker." },
            [ordered]@{ path = "docs/features/low_power_wake_policy.md"; purpose = "Firmware long-idle hardware shutdown contract for low-active PWR_HOLD/GPIO11 and USB/VBUS external-power blockers." },
            [ordered]@{ path = "tools/verify_charging_awake_policy_hardware.ps1"; purpose = "Hardware helper for USB/charging awake evidence plus unplugged/destructive manual gates." },
            [ordered]@{ path = "tools/decode_diag_log.py"; purpose = "Offline decoder for ~DIAGLOG JSONL into stable AI-readable JSON bundles." },
            [ordered]@{ path = "tools/collect_ai_diagnostics.ps1"; purpose = "Collect bounded serial diag_log evidence or decode saved JSONL into AI-readable artifacts." },
            [ordered]@{ path = "tools/collect_v2_current_telemetry.ps1"; purpose = "V2 current telemetry helper for TPS63020/SY7088 branch measurements and future revised-board absent evidence." },
            [ordered]@{ path = "ports/esp32/ble_diag_log/"; purpose = "BLE GATT service for paginated firmware diag_log export with per-chunk CRC." },
            [ordered]@{ path = "components/firmware_ota/"; purpose = "ESP-IDF OTA manager, rollback/pending-verify handling, blockers, and OTA serial diagnostics." },
            [ordered]@{ path = "ports/esp32/ble_firmware_ota/"; purpose = "NimBLE firmware OTA service with control/data characteristics and BLE abort integration." },
            [ordered]@{ path = "components/system_health/"; purpose = "Health status, heartbeat, and fault reporting." },
            [ordered]@{ path = "components/voice_recording_control/"; purpose = "Voice key state machine and serial control contract." },
            [ordered]@{ path = "ports/esp32/ble_hid*"; purpose = "ESP32 BLE HID service, GAP, pairing, and host connection." },
            [ordered]@{ path = "ports/esp32/ble_audio_stream*"; purpose = "ESP32 BLE audio transport and notifications." },
            [ordered]@{ path = "ports/esp32/audio_capture*"; purpose = "Digital microphone capture path, including V2 PDM RX." },
            [ordered]@{ path = "partitions.csv"; purpose = "V2 16 MB flash layout including OTA app slots and diag_log partition." },
            [ordered]@{ path = "tools/verify_v2_board_profile_static.ps1"; purpose = "Static V2 board profile, memory, pin, LED, current telemetry, partition, and package identity check." },
            [ordered]@{ path = "tools/"; purpose = "Build, flash, monitor, BLE, audio, and diagnostic validation scripts." }
        )
        hardware_assumptions = @(
            "Default active board is ESP32-S3-WROOM-1-N16R8 with 16 MB flash and 8 MB Octal PSRAM.",
            "Microphone path captures product-rate PCM from the active digital mic path; N16R8 validation builds use ESP-IDF PDM RX on CLK/GPIO48 and DOUT/GPIO47.",
            "Physical key GPIO mapping and voice key GPIO live in board pin configuration, not desktop code.",
            "V2 EC11-KEY/GPIO18 is the power-on key while off and sends the runtime custom-key fallback Shift+F13 after boot; KEY1/KEY2/KEY3/KEY4 use GPIO38/GPIO39/GPIO40/GPIO41 and fall back to F13-F24 gesture usages; EC11 encoder uses GPIO42/GPIO2/GPIO18.",
            "V2 long-idle shutdown is firmware-controlled by driving low-active PWR_HOLD/GPIO11 high; real power-off, short-press cold boot, and USB/VBUS blocker behavior require hardware-gated validation.",
            "Battery percentage uses the protected product range 3000mV=0% and 4200mV=100%; 2700mV is an absolute danger marker, not usable empty capacity.",
            "GPIO35/GPIO36/GPIO37 are reserved for the N16R8 module flash/PSRAM/MSPI interface.",
            "PWR_HOLD/GPIO11, RGB LEDs, and TPS63020/SY7088 current-sense telemetry on GPIO10/GPIO9 are populated in the active N16R8 profile; future revised board profiles may treat GPIO_NUM_NC current inputs as normal.",
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
            "pwsh -NoProfile -File .\tools\dump_diag_log.ps1 -Port <COMx> -Count 200",
            "pwsh -NoProfile -File .\tools\verify_v2_board_profile_static.ps1",
            "pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1",
            "pwsh -NoProfile -File .\tools\verify_device_settings_static.ps1",
            "pwsh -NoProfile -File .\tools\verify_charging_awake_policy_static.ps1",
            "pwsh -NoProfile -File .\tools\verify_charging_awake_policy_hardware.ps1 -Port <COMx> -ExpectExternalPower",
            "python .\tools\verify_ble_audio_transport_model.py",
            "pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1",
            "pwsh -NoProfile -File .\tools\verify_ble_audio_backpressure_static.ps1",
            "python .\tools\verify_ble_diag_log_gatt_contract.py",
            "pwsh -NoProfile -File .\tools\verify_ble_diag_log_audio_concurrency.ps1 -Port <COMx> -BluetoothAddress <addr>",
            "pwsh -NoProfile -File .\tools\collect_ai_diagnostics.ps1 -InputJsonl <diag_log.jsonl> -OutputDir .\tests\artifacts\ai_diagnostics",
            "pwsh -NoProfile -File .\tools\collect_ai_diagnostics.ps1 -Port <COMx> -RecentEventCount 200 -EnableSource keyboard,voice_key -Source keyboard,voice_key -OutputDir .\tests\artifacts\ai_diagnostics",
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

    function Test-RepoText {
        param([string]$RelativePath, [string]$Pattern, [string]$Label)
        $path = Join-Path $resolvedRepoRoot $RelativePath
        if (-not (Test-Path -LiteralPath $path)) {
            return @("missing file for $($Label): $RelativePath")
        }
        $text = Get-Content -LiteralPath $path -Raw
        if ($text -notmatch $Pattern) {
            return @("missing $($Label) in $($RelativePath): $Pattern")
        }
        return @()
    }

    foreach ($term in @("ESP32-S3", "N16R8", "Octal PSRAM", "BLE HID", "BLE audio", "diag_log", "system_health", "voice key", "build", "flash", "serial")) {
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
    $errors += @(Test-RepoText "components/device_settings/include/device_settings.h" 'DEVICE_SETTINGS_DEFAULT_BLE_NAME\s+"listener"' 'default BLE name')
    $errors += @(Test-RepoText "components/device_settings/include/device_settings.h" 'DEVICE_SETTINGS_DEFAULT_PLUGGED_BRIGHTNESS_PERCENT\s+80U' 'plugged brightness default')
    $errors += @(Test-RepoText "components/device_settings/include/device_settings.h" 'DEVICE_SETTINGS_DEFAULT_BATTERY_BRIGHTNESS_PERCENT\s+50U' 'battery brightness default')
    $errors += @(Test-RepoText "components/board/board.c" 'BOARD_V2_PWR_HOLD_POLICY\s+"v2_gpio11_power_latch_hold_low_release_high_for_hardware_shutdown"' 'PWR_HOLD low-active policy')
    $errors += @(Test-RepoText "components/board/board.c" 'board_set_power_hold_enabled[\s\S]*int level = enabled \? 0 : 1;[\s\S]*gpio_set_level\(BOARD_PINS_PWR_HOLD_IO,\s*level\)' 'PWR_HOLD LOW hold HIGH release implementation')
    if ($scriptText.Length -gt 18500) {
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
    Write-Output "PASS: firmware repo feature script is present, concise, and covers ESP32-S3 N16R8 BLE HID/audio diagnostics."
    exit 0
}

if ($Json) {
    $snapshot | ConvertTo-Json -Depth 10
    exit 0
}

Write-HumanSnapshot -Snapshot $snapshot
