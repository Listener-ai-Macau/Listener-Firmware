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
            "Expose BLE HID keyboard behavior and voice key state.",
            "Capture microphone audio and stream Listener BLE audio.",
            "Own serial commands, diagnostics, system health, recovery, and OTA primitives.",
            "Provide build, flash, serial, BLE, audio, and diag_log validation tools.",
            "Provide AI-readable diagnostic bundles from raw diag_log events."
        )
        major_features = @(
            "BLE HID fallback for KEY1-KEY4 custom keys: single F13-F16, double F17-F20, long F21-F24.",
            "BLE audio upload path for 16 kHz Listener-Type sessions.",
            "Voice key start/stop flow with serial VREC commands and gold REC feedback.",
            "diag_log flash ring buffer for boot, BLE, audio, health, power, board, LED, WARN, and ERROR events, with runtime INFO masks for noisy sources.",
            "Status LED flash diagnostics persist PWR/BLE/OK/WARN flags, PWR RGB/class, output off/resume, and LED-side power state.",
            "Default-off ~DIAGLOG:INPUTDBG records temporary KEY1-KEY4 and EC11 input traces for hardware bring-up.",
            "BLE diag_log GATT export with paginated CRC-tagged pulls.",
            "AI-readable diag_log JSON bundles for event, warning/error, parameter, key, and EC11 summaries.",
            "Firmware OTA v1 with ESP-IDF OTA slots, BLE bridge, rollback, pending verify, blockers, and diag events.",
            "system_health heartbeat for heap, task, BLE, and disconnect conditions.",
            "V2 N16R8 profile: 16 MB flash, 8 MB Octal PSRAM, EC11 GPIO18, KEY1-KEY4 GPIOs, WS2812 zones.",
            "power_manager handles idle, BLE churn, charger-status blockers, low-battery blocking, manual ~POWER:TEST:SHUTDOWN, and PWR_HOLD/GPIO9 diagnostics.",
            "Persisted ~DEVICE:SETTINGS for brightness, idle timeouts, plugged low-power, disable-able auto-shutdown, and BLE name.",
            "Latest V2 N16R8 pin map uses PWR_HOLD/GPIO9, BAT_V_ADC/GPIO10, and no populated current-sense chips.",
            "Real PWR_HOLD/GPIO9 power-off and LED VDD validation stay hardware-gated."
        )
        key_paths = @(
            [ordered]@{ path = "main/"; purpose = "Startup, POST, BLE/audio/keyboard init." },
            [ordered]@{ path = "components/keyboard/"; purpose = "Physical key scanning." },
            [ordered]@{ path = "components/hid_keyboard/"; purpose = "HID keyboard abstraction." },
            [ordered]@{ path = "components/diag_log/"; purpose = "diag_log schema, masks, input debug, ring buffer." },
            [ordered]@{ path = "components/power_manager/"; purpose = "Low-power, blockers, PWR_HOLD/GPIO9, diagnostics." },
            [ordered]@{ path = "components/status_led/"; purpose = "LED rendering plus flash LED diagnostics." },
            [ordered]@{ path = "components/device_settings/"; purpose = "Persisted ~DEVICE:SETTINGS contract." },
            [ordered]@{ path = "components/battery_monitor/"; purpose = "2850-4150mV protected battery level." },
            [ordered]@{ path = "docs/features/low_power_wake_policy.md"; purpose = "PWR_HOLD and charger-status blocker contract." },
            [ordered]@{ path = "tools/device_maintenance.ps1"; purpose = "Operator maintenance entry for ports, probe, flash, bootloader restore, and flash checks." },
            [ordered]@{ path = "tools/verify_charging_awake_policy_hardware.ps1"; purpose = "USB/charging awake hardware gate." },
            [ordered]@{ path = "tools/decode_diag_log.py"; purpose = "Decode ~DIAGLOG JSONL to AI bundle." },
            [ordered]@{ path = "tools/verify_unplugged_flash_diag_bundle.py"; purpose = "Verify unplugged LED flash diag evidence." },
            [ordered]@{ path = "tools/collect_ai_diagnostics.ps1"; purpose = "Collect/decode bounded diag_log evidence." },
            [ordered]@{ path = "tools/collect_v2_current_telemetry.ps1"; purpose = "Optional current-rail diagnostic helper; current board reports not_populated." },
            [ordered]@{ path = "ports/esp32/ble_diag_log/"; purpose = "BLE GATT diag_log export." },
            [ordered]@{ path = "components/firmware_ota/"; purpose = "ESP-IDF OTA manager and diagnostics." },
            [ordered]@{ path = "ports/esp32/ble_firmware_ota/"; purpose = "NimBLE firmware OTA service." },
            [ordered]@{ path = "components/system_health/"; purpose = "Health heartbeat and fault reporting." },
            [ordered]@{ path = "components/voice_recording_control/"; purpose = "Voice key state machine." },
            [ordered]@{ path = "ports/esp32/ble_hid*"; purpose = "ESP32 BLE HID and GAP." },
            [ordered]@{ path = "ports/esp32/ble_audio_stream*"; purpose = "ESP32 BLE audio transport." },
            [ordered]@{ path = "ports/esp32/audio_capture*"; purpose = "Digital microphone capture path." },
            [ordered]@{ path = "partitions.csv"; purpose = "V2 16 MB flash layout including OTA app slots and diag_log partition." },
            [ordered]@{ path = "tools/verify_v2_board_profile_static.ps1"; purpose = "Static V2 board profile, memory, pin, LED, current telemetry, partition, and package identity check." },
            [ordered]@{ path = "tools/"; purpose = "Build, flash, monitor, BLE, audio, and diagnostic validation scripts." }
        )
        hardware_assumptions = @(
            "Active board is ESP32-S3-WROOM-1-N16R8 with 16 MB flash and 8 MB Octal PSRAM.",
            "N16R8 validation uses PDM RX on CLK/GPIO48 and DOUT/GPIO47.",
            "Physical key GPIO mapping lives in board pins, not desktop code.",
            "V2 EC11-KEY/GPIO18 powers on while off; after boot it sends Shift+F13. KEY1-KEY4 use GPIO38-41.",
            "V2 shutdown drives runtime-low PWR_HOLD/GPIO9 high; real power-off and blockers require hardware validation.",
            "Bench sessions can disable battery and plugged inactivity shutdown with ~DEVICE:SET auto_shutdown_minutes=off plugged_auto_shutdown_minutes=off, then use ~POWER:TEST:SHUTDOWN for true serial-triggered shutdown.",
            "USB-unplug light-cycle claims require decoded flash diag timelines: status_led power_input/visual/output plus power external/sleep_wake.",
            "Battery percentage uses the protected product range 2850mV=0% and 4150mV=100%; 2700mV is an absolute danger marker, not usable empty capacity.",
            "GPIO35/GPIO36/GPIO37 are reserved for the N16R8 module flash/PSRAM/MSPI interface.",
            "PWR_HOLD/GPIO9 and BAT_V_ADC/GPIO10 are populated; TPS63020/SY7088 current-sense telemetry is not populated and must report GPIO_NUM_NC.",
            "Real BLE, flash, serial, or audio validation requires a hardware lock."
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
            "pwsh -NoProfile -File .\tools\device_maintenance.ps1 -Action help",
            "pwsh -NoProfile -File .\tools\dump_diag_log.ps1 -Port <COMx> -Count 200",
            "python .\tools\verify_unplugged_flash_diag_bundle.py --bundle <diag_log_ai_bundle.json> --expect-pwr-class amber --expect-pwr-class green",
            "python .\tools\verify_serial_no_reset_static.py",
            "python .\tools\verify_current_docs_static.py",
            "pwsh -NoProfile -File .\tools\verify_v2_board_profile_static.ps1",
            "pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1",
            "pwsh -NoProfile -File .\tools\verify_device_settings_static.ps1",
            "pwsh -NoProfile -File .\tools\verify_charging_awake_policy_static.ps1",
            "pwsh -NoProfile -File .\tools\verify_charging_awake_policy_hardware.ps1 -Port <COMx> -ExpectExternalPower",
            "pwsh -NoProfile -File .\tools\verify_low_power_unplug_wake_hardware.ps1 -Port <COMx>",
            "python .\tools\verify_ble_audio_transport_model.py",
            "pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1",
            "pwsh -NoProfile -File .\tools\collect_ai_diagnostics.ps1 -Port <COMx> -RecentEventCount 200 -EnableSource keyboard,voice_key -Source keyboard,voice_key -OutputDir .\tests\artifacts\ai_diagnostics",
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
    $errors += @(Test-RepoText "components/device_settings/include/device_settings.h" 'DEVICE_SETTINGS_DEFAULT_LOW_POWER_IDLE_MS\s+60000U' 'low-power idle default')
    $errors += @(Test-RepoText "components/device_settings/include/device_settings.h" 'DEVICE_SETTINGS_DEFAULT_PLUGGED_LOW_POWER_ENABLED\s+0' 'plugged low-power default off')
    $errors += @(Test-RepoText "sdkconfig.defaults.esp32s3" 'CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION=y' 'USB Serial/JTAG stays awake while connected in release defaults')
    $errors += @(Test-RepoText "components/power_manager/power_manager.c" 'plugged_low_power_enabled' 'plugged low-power effective status')
    $errors += @(Test-RepoText "components/power_manager/power_manager.c" 'TEST:SHUTDOWN' 'serial manual shutdown test alias')
    $errors += @(Test-RepoText "components/device_settings/device_settings.c" 'auto_shutdown_ms_0_off_or_' 'auto-shutdown disabled setting contract')
    $errors += @(Test-RepoText "tools/device_maintenance.ps1" 'restore-bootloader' 'device maintenance helper')
    $errors += @(Test-RepoText "components/board/board.c" 'BOARD_V2_PWR_HOLD_POLICY\s+"v2_gpio9_power_latch_runtime_low_drive_high_for_hardware_shutdown"' 'PWR_HOLD runtime-low policy')
    $errors += @(Test-RepoText "ports/esp32/board_pins/include/board_pins.h" 'BOARD_PINS_BAT_V_ADC_IO\s+\(GPIO_NUM_10\)[\s\S]*BOARD_PINS_PWR_HOLD_IO\s+\(GPIO_NUM_9\)[\s\S]*BOARD_PINS_CURRENT_TELEMETRY_PRESENT\s+\(0\)[\s\S]*BOARD_PINS_TPS63020_I_ADC_IO\s+\(GPIO_NUM_NC\)[\s\S]*BOARD_PINS_SY7088_I_ADC_IO\s+\(GPIO_NUM_NC\)' 'latest V2 pin map and absent current telemetry')
    $errors += @(Test-RepoText "components/board/board.c" 'gpio_set_level\(BOARD_PINS_PWR_HOLD_IO,\s*0\)[\s\S]*GPIO_MODE_OUTPUT[\s\S]*runtime low configured[\s\S]*gpio_set_level\(BOARD_PINS_PWR_HOLD_IO,\s*1\)[\s\S]*GPIO_MODE_OUTPUT[\s\S]*board_wait_power_hold_readback\("driven high for hardware shutdown",\s*1\)' 'PWR_HOLD runtime-low drive-high shutdown implementation')
    $errors += @(Test-RepoText "components/status_led/status_led.c" 'DIAG_LED_VISUAL_STATE' 'status LED visual flash diagnostics')
    $errors += @(Test-RepoText "tools/verify_current_docs_static.py" 'keeps restrained PWR/BLE status visible' 'current docs stale-rollback guard')
    $errors += @(Test-RepoText "tools/decode_diag_log.py" 'led_visual_state_flags' 'decoded status LED visual flash diagnostics')
    $errors += @(Test-RepoText "tools/verify_unplugged_flash_diag_bundle.py" 'off followed by visible-on recovery' 'unplugged flash diag verifier')
    $errors += @(Test-RepoText "tools/verify_low_power_unplug_wake_hardware.ps1" '\[System\.Windows\.Forms\.MessageBox\]::Show[\s\S]*serial_opened port=.*dtr=0 rts=0[\s\S]*power_input_wake_configured=1' 'low-power unplug wake guided hardware validation')
    $errors += @(Test-RepoText "docs/features/low_power_wake_policy.md" 'decoded `status_led\.power_input`, `status_led\.visual_state`, `status_led\.output_state`' 'unplugged flash diag validation requirement')
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
