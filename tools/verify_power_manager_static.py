from __future__ import annotations

import sys
import re
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


CHECKS = {
    "components/power_manager/include/power_manager.h": [
        "POWER_MANAGER_STATE_ACTIVE",
        "POWER_MANAGER_STATE_CONNECTED_IDLE",
        "POWER_MANAGER_STATE_DISCONNECTED_IDLE",
        "POWER_MANAGER_STATE_HARDWARE_SHUTDOWN",
        "POWER_MANAGER_BLOCKER_RECORDING",
        "POWER_MANAGER_BLOCKER_BLE_AUDIO",
        "POWER_MANAGER_BLOCKER_DIAG_EXPORT",
        "POWER_MANAGER_BLOCKER_EXTERNAL_POWER",
        "POWER_MANAGER_SHUTDOWN_REASON_LONG_IDLE",
        "POWER_MANAGER_SHUTDOWN_REASON_MANUAL_COMMAND",
        "shutdown_blockers",
        "user_idle_ms",
        "radio_idle_ms",
        "audio_idle_power_save_enabled",
        "audio_idle_blockers",
        "low_power_idle_threshold_ms",
        "hardware_shutdown_threshold_ms",
        "external_power_present",
        "usb_power_present",
        "charging",
        "charge_full",
        "charge_full_latched",
        "charge_full_candidate_ms",
        "charge_full_debounce_ms",
        "automatic_shutdown_blocked_by_external_power",
        "last_shutdown_reason",
        "last_shutdown_idle_ms",
        "last_shutdown_blockers",
        "pwr_hold_gpio",
        "pwr_hold_level",
        "pwr_hold_configured",
        "pwr_hold_policy",
        "hardware_shutdown_user_action",
        "power_manager_consume_usb_command",
    ],
    "components/power_manager/power_manager.c": [
        "~POWER:STATUS",
        "POWER_MANAGER_STATE_HARDWARE_SHUTDOWN",
        "CONFIG_POWER_MANAGER_HARDWARE_SHUTDOWN_MS",
        "POWER_MANAGER_SHUTDOWN_REASON_LONG_IDLE",
        "POWER_MANAGER_SHUTDOWN_REASON_MANUAL_COMMAND",
        "power_manager_enter_hardware_shutdown",
        "board_set_power_hold_enabled(false)",
        "board_set_power_hold_enabled(true)",
        "board_configure_power_hold_latch",
        "board_get_v2_power_hold_snapshot",
        "PWR_HOLD/GPIO11",
        "pwr_hold_gpio=%d",
        "pwr_hold_level=%s",
        "pwr_hold_configured=%u",
        "pwr_hold_policy=%s",
        "hardware_shutdown_ms=%",
        "shutdown_blockers=0x%08",
        "automatic hardware shutdown blocked by external power",
        "power_manager_shutdown_blockers_for_source",
        "power_manager_automatic_shutdown_blocked_by_external_power_locked",
        "board_get_v2_power_input_snapshot",
        "s_last_user_activity_ms",
        "s_last_radio_activity_ms",
        "power_manager_user_idle_ms_locked",
        "power_manager_radio_idle_ms_locked",
        "user_idle_ms=%",
        "radio_idle_ms=%",
        "external_power_present=%u",
        "charging=%u",
        "charge_full=%u",
        "usb_det_level=%s",
        "bat_chg_level=%s",
        "bat_std_level=%s",
        "pwr_hold_level=%s",
        "pwr_hold_policy=%s",
        "POWER_MANAGER_BLOCKER_EXTERNAL_POWER",
        "power_manager_awake_blockers",
        "power_manager_audio_idle_blockers",
        "power_manager_read_power_source",
        "power_manager_apply_charge_state_filter_locked",
        "POWER_MANAGER_CHARGE_FULL_DEBOUNCE_MS",
        "POWER_MANAGER_IDLE_BATTERY_REFRESH_MS",
        "POWER_MANAGER_LOW_POWER_EVALUATE_INTERVAL_MS",
        "POWER_MANAGER_LOW_BATTERY_SHUTDOWN_MAX_MV",
        "POWER_MANAGER_LOW_BATTERY_BOOT_GRACE_MS",
        "power_manager_should_refresh_battery_for_evaluate_locked",
        "power_manager_should_refresh_battery_for_snapshot_locked",
        "power_manager_low_battery_shutdown_confirmed_locked",
        "power_manager_copy_cached_battery_snapshot_locked",
        "power_manager_should_preserve_idle_for_ble_change_locked",
        "power_manager_apply_ble_connection_change_locked",
        "DIAG_POWER_SLEEP_ENTRY",
        "DIAG_POWER_SLEEP_BLOCKED",
        "DIAG_POWER_STATUS",
        "DIAG_POWER_WAKE",
        "DIAG_POWER_EXTERNAL_POWER",
        "esp_reset_reason",
        "DIAG_POWER_USB_DETECT",
        "DIAG_POWER_CHARGE_STATE",
        "DIAG_POWER_HOLD_STATE",
        "battery_monitor_read",
        "ble_hid_gap_prepare_shutdown_disconnect",
        "ble_hid_gap_request_low_power_connection",
        "ble_hid_gap_set_low_power_advertising",
        "ble_hid_gap_stop_advertising_for_key_wake",
        "audio_capture_set_idle_power_save",
        "audio_idle_power_save=%u",
        "audio_idle_blockers=0x%08",
        "low_power_idle_ms=%",
        "plugged_low_power_enabled=%u",
        "low_power_idle_allowed=%u",
        "status_led_prepare_sleep",
        "power_manager_low_power_idle_ms",
        "power_manager_plugged_low_power_enabled",
        "power_manager_guard_runtime_power_hold_low",
        "PWR_HOLD/GPIO11 runtime guard reasserting low",
        'strcmp(command, "SHUTDOWN")',
    ],
    "ports/esp32/audio_capture/audio_capture_esp32.c": [
        "audio_capture_set_idle_power_save",
        "i2s_channel_disable",
        "DIAG_AUDIO_IDLE_POWER",
    ],
    "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c": [
        "ble_hid_gap_set_low_power_advertising",
        "ble_hid_gap_stop_advertising_for_key_wake",
        "ble_hid_gap_prepare_shutdown_disconnect",
        "ble_gap_terminate",
        "ble_gap_adv_stop",
        "s_key_wake_only_advertising",
        "BLE advertising stopped: key-wake-only idle",
        "NimBLE advertising suppressed: key-wake-only idle",
        "BLE_GAP_ADV_ITVL_MS(adv_min_ms)",
        "DIAG_GAP_RECOVERY",
    ],
    "ports/esp32/ble_hid/ble_hid.c": [
        "power_manager_consume_usb_command",
        "POWER_MANAGER_BLOCKER_DIAG_EXPORT",
        "power_manager_set_ble_connected",
        "ble_hid_usb_command_is_passive_query",
        "ble_hid_usb_command_records_activity",
        "POWER:STATUS",
        "BOARD:STATUS",
        "BOARD:POWER",
        "LED:STATUS",
        "DEVICE:SETTINGS",
    ],
    "components/voice_recording_control/voice_recording_control.c": [
        "POWER_MANAGER_BLOCKER_RECORDING",
        "POWER_MANAGER_BLOCKER_BLE_AUDIO",
        "POWER_MANAGER_BLOCKER_PAIRING",
    ],
    "components/diag_log/include/diag_log_events.h": [
        "DIAG_SRC_POWER",
        "DIAG_POWER_STATE",
        "DIAG_POWER_WAKE",
        "a1=reset_reason",
        "a2=pwr_hold_gpio",
        "DIAG_POWER_BATTERY_WARN",
        "DIAG_POWER_EXTERNAL_POWER",
        "shutdown_blockers",
        "DIAG_POWER_USB_DETECT",
        "DIAG_POWER_CHARGE_STATE",
        "DIAG_POWER_HOLD_STATE",
    ],
    "components/power_manager/Kconfig.projbuild": [
        "POWER_MANAGER_ENABLE",
        "POWER_MANAGER_AUDIO_IDLE_MS",
        "POWER_MANAGER_HARDWARE_SHUTDOWN_MS",
        "POWER_MANAGER_BATTERY_CRITICAL_PERCENT",
        "default 0",
        "range 0 100",
    ],
    "sdkconfig.defaults": [
        "CONFIG_POWER_MANAGER_BATTERY_CRITICAL_PERCENT=0",
    ],
    "sdkconfig.defaults.esp32s3": [
        "CONFIG_PM_ENABLE=y",
        "CONFIG_PM_SLEEP_FUNC_IN_IRAM=y",
        "CONFIG_FREERTOS_USE_TICKLESS_IDLE=y",
        "CONFIG_BT_CTRL_MODEM_SLEEP=y",
        "CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION=y",
        "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y",
        "CONFIG_SPIRAM=y",
        "CONFIG_POWER_MANAGER_AUDIO_IDLE_MS=5000",
        "CONFIG_POWER_MANAGER_CONNECTED_IDLE_MS=60000",
        "CONFIG_POWER_MANAGER_DISCONNECTED_IDLE_MS=60000",
        "CONFIG_POWER_MANAGER_HARDWARE_SHUTDOWN_MS=1800000",
    ],
    "main/main.c": [
        "esp_pm_configure",
        "light_sleep_enable = true",
        "esp_reset_reason",
        "board_get_v2_power_hold_snapshot",
        "configure_boot_power_hold_latch",
        "board_configure_power_hold_latch",
        "power cold-boot status",
    ],
    "components/board/board.c": [
        "Voice Keyboard V2",
        "EC11 push/GPIO18",
        "PWR_HOLD/GPIO11",
        "v2_gpio11_power_latch_runtime_low_drive_high_for_hardware_shutdown",
        "GPIO_MODE_OUTPUT",
        "gpio_set_level(BOARD_PINS_PWR_HOLD_IO, 0)",
        "runtime low configured",
        "gpio_set_level(BOARD_PINS_PWR_HOLD_IO, 1)",
        "driven high for hardware shutdown",
        "BOARD_PWR_HOLD_RELEASE_SETTLE_MS",
        "board_wait_power_hold_readback",
        "board_verify_power_hold_readback",
        "refusing to enter silent hardware-shutdown wait",
        "~POWER:SHUTDOWN",
    ],
    "docs/features/low_power_wake_policy.md": [
        "2800mV=0%",
        "4200mV=100%",
        "2700mV",
        "default critical threshold `0%`",
        "forces hardware shutdown",
        "actively driven LOW during normal boot and runtime",
        "USB/VBUS, active charging, or charge-full status blocks this automatic low-battery shutdown",
        "BLE link churn is radio activity, not user activity",
    ],
}


FORBIDDEN = {
    "components/power_manager/include/power_manager.h": [
        "POWER_MANAGER_STATE_OVERNIGHT_SLEEP",
        "POWER_MANAGER_SLEEP_REASON",
        "POWER_MANAGER_WAKE_POLICY",
        "sleep_blockers",
        "last_sleep",
        "last_wake",
        "wake_gpio",
        "sleep_drain",
    ],
    "components/power_manager/power_manager.c": [
        "esp_deep_sleep_start",
        "esp_sleep_",
        "rtc_gpio_",
        "RTC_DATA_ATTR",
        "CONFIG_POWER_MANAGER_OVERNIGHT_SLEEP_MS",
        "CONFIG_LISTENER_V2_ENABLE_EC11_DEEP_SLEEP_WAKE",
        "POWER_MANAGER_STATE_OVERNIGHT_SLEEP",
        "POWER_MANAGER_SLEEP_REASON",
        "power_manager_configure_wakeup",
        "power_manager_wake_gpio_mask",
        "wake_policy=",
        "sleep_blockers=0x%08",
        "automatic overnight sleep",
        "entering deep sleep",
        "restoring hold high",
        "restoring hold low",
        "release-low",
        "shutdown-low",
    ],
    "components/board/board.c": [
        "v2_gpio46_power_latch_hold_high_release_low_for_hardware_shutdown",
        "v2_gpio46_power_latch_hold_low_release_high_for_hardware_shutdown",
        "v2_gpio11_power_latch_hold_high_release_low_for_hardware_shutdown",
        "v2_gpio11_power_latch_hold_low_release_high_for_hardware_shutdown",
        "hold-high",
        "hold-low",
        "released low for hardware shutdown",
        "held high",
        "held low",
    ],
    "main/main.c": [
        "esp_sleep_get_wakeup_cause",
        "esp_sleep_get_ext1_wakeup_status",
        "case ESP_RST_DEEPSLEEP",
    ],
    "sdkconfig.defaults.esp32s3": [
        "CONFIG_POWER_MANAGER_OVERNIGHT_SLEEP_MS",
        "CONFIG_LISTENER_V2_ENABLE_EC11_DEEP_SLEEP_WAKE",
    ],
    "sdkconfig.defaults": [
        "CONFIG_POWER_MANAGER_OVERNIGHT_SLEEP_MS",
        "CONFIG_LISTENER_V2_ENABLE_EC11_DEEP_SLEEP_WAKE",
    ],
    "ports/esp32/board_pins/Kconfig.projbuild": [
        "LISTENER_V2_ENABLE_EC11_DEEP_SLEEP_WAKE",
    ],
}


def main() -> int:
    failures: list[str] = []
    for relative_path, required_tokens in CHECKS.items():
        path = REPO_ROOT / relative_path
        if not path.is_file():
            failures.append(f"missing file: {relative_path}")
            continue
        text = path.read_text(encoding="utf-8")
        for token in required_tokens:
            if token not in text:
                failures.append(f"{relative_path}: missing token {token!r}")

    for relative_path, stale_tokens in FORBIDDEN.items():
        path = REPO_ROOT / relative_path
        if not path.is_file():
            failures.append(f"missing file for stale-token check: {relative_path}")
            continue
        text = path.read_text(encoding="utf-8")
        for token in stale_tokens:
            if token in text:
                failures.append(f"{relative_path}: stale token {token!r}")

    board = (REPO_ROOT / "components/board/board.c").read_text(encoding="utf-8")
    for stale in ("Voice Keyboard N4", "EC11 push/GPIO11", "EC11 push/GPIO35", "N4 deep sleep wakes by KEY4/GPIO21"):
        if stale in board:
            failures.append(f"components/board/board.c: stale token {stale!r}")
    if not re.search(
        r"board_print_power_rail_status\(battery_monitor_power_rail_t rail,\s*bool force_sample\)[\s\S]*"
        r"if\s*\(force_sample\)[\s\S]*battery_monitor_read_power_rail\(rail,\s*&status\)[\s\S]*"
        r"battery_monitor_get_cached_power_rail\(rail,\s*&status\)",
        board,
    ):
        failures.append(
            "components/board/board.c: BOARD:POWER must use cached current telemetry unless FORCE is requested"
        )
    if not re.search(
        r"sample_mode=%s cache_valid=%u cache_sequence=%[\s\S]*"
        r"force_sample\s*\?\s*\"force\"\s*:\s*\"cached\"",
        board,
    ):
        failures.append(
            "components/board/board.c: BOARD:POWER must report sample mode and cache state"
        )
    if not re.search(
        r"strcmp\(command,\s*\"POWER\"\)[\s\S]*board_print_power_status\(false\)[\s\S]*"
        r"strcmp\(command,\s*\"POWER:FORCE\"\)[\s\S]*board_print_power_status\(true\)",
        board,
    ):
        failures.append(
            "components/board/board.c: BOARD:POWER:FORCE must be the explicit ADC current sampling command"
        )
    if not re.search(
        r"board_verify_power_hold_readback[\s\S]*"
        r"actual_level\s*<\s*0[\s\S]*return\s+ESP_FAIL[\s\S]*"
        r"actual_level\s*!=\s*requested_level[\s\S]*return\s+ESP_ERR_INVALID_STATE",
        board,
    ):
        failures.append(
            "components/board/board.c: PWR_HOLD readback failure or mismatch must be a hard error"
        )
    if not re.search(
        r"board_set_power_hold_enabled[\s\S]*"
        r"gpio_set_level\(BOARD_PINS_PWR_HOLD_IO,\s*1\)[\s\S]*"
        r"GPIO_MODE_OUTPUT[\s\S]*"
        r"board_wait_power_hold_readback\(\"driven high for hardware shutdown\",\s*1\)[\s\S]*"
        r"return\s+ret",
        board,
    ):
        failures.append(
            "components/board/board.c: hardware shutdown drive-high must wait for PWR_HOLD readback before reporting success"
        )

    main_source = (REPO_ROOT / "main/main.c").read_text(encoding="utf-8")
    if not re.search(
        r"static\s+void\s+configure_boot_power_hold_latch\(void\)[\s\S]*"
        r"board_configure_power_hold_latch\(\)",
        main_source,
    ):
        failures.append(
            "main/main.c: early boot PWR_HOLD helper must configure the runtime-low latch"
        )
    if not re.search(
        r"void\s+app_main\(void\)\s*\{\s*configure_boot_power_hold_latch\(\);",
        main_source,
    ):
        failures.append(
            "main/main.c: app_main must drive PWR_HOLD/GPIO11 low before LED/BLE/diagnostic init"
        )

    power_manager = (REPO_ROOT / "components/power_manager/power_manager.c").read_text(encoding="utf-8")
    if not re.search(
        r"power_manager_apply_charge_state_filter_locked[\s\S]*"
        r"raw_full\s*&&[\s\S]*"
        r"!raw_charging\s*&&[\s\S]*"
        r"power_manager_charge_full_battery_allowed[\s\S]*"
        r"POWER_MANAGER_CHARGE_FULL_DEBOUNCE_MS[\s\S]*"
        r"source->charge_full\s*=\s*source->usb_power_present\s*&&\s*s_charge_full_latched",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: charge-full must be USB-gated and debounced against raw BAT_STD jitter"
        )
    if not re.search(
        r"power_manager_sync_power_source_locked[\s\S]*"
        r"state_changed[\s\S]*"
        r"s_charge_full\s*!=\s*source->charge_full[\s\S]*"
        r"raw_status_changed[\s\S]*"
        r"return\s+state_changed",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: raw charger pin changes must not by themselves emit power-source transitions"
        )
    if not re.search(
        r"POWER_MANAGER_IDLE_BATTERY_REFRESH_MS\s+600000U[\s\S]*"
        r"power_manager_should_refresh_battery_for_evaluate_locked[\s\S]*"
        r"s_state\s*!=\s*POWER_MANAGER_STATE_DISCONNECTED_IDLE[\s\S]*"
        r"source->usb_power_present\s*!=\s*s_usb_power_present[\s\S]*"
        r"now_ms\s*-\s*s_cached_battery_read_ms\s*>=\s*POWER_MANAGER_IDLE_BATTERY_REFRESH_MS",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: stable disconnected idle must use cached battery snapshots between long refresh intervals"
        )
    if not re.search(
        r"power_manager_should_refresh_battery_for_snapshot_locked[\s\S]*"
        r"s_state\s*!=\s*POWER_MANAGER_STATE_DISCONNECTED_IDLE[\s\S]*"
        r"source->usb_power_present\s*!=\s*s_usb_power_present[\s\S]*"
        r"return\s+false",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: POWER:STATUS must not force a battery ADC read during stable disconnected idle"
        )
    for function_name in (
        "power_manager_should_refresh_battery_for_evaluate_locked",
        "power_manager_should_refresh_battery_for_snapshot_locked",
    ):
        match = re.search(
            rf"static bool {function_name}\([^{{]+{{(?P<body>[\s\S]*?)\n}}\n",
            power_manager,
        )
        if match and re.search(r"source->bat_(?:chg|std)_level\s*!=", match.group("body")):
            failures.append(
                f"components/power_manager/power_manager.c: {function_name} must ignore raw charger-pin jitter while disconnected idle"
            )
    if not re.search(
        r"power_manager_evaluate[\s\S]*"
        r"power_manager_should_refresh_battery_for_evaluate_locked\(now_ms,\s*&power_source\)[\s\S]*"
        r"power_manager_copy_cached_battery_snapshot_locked\(&battery_snapshot\)[\s\S]*"
        r"power_manager_update_battery_snapshot\(&battery_snapshot\)[\s\S]*"
        r"power_manager_store_battery_snapshot_locked\(&battery_snapshot,\s*now_ms\)",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: evaluate loop must avoid unconditional 2s battery ADC reads in disconnected idle"
        )
    if not re.search(
        r"POWER_MANAGER_LOW_POWER_EVALUATE_INTERVAL_MS\s+60000U[\s\S]*"
        r"power_manager_task[\s\S]*"
        r"state\s*==\s*POWER_MANAGER_STATE_ACTIVE[\s\S]*"
        r"watchdog_platform_task_notify_take\([\s\S]*CONFIG_POWER_MANAGER_EVALUATE_INTERVAL_MS[\s\S]*"
        r"watchdog_platform_task_notify_take_low_power\([\s\S]*POWER_MANAGER_LOW_POWER_EVALUATE_INTERVAL_MS",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: stable idle must use a long low-power evaluate wait instead of 2s polling"
        )
    if not re.search(
        r"POWER_MANAGER_LOW_BATTERY_SHUTDOWN_MAX_MV\s+2800U[\s\S]*"
        r"POWER_MANAGER_LOW_BATTERY_CONFIRM_MS\s+5000U[\s\S]*"
        r"POWER_MANAGER_LOW_BATTERY_BOOT_GRACE_MS\s+15000U[\s\S]*"
        r"power_manager_low_battery_shutdown_confirmed_locked[\s\S]*"
        r"battery_snapshot->battery_mv\s*>\s*POWER_MANAGER_LOW_BATTERY_SHUTDOWN_MAX_MV[\s\S]*"
        r"s_low_battery_critical_since_ms[\s\S]*"
        r"POWER_MANAGER_LOW_BATTERY_CONFIRM_MS[\s\S]*"
        r"power_manager_user_idle_ms_locked\(now_ms\)\s*>=\s*POWER_MANAGER_LOW_BATTERY_BOOT_GRACE_MS",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: low-battery hardware shutdown must reject ADC/USB_DET startup transients"
        )
    if not re.search(
        r"power_manager_refresh_ble_connection_locked[\s\S]*"
        r"power_manager_apply_ble_connection_change_locked\(connected,\s*now_ms\)",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: BLE refresh must use the shared idle-preserving connection-change helper"
        )
    if not re.search(
        r"void\s+power_manager_set_ble_connected[\s\S]*"
        r"power_manager_apply_ble_connection_change_locked\(connected,\s*now_ms\)",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: BLE callbacks must use the shared idle-preserving connection-change helper"
        )
    if not re.search(
        r"power_manager_should_preserve_idle_for_ble_change_locked[\s\S]*"
        r"power_manager_awake_blockers\(s_blockers\)\s*==\s*0[\s\S]*"
        r"power_manager_user_idle_ms_locked\(now_ms\)\s*>=\s*"
        r"\(uint32_t\)CONFIG_POWER_MANAGER_AUDIO_IDLE_MS",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: BLE churn must preserve idle after the audio/user idle threshold"
        )
    if not re.search(
        r"power_manager_apply_ble_connection_change_locked[\s\S]*"
        r"s_ble_connected\s*=\s*connected[\s\S]*"
        r"power_manager_should_preserve_idle_for_ble_change_locked\(now_ms\)[\s\S]*"
        r"s_state\s*=\s*power_manager_target_state_locked\(now_ms\)[\s\S]*"
        r"s_last_radio_activity_ms\s*=\s*now_ms[\s\S]*"
        r"s_state\s*=\s*POWER_MANAGER_STATE_ACTIVE",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: BLE connection changes must preserve idle when possible and only refresh radio idle for real active windows"
        )
    if not re.search(
        r"void\s+power_manager_set_ble_connected[\s\S]*"
        r"if\s*\(changed\)\s*\{[\s\S]*"
        r"power_manager_apply_ble_connection_change_locked\(connected,\s*now_ms\)[\s\S]*"
        r"power_manager_apply_fast_idle_actions\(next,\s*user_idle_ms,\s*blockers\)",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: BLE callback must ignore duplicate same-state callbacks and immediately re-apply fast idle actions"
        )
    if not re.search(
        r"case\s+POWER_MANAGER_STATE_CONNECTED_IDLE:[\s\S]{0,220}"
        r"status_led_set_low_power_disabled\(true\)[\s\S]{0,240}"
        r"power_manager_set_audio_idle_power_save\(true\)",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: connected idle must turn off routine status LEDs before sleeping audio"
        )
    if not re.search(
        r"case\s+POWER_MANAGER_STATE_DISCONNECTED_IDLE:[\s\S]{0,80}"
        r"case\s+POWER_MANAGER_STATE_HARDWARE_SHUTDOWN:[\s\S]{0,220}"
        r"status_led_set_low_power_disabled\(true\)[\s\S]{0,240}"
        r"power_manager_set_audio_idle_power_save\(true\)",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: disconnected idle and hardware shutdown must turn off routine status LEDs"
        )
    if re.search(
        r"if\s*\(connected\)\s*\{[\s\S]{0,120}s_last_user_activity_ms\s*=",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: BLE reconnect must not reset the user-idle clock"
        )
    if "power_manager_wait_for_power_removal" in power_manager:
        failures.append(
            "components/power_manager/power_manager.c: shutdown failure must not enter an unbounded wait-for-power-removal loop"
        )
    if re.search(
        r"while\s*\(\s*1\s*\)\s*\{[\s\S]{0,180}"
        r"watchdog_platform_feed_current_task\(\)",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: shutdown failure must not stay in a silent watchdog-fed loop"
        )
    if "POWER_MANAGER_POWER_REMOVAL_WAIT_MS 750U" not in power_manager:
        failures.append(
            "components/power_manager/power_manager.c: shutdown power-removal observation window must be explicit and bounded"
        )
    if not re.search(
        r"power_manager_restore_after_shutdown_failure[\s\S]*"
        r"board_set_power_hold_enabled\(true\)[\s\S]*"
        r"DIAG_POWER_SLEEP_BLOCKED[\s\S]*"
        r"power_manager_schedule_shutdown_failure_retry",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: automatic failed shutdown must restore PWR_HOLD low, record diag, and enter retry cooldown"
        )
    if "POWER_MANAGER_SHUTDOWN_FAILURE_RETRY_MS 900000U" not in power_manager:
        failures.append(
            "components/power_manager/power_manager.c: shutdown failure retry cooldown must be explicit"
        )
    if "power_manager_shutdown_failure_retry_active_locked" not in power_manager:
        failures.append(
            "components/power_manager/power_manager.c: target state must honor shutdown failure retry cooldown"
        )
    if not re.search(
        r"board_set_power_hold_enabled\(false\)[\s\S]*"
        r"hold_ret\s*!=\s*ESP_OK[\s\S]*"
        r"power_manager_restore_after_shutdown_failure\(reason,\s*final_idle_ms,\s*hold_ret\)",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: PWR_HOLD drive-high/readback failure must use the shutdown restore path"
        )
    if not re.search(
        r"vTaskDelay\(pdMS_TO_TICKS\(POWER_MANAGER_POWER_REMOVAL_WAIT_MS\)\)[\s\S]*"
        r"hardware shutdown did not remove power[\s\S]*"
        r"power_manager_restore_after_shutdown_failure\(reason,\s*final_idle_ms,\s*ESP_FAIL\)",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: powered-after-shutdown fallback must restore runtime low after the bounded wait"
        )
    if not re.search(
        r"POWER_MANAGER_SHUTDOWN_REASON_LOW_BATTERY[\s\S]*"
        r"source->usb_power_present[\s\S]*"
        r"source->external_power_present[\s\S]*"
        r"source->charging[\s\S]*"
        r"source->charge_full[\s\S]*"
        r"POWER_MANAGER_BLOCKER_EXTERNAL_POWER",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: low-battery automatic shutdown must be blocked by USB/charging/full power source"
        )
    if not re.search(
        r"power_manager_low_battery_shutdown_confirmed_locked[\s\S]*"
        r"battery_snapshot->battery_level_percent\s*>\s*POWER_MANAGER_BATTERY_CRITICAL_PERCENT[\s\S]*"
        r"power_manager_low_battery_shutdown_allowed\(source\)[\s\S]*"
        r"POWER_MANAGER_LOW_BATTERY_CONFIRM_MS[\s\S]*"
        r"power_manager_user_idle_ms_locked\(now_ms\)[\s\S]*"
        r"low_battery_shutdown_confirmed[\s\S]*"
        r"power_manager_enter_hardware_shutdown\(POWER_MANAGER_SHUTDOWN_REASON_LOW_BATTERY\)",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: critical low-battery path must use the explicit power-source allow gate and transient guard"
        )
    if not re.search(
        r"power_manager_target_state_locked[\s\S]*"
        r"power_manager_awake_blockers\(s_blockers\)\s*!=\s*0[\s\S]*"
        r"POWER_MANAGER_STATE_ACTIVE",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: external power must not block connected/disconnected awake idle"
        )
    if not re.search(
        r"power_manager_awake_idle_state_locked[\s\S]*"
        r"s_external_power_present\s*&&\s*!power_manager_plugged_low_power_enabled\(\)[\s\S]*"
        r"POWER_MANAGER_STATE_ACTIVE[\s\S]*"
        r"uint32_t low_power_idle_ms\s*=\s*power_manager_low_power_idle_ms\(\)[\s\S]*"
        r"radio_idle_ms\s*>=\s*low_power_idle_ms",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: connected/disconnected idle must use the device-settings low-power timeout and plugged low-power switch"
        )
    if not re.search(
        r"power_manager_get_snapshot[\s\S]*"
        r"low_power_idle_threshold_ms\s*=\s*power_manager_low_power_idle_ms\(\)[\s\S]*"
        r"connected_idle_threshold_ms\s*=\s*snapshot->low_power_idle_threshold_ms[\s\S]*"
        r"disconnected_idle_threshold_ms\s*=\s*snapshot->low_power_idle_threshold_ms[\s\S]*"
        r"plugged_low_power_enabled\s*=\s*power_manager_plugged_low_power_enabled\(\)[\s\S]*"
        r"low_power_idle_allowed",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: POWER:STATUS must report the effective low-power timeout and plugged low-power allowance"
        )
    if not re.search(
        r"power_manager_guard_runtime_power_hold_low[\s\S]*"
        r"state\s*==\s*POWER_MANAGER_STATE_HARDWARE_SHUTDOWN[\s\S]*"
        r"board_set_power_hold_enabled\(true\)",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: runtime PWR_HOLD guard must reassert low outside hardware shutdown"
        )
    if not re.search(
        r"strcmp\(command,\s*\"STATUS\"\)[\s\S]*"
        r"power_manager_print_status\(\)[\s\S]*"
        r"power_manager_record_activity\(\"usb_power_command\"\)",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: POWER:STATUS must remain a passive query before activity recording"
        )

    ble_gap = (REPO_ROOT / "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c").read_text(encoding="utf-8")
    if "s_shutdown_quiesce" not in ble_gap:
        failures.append("ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c: missing shutdown quiesce state")
    if not re.search(
        r"ble_hid_gap_stop_advertising_for_key_wake[\s\S]*"
        r"s_key_wake_only_advertising\s*=\s*true[\s\S]*"
        r"s_directed_adv_pending\s*=\s*false[\s\S]*"
        r"ble_gap_adv_stop\(\)",
        ble_gap,
    ):
        failures.append(
            "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c: disconnected idle must stop advertising for key-wake-only mode"
        )
    if not re.search(
        r"esp_hid_ble_gap_adv_start[\s\S]*"
        r"s_key_wake_only_advertising[\s\S]*"
        r"NimBLE advertising suppressed: key-wake-only idle[\s\S]*"
        r"return\s+ESP_OK",
        ble_gap,
    ):
        failures.append(
            "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c: key-wake-only mode must suppress advertising starts"
        )
    if not re.search(
        r"ble_hid_gap_request_reconnect[\s\S]*"
        r"s_key_wake_only_advertising\s*=\s*false[\s\S]*"
        r"s_directed_adv_pending\s*=\s*true[\s\S]*"
        r"ble_hid_gap_start_advertising\(\)",
        ble_gap,
    ):
        failures.append(
            "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c: physical wake reconnect must leave key-wake-only mode and restart advertising"
        )
    if not re.search(
        r"ble_hid_gap_forget_bonds_and_repair[\s\S]{0,260}"
        r"s_key_wake_only_advertising\s*=\s*false",
        ble_gap,
    ):
        failures.append(
            "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c: recovery pairing must be able to reopen advertising from key-wake-only idle"
        )
    for label, pattern in (
        (
            "advertising start",
            r"esp_hid_ble_gap_adv_start[\s\S]*s_shutdown_quiesce[\s\S]*return\s+ESP_OK",
        ),
        (
            "disconnect callback",
            r"BLE_GAP_EVENT_DISCONNECT[\s\S]*s_shutdown_quiesce[\s\S]*suppressing advertising restart after disconnect[\s\S]*return\s+0",
        ),
        (
            "advertise-complete callback",
            r"BLE_GAP_EVENT_ADV_COMPLETE[\s\S]*s_shutdown_quiesce[\s\S]*suppressing advertising restart after adv complete[\s\S]*return\s+0",
        ),
        (
            "shutdown entry",
            r"ble_hid_gap_prepare_shutdown_disconnect[\s\S]*s_shutdown_quiesce\s*=\s*true",
        ),
        (
            "explicit reconnect recovery",
            r"ble_hid_gap_request_reconnect[\s\S]*s_shutdown_quiesce\s*=\s*false",
        ),
    ):
        if not re.search(pattern, ble_gap):
            failures.append(
                f"ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c: shutdown quiesce must suppress {label}"
            )

    ble_hid = (REPO_ROOT / "ports/esp32/ble_hid/ble_hid.c").read_text(encoding="utf-8")
    if not re.search(
        r"if\s*\(!ble_hid_is_connected\(\)\)\s*\{[\s\S]{0,120}"
        r"power_manager_record_activity\(\"hid_(?:key|usage|consumer)_wake\"\)[\s\S]{0,120}"
        r"ble_hid_gap_request_reconnect\(\)",
        ble_hid,
    ):
        failures.append(
            "ports/esp32/ble_hid/ble_hid.c: physical HID activity must wake key-only idle before requesting reconnect"
        )
    if not re.search(
        r"BLE_HID_BATTERY_DISCONNECTED_IDLE_INTERVAL_MS\s+600000[\s\S]*"
        r"POWER_MANAGER_STATE_CONNECTED_IDLE[\s\S]*"
        r"!s_ble_connected\s*&&\s*!force_notify[\s\S]*"
        r"battery update skipped while disconnected",
        ble_hid,
    ):
        failures.append(
            "ports/esp32/ble_hid/ble_hid.c: BLE battery task must back off connected/disconnected low-power idle and skip routine disconnected updates"
        )
    if not re.search(
        r"ble_hid_usb_command_is_passive_query[\s\S]*"
        r"POWER:STATUS[\s\S]*BOARD:STATUS[\s\S]*BOARD:POWER[\s\S]*BOARD:POWER:FORCE[\s\S]*LED:STATUS[\s\S]*DEVICE:SETTINGS",
        ble_hid,
    ):
        failures.append(
            "ports/esp32/ble_hid/ble_hid.c: passive board, power, LED, and device queries must be enumerated"
        )
    if not re.search(
        r"ble_hid_usb_command_records_activity[\s\S]*"
        r"ble_hid_usb_command_is_passive_query\(line\)[\s\S]*return\s+false[\s\S]*"
        r"POWER:SHUTDOWN[\s\S]*POWER:ACTIVITY[\s\S]*LED:WAKE[\s\S]*DEVICE:SET",
        ble_hid,
    ):
        failures.append(
            "ports/esp32/ble_hid/ble_hid.c: USB activity must be gated to explicit active commands"
        )
    if re.search(
        r"ble_hid_dispatch_usb_command_line[\s\S]{0,240}"
        r"if\s*\(!ble_hid_usb_command_is_passive_query\(line\)\)[\s\S]{0,240}"
        r"power_manager_record_activity\(\"usb_control_line\"\)",
        ble_hid,
    ):
        failures.append(
            "ports/esp32/ble_hid/ble_hid.c: USB activity must not be recorded for every non-passive command"
        )
    if not re.search(
        r"ble_hid_dispatch_usb_command_line[\s\S]{0,240}"
        r"if\s*\(ble_hid_usb_command_records_activity\(line\)\)[\s\S]{0,180}"
        r"power_manager_record_activity\(\"usb_control_line\"\)",
        ble_hid,
    ):
        failures.append(
            "ports/esp32/ble_hid/ble_hid.c: dispatch must record usb_control_line only through the explicit activity gate"
        )
    activity_gate = re.search(
        r"static\s+bool\s+ble_hid_usb_command_records_activity[\s\S]*?\n\}",
        ble_hid,
    )
    if activity_gate is None:
        failures.append("ports/esp32/ble_hid/ble_hid.c: missing USB activity gate body")
    else:
        gate_text = activity_gate.group(0)
        for token in ("BOARD:NO_SUCH_COMMAND", "LED:NO_SUCH_COMMAND", "NO_SUCH:COMMAND"):
            if token in gate_text:
                failures.append(
                    f"ports/esp32/ble_hid/ble_hid.c: invalid command {token!r} must not be activity-gated"
                )
    if not re.search(
        r"strcmp\(command,\s*\"STATUS\"\)[\s\S]*"
        r"power_manager_print_status\(\)[\s\S]*"
        r"strcmp\(command,\s*\"SHUTDOWN\"\)[\s\S]*"
        r"power_manager_record_activity\(\"usb_power_command\"\)[\s\S]*"
        r"strcmp\(command,\s*\"ACTIVITY\"\)[\s\S]*"
        r"power_manager_record_activity\(\"usb_power_command\"\)[\s\S]*"
        r"POWER:\s+unknown command",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: POWER unknown/status commands must stay passive while SHUTDOWN/ACTIVITY record activity"
        )

    monitor = (REPO_ROOT / "tools/monitor_idle_power.py").read_text(encoding="utf-8")
    if not re.search(
        r"--board-power[\s\S]*choices=\(\"none\",\s*\"cached\",\s*\"force\"\)[\s\S]*default=\"none\"",
        monitor,
    ):
        failures.append(
            "tools/monitor_idle_power.py: idle monitor must default to status-only telemetry"
        )
    if not re.search(
        r"args\.board_power\s*==\s*\"force\"[\s\S]*~BOARD:POWER:FORCE\\n[\s\S]*"
        r"elif args\.board_power\s*==\s*\"cached\"[\s\S]*~BOARD:POWER\\n",
        monitor,
    ):
        failures.append(
            "tools/monitor_idle_power.py: forced current ADC sampling must be explicit and separate from cached idle polling"
        )

    if failures:
        print("FAIL: power manager static verification failed")
        for failure in failures:
            print(f" - {failure}")
        return 1

    print(
        "PASS: power manager static verification covers hardware shutdown, PWR_HOLD/GPIO11, "
        "external-power blockers, configurable idle actions, PWR_HOLD guard, diagnostics, and Deep Sleep removal."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
