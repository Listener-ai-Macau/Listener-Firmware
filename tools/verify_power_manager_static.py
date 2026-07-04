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
        "POWER_MANAGER_BLOCKER_OTA",
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
        "usb_det_adc_valid",
        "usb_det_adc_mv",
        "usb_det_mismatch",
        "charger_active",
        "charge_power_present",
        "charging",
        "charge_full",
        "charge_full_latched",
        "charge_full_candidate_ms",
        "charge_full_debounce_ms",
        "automatic_shutdown_blocked_by_external_power",
        "last_shutdown_reason",
        "last_shutdown_idle_ms",
        "last_shutdown_blockers",
        "last_shutdown_persisted",
        "last_shutdown_battery_mv",
        "last_shutdown_battery_level_percent",
        "last_shutdown_power_flags",
        "pwr_hold_gpio",
        "pwr_hold_level",
        "pwr_hold_configured",
        "pwr_hold_policy",
        "hardware_shutdown_user_action",
        "power_manager_consume_usb_command",
    ],
    "components/power_manager/power_manager.c": [
        "~POWER:STATUS",
        "~POWER:IDLE_DIAG",
        "POWER_MANAGER_STATE_HARDWARE_SHUTDOWN",
        "CONFIG_POWER_MANAGER_HARDWARE_SHUTDOWN_MS",
        "POWER_MANAGER_SHUTDOWN_REASON_LONG_IDLE",
        "POWER_MANAGER_SHUTDOWN_REASON_MANUAL_COMMAND",
        "power_manager_enter_hardware_shutdown",
        "board_set_power_hold_enabled(false)",
        "board_set_power_hold_enabled(true)",
        "board_configure_power_hold_latch",
        "POWER_MANAGER_NVS_NAMESPACE",
        "POWER_MANAGER_NVS_SHUTDOWN_REASON_KEY",
        "POWER_MANAGER_NVS_SHUTDOWN_IDLE_MS_KEY",
        "POWER_MANAGER_NVS_SHUTDOWN_BLOCKERS_KEY",
        "POWER_MANAGER_NVS_SHUTDOWN_BATTERY_MV_KEY",
        "POWER_MANAGER_NVS_SHUTDOWN_BATTERY_LEVEL_KEY",
        "POWER_MANAGER_NVS_SHUTDOWN_POWER_FLAGS_KEY",
        "POWER_MANAGER_SHUTDOWN_TRACE_MAGIC",
        "power_manager_load_persisted_shutdown_trace",
        "power_manager_persist_shutdown_trace",
        "board_get_v2_power_hold_snapshot",
        "PWR_HOLD/GPIO9",
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
        "reason=%s",
        "next=%s",
        "low_power_wait_ms=%",
        "audio_wait_ms=%",
        "awake_blocker_names=%s",
        "external_power_present=%u",
        "charger_active=%u",
        "charge_power_present=%u",
        "charging=%u",
        "charge_full=%u",
        "usb_det_level=%s",
        "usb_det_adc_valid=%u",
        "usb_det_adc_mv=%d",
        "usb_det_mismatch=%u",
        "bat_chg_level=%s",
        "bat_std_level=%s",
        "pwr_hold_level=%s",
        "pwr_hold_policy=%s",
        "POWER_MANAGER_BLOCKER_EXTERNAL_POWER",
        "POWER_MANAGER_BLOCKER_OTA",
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
        "power_manager_configure_power_input_wake",
        "power_manager_update_power_input_irq_arm",
        "s_power_input_irq_armed",
        "esp_sleep_enable_gpio_wakeup",
        "gpio_wakeup_enable",
        "gpio_intr_enable",
        "gpio_intr_disable",
        "power input wake ready",
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
        "power_input_wake_configured=%u",
        "power_input_irq_armed=%u",
        "last_shutdown_persisted=%u",
        "last_shutdown_battery_mv=%",
        "last_shutdown_battery_level=%u",
        "last_shutdown_power_flags=0x%08",
        "status_led_prepare_sleep",
        "power_manager_low_power_idle_ms",
        "power_manager_plugged_low_power_enabled",
        "power_manager_guard_runtime_power_hold_low",
        "PWR_HOLD/GPIO9 runtime guard reasserting low",
        "esp_pm_dump_locks",
        'strcmp(command, "PM")',
        'strcmp(command, "SHUTDOWN")',
    ],
    "components/status_led/include/status_led.h": [
        "STATUS_LED_KEY_FEEDBACK_DOUBLE",
        "status_led_notify_ec11_feedback",
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
        "s_recovery_power_blocker_active",
        "ble_hid_gap_set_recovery_power_blocker",
        "recovery: pairing power blocker",
        "s_recovery_pairing_window_timer",
        "esp_timer_start_once",
    ],
    "ports/esp32/ble_hid/ble_hid.c": [
        "power_manager_consume_usb_command",
        "POWER_MANAGER_BLOCKER_DIAG_EXPORT",
        "power_manager_set_ble_connected",
        "BLE_HID_USB_READ_LOW_POWER_TIMEOUT_MS 5000",
        "ble_hid_usb_command_is_passive_query",
        "ble_hid_usb_command_records_activity",
        "POWER:STATUS",
        "POWER:IDLE",
        "POWER:IDLE:DIAG",
        "POWER:IDLE_DIAG",
        "POWER:PM",
        "POWER:PM:LOCKS",
        "BOARD:STATUS",
        "BOARD:POWER",
        "LED:STATUS",
        "DEVICE:SETTINGS",
    ],
    "ports/esp32/voice_key_input/voice_key_input_esp32.c": [
        "VOICE_KEY_INPUT_IDLE_BACKUP_POLL_MS (20)",
        "VOICE_KEY_INPUT_LOW_POWER_IDLE_BACKUP_POLL_MS (20)",
        "VOICE_KEY_INPUT_DOUBLE_CLICK_MIN_GAP_MS (80)",
        "VOICE_KEY_INPUT_DOUBLE_CLICK_WINDOW_MS (200)",
        "VOICE_KEY_INPUT_RECOVERY_DOUBLE_CLICK_WINDOW_MS (200)",
        "VOICE_KEY_INPUT_LONG_PRESS_IGNORE_MS (800)",
        "VOICE_KEY_INPUT_HOLD_FEEDBACK_REFRESH_MS (300)",
        "voice_key_input_next_wait_ms",
        "voice_key_input_enable_light_sleep_wake",
        "GPIO_INTR_DISABLE",
        "GPIO_INTR_LOW_LEVEL",
        "gpio_wakeup_enable",
        "esp_sleep_enable_gpio_wakeup",
        "watchdog_platform_task_notify_take_low_power",
        "wake=active_low_gpio_wakeup+20ms_scan",
        "low_power_wake=active_low_gpio_wakeup+20ms_scan",
        "runtime_irq=anyedge_notify_edge_latch",
        "s_direct_gpio_isr_press_pending",
        "voice_key_input_take_direct_gpio_isr_press_pending",
    ],
    "ports/esp32/voice_key_input/CMakeLists.txt": [
        "esp_hw_support",
    ],
    "components/keyboard/keyboard.c": [
        "KEYBOARD_CUSTOM_IDLE_BACKUP_POLL_MS 20",
        "KEYBOARD_CUSTOM_LOW_POWER_IDLE_BACKUP_POLL_MS 20",
        "KEYBOARD_CUSTOM_DEBOUNCE_MS 20",
        "KEYBOARD_CUSTOM_DOUBLE_CLICK_WINDOW_MS 200",
        "KEYBOARD_EC11_EVENT_QUEUE_DEPTH 256",
        "KEYBOARD_EC11_LOW_POWER_IDLE_POLL_MS 20",
        "KEYBOARD_EC11_FEEDBACK_EDGE_REFRESH_MS 60",
        "KEYBOARD_EC11_DROP_LOG_INTERVAL_MS 1000",
        "s_ec11_isr_last_raw_state",
        "s_ec11_overflow_pending",
        "keyboard_ec11_refresh_feedback_for_delta",
        "direction_changed",
        "raw_state != KEYBOARD_EC11_DETENT_STATE",
        "was_low_power_idle",
        "keyboard_enable_active_low_light_sleep_wake",
        "GPIO_INTR_ANYEDGE",
        "GPIO_INTR_LOW_LEVEL",
        "GPIO_INTR_DISABLE",
        "gpio_wakeup_enable",
        "esp_sleep_enable_gpio_wakeup",
        "gpio_isr_handler_add",
        "watchdog_platform_task_notify_take_low_power",
        "watchdog_platform_task_notify_take(pdTRUE, wait_ms)",
        "wake=active_low_gpio_wakeup+20ms_scan",
        "low_power_wake=active_low_gpio_wakeup+20ms_scan",
    ],
    "components/keyboard/CMakeLists.txt": [
        "esp_hw_support",
    ],
    "components/voice_recording_control/voice_recording_control.c": [
        "POWER_MANAGER_BLOCKER_RECORDING",
        "POWER_MANAGER_BLOCKER_BLE_AUDIO",
        "POWER_MANAGER_BLOCKER_PAIRING",
        "recovery reset accepted; waiting for a fresh Windows/Type bond while BLE recovery pairing window is open",
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
    "components/power_manager/CMakeLists.txt": [
        "nvs_flash",
    ],
    "sdkconfig.defaults": [
        "CONFIG_POWER_MANAGER_BATTERY_CRITICAL_PERCENT=0",
        "CONFIG_BT_CTRL_MAIN_XTAL_PU_DURING_LIGHT_SLEEP=y",
        "CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION=y",
    ],
    "sdkconfig.defaults.esp32s3": [
        "CONFIG_PM_ENABLE=y",
        "CONFIG_PM_SLEEP_FUNC_IN_IRAM=y",
        "CONFIG_FREERTOS_USE_TICKLESS_IDLE=y",
        "# CONFIG_BT_CTRL_MODEM_SLEEP is not set",
        "CONFIG_BT_CTRL_MAIN_XTAL_PU_DURING_LIGHT_SLEEP=y",
        "CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION=y",
        "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y",
        "CONFIG_SPIRAM=y",
        "CONFIG_POWER_MANAGER_AUDIO_IDLE_MS=5000",
        "CONFIG_POWER_MANAGER_CONNECTED_IDLE_MS=60000",
        "CONFIG_POWER_MANAGER_DISCONNECTED_IDLE_MS=60000",
        "CONFIG_POWER_MANAGER_HARDWARE_SHUTDOWN_MS=600000",
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
        "PWR_HOLD/GPIO9",
        "v2_gpio9_power_latch_runtime_low_drive_high_for_hardware_shutdown",
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
        "2850mV=0%",
        "4150mV=100%",
        "2700mV",
        "default critical threshold `0%`",
        "forces hardware shutdown",
        "actively driven LOW during normal boot and runtime",
        "Active charging or charge-full status blocks this automatic low-battery shutdown",
        "BLE link churn is radio activity, not user activity",
        "button-wakeable and knob-wakeable",
        "20 ms low-power backup poll",
        "valid low-power rotation edge records activity",
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
        "esp_sleep_get_wakeup_cause",
        "esp_sleep_get_ext1_wakeup_status",
        "esp_sleep_enable_ext",
        "esp_sleep_enable_timer_wakeup",
        "esp_sleep_disable_wakeup_source",
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
        "v2_gpio11_power_latch_runtime_low_drive_high_for_hardware_shutdown",
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
            "main/main.c: app_main must drive PWR_HOLD/GPIO9 low before LED/BLE/diagnostic init"
        )

    power_manager = (REPO_ROOT / "components/power_manager/power_manager.c").read_text(encoding="utf-8")
    audio_capture = (REPO_ROOT / "ports/esp32/audio_capture/audio_capture_esp32.c").read_text(encoding="utf-8")
    if not re.search(r"POWER_MANAGER_CHARGER_STATUS_EXTERNAL_HOLD_MS\s+1000U", power_manager):
        failures.append(
            "components/power_manager/power_manager.c: charger-status retention should be short enough for fast unplug feedback"
        )
    if not re.search(
        r"i2s_channel_disable\(s_i2s_rx_handle\);[\s\S]{0,240}"
        r"ret\s*==\s*ESP_ERR_INVALID_STATE[\s\S]{0,120}"
        r"ret\s*=\s*ESP_OK;[\s\S]{0,180}"
        r"s_i2s_low_power_disabled\s*=\s*true;",
        audio_capture,
    ):
        failures.append(
            "ports/esp32/audio_capture/audio_capture_esp32.c: idle power-save enable must treat already-disabled I2S as idempotent success"
        )
    if not re.search(
        r"i2s_channel_enable\(s_i2s_rx_handle\);[\s\S]{0,240}"
        r"ret\s*==\s*ESP_ERR_INVALID_STATE[\s\S]{0,120}"
        r"ret\s*=\s*ESP_OK;[\s\S]{0,180}"
        r"s_i2s_low_power_disabled\s*=\s*false;",
        audio_capture,
    ):
        failures.append(
            "ports/esp32/audio_capture/audio_capture_esp32.c: idle power-save disable must treat already-enabled I2S as idempotent success"
        )
    if re.search(
        r"power_manager_charger_status_external_locked[\s\S]*usb_power_present\s*\|\|\s*raw_charging",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: USB SOF must not refresh charger-status retention after unplug"
        )
    if not re.search(
        r"power_manager_load_persisted_shutdown_trace[\s\S]*"
        r"POWER_MANAGER_NVS_SHUTDOWN_MAGIC_KEY[\s\S]*"
        r"POWER_MANAGER_NVS_SHUTDOWN_REASON_KEY[\s\S]*"
        r"power_manager_store_shutdown_trace_locked",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: boot must load the persisted shutdown trace from NVS"
        )
    if not re.search(
        r"power_manager_init[\s\S]*"
        r"power_manager_load_persisted_shutdown_trace\(\)[\s\S]*"
        r"DIAG_POWER_WAKE",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: init must load persisted shutdown trace before wake diagnostics"
        )
    if not re.search(
        r"power_manager_persist_shutdown_trace[\s\S]*"
        r"POWER_MANAGER_NVS_SHUTDOWN_REASON_KEY[\s\S]*"
        r"POWER_MANAGER_NVS_SHUTDOWN_IDLE_MS_KEY[\s\S]*"
        r"POWER_MANAGER_NVS_SHUTDOWN_POWER_FLAGS_KEY[\s\S]*"
        r"nvs_commit",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: shutdown trace must persist reason, idle, blockers, battery, and power flags"
        )
    if not re.search(
        r"power_manager_enter_hardware_shutdown[\s\S]*"
        r"power_manager_store_shutdown_trace_locked[\s\S]*"
        r"power_manager_persist_shutdown_trace\([\s\S]*"
        r"board_set_power_hold_enabled\(false\)",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: shutdown trace must be persisted before PWR_HOLD/GPIO11 drive-high"
        )
    if not re.search(
        r"power_manager_get_snapshot[\s\S]*"
        r"snapshot->last_shutdown_persisted\s*=\s*s_last_shutdown_persisted[\s\S]*"
        r"snapshot->last_shutdown_power_flags\s*=\s*s_last_shutdown_power_flags",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: POWER:STATUS snapshot must expose persisted shutdown trace details"
        )
    if not re.search(
        r"power_manager_apply_charge_state_filter_locked[\s\S]*"
        r"raw_full_external\s*=[\s\S]*"
        r"power_manager_charge_full_battery_allowed[\s\S]*"
        r"power_manager_charger_status_external_locked[\s\S]*"
        r"source->charge_power_present\s*=\s*source->charge_power_present\s*\|\|\s*charger_status_external[\s\S]*"
        r"raw_full\s*&&[\s\S]*"
        r"!raw_charging\s*&&[\s\S]*"
        r"power_manager_charge_full_battery_allowed[\s\S]*"
        r"POWER_MANAGER_CHARGE_FULL_DEBOUNCE_MS[\s\S]*"
        r"source->charge_full\s*=\s*source->charge_power_present\s*&&\s*s_charge_full_latched[\s\S]*"
        r"source->external_power_present\s*=[\s\S]*source->usb_power_present\s*\|\|\s*source->charging\s*\|\|\s*source->charge_full\s*\|\|\s*charger_status_external",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: charge-full must be debounce/near-full guarded while allowing USB SOF or BAT_STD/BAT_CHG to prove external power with GPIO7 disabled"
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
        r"POWER_MANAGER_LOW_POWER_EXTERNAL_EVALUATE_INTERVAL_MS\s+1000U[\s\S]*"
        r"POWER_MANAGER_LOW_POWER_EVALUATE_INTERVAL_MS\s+60000U[\s\S]*"
        r"power_manager_task[\s\S]*"
        r"state\s*==\s*POWER_MANAGER_STATE_ACTIVE[\s\S]*"
        r"watchdog_platform_task_notify_take\([\s\S]*CONFIG_POWER_MANAGER_EVALUATE_INTERVAL_MS[\s\S]*"
        r"external_power_present[\s\S]*"
        r"watchdog_platform_task_notify_take_low_power\([\s\S]*POWER_MANAGER_LOW_POWER_EXTERNAL_EVALUATE_INTERVAL_MS[\s\S]*"
        r"watchdog_platform_task_notify_take_low_power\([\s\S]*POWER_MANAGER_LOW_POWER_EVALUATE_INTERVAL_MS",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: stable idle must use a long low-power evaluate wait instead of 2s polling"
        )
    wake_checks = {
        "isr_one_shot_notify": (
            r"power_manager_disable_power_input_interrupt_from_isr[\s\S]*"
            r"gpio_intr_disable[\s\S]*"
            r"power_manager_power_input_wake_from_isr[\s\S]*"
            r"s_power_input_irq_armed\s*=\s*false[\s\S]*"
            r"vTaskNotifyGiveFromISR"
        ),
        "pin_low_level_wake": (
            r"power_manager_configure_power_input_wake_pin[\s\S]*"
            r"GPIO_INTR_DISABLE[\s\S]*"
            r"gpio_isr_handler_add\(gpio,\s*power_manager_power_input_wake_from_isr[\s\S]*"
            r"gpio_wakeup_enable\(gpio,\s*GPIO_INTR_LOW_LEVEL\)[\s\S]*"
            r"gpio_intr_disable\(gpio\)"
        ),
        "gpio_wakeup_source": (
            r"power_manager_configure_power_input_wake[\s\S]*"
            r"board_get_v2_power_input_snapshot[\s\S]*"
            r"BOARD_PINS_BAT_CHG_IO[\s\S]*"
            r"BOARD_PINS_BAT_STD_IO[\s\S]*"
            r"esp_sleep_enable_gpio_wakeup\(\)"
        ),
        "irq_arm_helper": (
            r"power_manager_set_power_input_interrupt[\s\S]*"
            r"gpio_intr_enable[\s\S]*"
            r"gpio_intr_disable[\s\S]*"
            r"power_manager_set_power_input_irq_armed"
        ),
        "battery_idle_arm_policy": (
            r"power_manager_update_power_input_irq_arm[\s\S]*"
            r"!source->external_power_present[\s\S]*"
            r"POWER_MANAGER_STATE_CONNECTED_IDLE[\s\S]*"
            r"POWER_MANAGER_STATE_DISCONNECTED_IDLE[\s\S]*"
            r"power_manager_set_power_input_irq_armed\(should_arm\)"
        ),
        "evaluate_wiring": (
            r"power_manager_evaluate[\s\S]*"
            r"power_manager_update_power_input_irq_arm\(next,\s*&power_source\)"
        ),
        "start_wiring": (
            r"power_manager_start[\s\S]*"
            r"power_manager_configure_power_input_wake\(\)"
        ),
    }
    missing_wake_checks = [
        name for name, pattern in wake_checks.items() if not re.search(pattern, power_manager)
    ]
    if missing_wake_checks:
        failures.append(
            "components/power_manager/power_manager.c: plugged attach must notify power manager from BAT_CHG/BAT_STD GPIO light-sleep wake"
            f" (missing: {', '.join(missing_wake_checks)})"
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
            "components/power_manager/power_manager.c: low-battery hardware shutdown must reject ADC startup transients"
        )
    if not re.search(
        r"power_manager_refresh_ble_connection_locked[\s\S]*"
        r"ble_hid_gap_is_connected\(\)[\s\S]*"
        r"ble_audio_stream_is_type_link_ready\(\)[\s\S]*"
        r"power_manager_apply_ble_connection_change_locked\(connected,\s*now_ms\)",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: BLE refresh must use HID/GAP or Type-link readiness through the shared idle-preserving connection-change helper"
        )
    if not re.search(
        r"void\s+power_manager_set_ble_connected[\s\S]*"
        r"effective_connected[\s\S]*"
        r"ble_audio_stream_is_type_link_ready\(\)[\s\S]*"
        r"power_manager_apply_ble_connection_change_locked\(effective_connected,\s*now_ms\)",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: BLE callbacks must use effective HID/GAP or Type-link readiness through the shared idle-preserving connection-change helper"
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
        r"power_manager_apply_ble_connection_change_locked\(effective_connected,\s*now_ms\)[\s\S]*"
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
            "components/power_manager/power_manager.c: connected idle must enter status LED low-power rendering before sleeping audio"
        )
    if not re.search(
        r"case\s+POWER_MANAGER_STATE_DISCONNECTED_IDLE:[\s\S]{0,80}"
        r"case\s+POWER_MANAGER_STATE_HARDWARE_SHUTDOWN:[\s\S]{0,220}"
        r"status_led_set_low_power_disabled\(true\)[\s\S]{0,240}"
        r"power_manager_set_audio_idle_power_save\(true\)",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: disconnected idle and hardware shutdown must enter status LED low-power rendering"
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
    if "POWER_MANAGER_POWER_REMOVAL_WAIT_MS 10000U" not in power_manager:
        failures.append(
            "components/power_manager/power_manager.c: shutdown power-removal observation window must be explicit and bounded"
        )
    if not re.search(r"#define\s+POWER_MANAGER_SHUTDOWN_LED_CONFIRM_MS\s+1200U\b", power_manager):
        failures.append(
            "components/power_manager/power_manager.c: hardware shutdown must hold the final PWR-only LED confirmation for 1200 ms"
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
    if "POWER_MANAGER_AUTO_SHUTDOWN_LED_CONFIRM_MS 1000U" not in power_manager:
        failures.append(
            "components/power_manager/power_manager.c: automatic shutdown PWR-only LED cue must be short and bounded"
        )
    if "POWER_MANAGER_AUTO_SHUTDOWN_LED_LOCK_WAIT_MS 20U" not in power_manager:
        failures.append(
            "components/power_manager/power_manager.c: automatic shutdown LED cue must use a bounded LED lock wait"
        )
    if "power_manager_shutdown_failure_retry_active_locked" not in power_manager:
        failures.append(
            "components/power_manager/power_manager.c: target state must honor shutdown failure retry cooldown"
        )
    if not re.search(
        r"power_manager_shutdown_reason_uses_graceful_prepare[\s\S]*"
        r"reason\s*==\s*POWER_MANAGER_SHUTDOWN_REASON_MANUAL_COMMAND",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: only manual shutdown may use graceful LED/BLE prepare before PWR_HOLD"
        )
    if not re.search(
        r"power_manager_show_automatic_shutdown_led_cue[\s\S]*"
        r"status_led_try_notify_shutdown_confirm\([\s\S]*"
        r"true,[\s\S]*"
        r"automatic_hardware_shutdown_confirmed[\s\S]*"
        r"POWER_MANAGER_AUTO_SHUTDOWN_LED_LOCK_WAIT_MS[\s\S]*"
        r"vTaskDelay\(pdMS_TO_TICKS\(POWER_MANAGER_AUTO_SHUTDOWN_LED_CONFIRM_MS\)\)[\s\S]*"
        r"status_led_try_hold_shutdown_all_off\([\s\S]*"
        r"automatic_hardware_shutdown_led_off_hold[\s\S]*"
        r"POWER_MANAGER_AUTO_SHUTDOWN_LED_LOCK_WAIT_MS",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: automatic shutdown must use a short PWR-only cue, then hold all LEDs off before PWR_HOLD"
        )
    if not re.search(
        r"if\s*\(\s*power_manager_shutdown_reason_uses_graceful_prepare\(reason\)\s*\)[\s\S]*"
        r"status_led_notify_shutdown_confirm[\s\S]*"
        r"ble_hid_battery_force_refresh[\s\S]*"
        r"status_led_prepare_sleep[\s\S]*"
        r"else\s*\{[\s\S]*"
        r"automatic hardware shutdown bypassing graceful prepare before PWR_HOLD[\s\S]*"
        r"power_manager_show_automatic_shutdown_led_cue\(reason,\s*final_idle_ms\)[\s\S]*"
        r"watchdog_platform_enter_shutdown_critical\(\"hardware_shutdown_pwr_hold\"\)",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: automatic shutdown must bypass graceful prepare, show only a bounded LED cue, and reach PWR_HOLD setup immediately"
        )
    if not re.search(
        r"power_manager_restore_after_shutdown_failure[\s\S]*"
        r"status_led_cancel_shutdown_confirm\(\"hardware_shutdown_failed\"\)[\s\S]*"
        r"status_led_set_low_power_disabled\(false\)",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: failed automatic shutdown must cancel shutdown LED confirmation and release all-off hold"
        )
    if not re.search(
        r"watchdog_platform_enter_shutdown_critical\(\"hardware_shutdown_pwr_hold\"\)[\s\S]*"
        r"POWER_MANAGER_POWER_HOLD_ACTION_SHUTDOWN_DRIVE_HIGH[\s\S]*"
        r"board_set_power_hold_enabled\(false\)",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: hardware shutdown must extend WDT and log drive-high attempt before waiting on PWR_HOLD"
        )
    if not re.search(
        r"board_set_power_hold_enabled\(false\)[\s\S]*"
        r"hold_ret\s*!=\s*ESP_OK[\s\S]*"
        r"watchdog_platform_exit_shutdown_critical\(\)[\s\S]*"
        r"power_manager_restore_after_shutdown_failure\(reason,\s*final_idle_ms,\s*hold_ret\)",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: PWR_HOLD drive-high/readback failure must restore WDT and use the shutdown restore path"
        )
    if not re.search(
        r"watchdog_platform_delay_ms\(POWER_MANAGER_POWER_REMOVAL_WAIT_MS\)[\s\S]*"
        r"hardware shutdown did not remove power[\s\S]*"
        r"watchdog_platform_exit_shutdown_critical\(\)[\s\S]*"
        r"power_manager_restore_after_shutdown_failure\(reason,\s*final_idle_ms,\s*ESP_FAIL\)",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: powered-after-shutdown fallback must restore WDT and runtime low after the bounded wait"
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
        r"case\s+POWER_MANAGER_STATE_DISCONNECTED_IDLE:[\s\S]*"
        r"s_external_power_present[\s\S]*"
        r"ble_hid_gap_set_low_power_advertising\(false\)[\s\S]*"
        r"ble_hid_gap_stop_advertising_for_key_wake",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: externally powered disconnected idle must keep connectable BLE advertising before battery key-wake-only fallback"
        )
    if not re.search(
        r"power_manager_get_snapshot[\s\S]*"
        r"low_power_idle_threshold_ms\s*=\s*power_manager_low_power_idle_ms\(\)[\s\S]*"
        r"connected_idle_threshold_ms\s*=\s*snapshot->low_power_idle_threshold_ms[\s\S]*"
        r"disconnected_idle_threshold_ms\s*=\s*snapshot->low_power_idle_threshold_ms[\s\S]*"
        r"plugged_low_power_enabled\s*=\s*power_manager_plugged_low_power_enabled\(\)[\s\S]*"
        r"low_power_idle_allowed[\s\S]*"
        r"power_input_wake_configured[\s\S]*"
        r"power_input_irq_armed",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: POWER:STATUS must report the effective low-power timeout, plugged low-power allowance, and power-input wake diagnostics"
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
        r"power_manager_print_idle_diag\(void\)[\s\S]*"
        r"~POWER:IDLE_DIAG[\s\S]*"
        r"reason=%s[\s\S]*"
        r"low_power_wait_valid=%u[\s\S]*"
        r"low_power_wait_ms=%[\s\S]*"
        r"audio_idle_ready=%u[\s\S]*"
        r"awake_blocker_names=%s",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: POWER:IDLE_DIAG must summarize idle blockers, wait time, audio idle, and next action"
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
    if (
        "BLE_HID_GAP_RANDOM_IDENTITY_KEY" not in ble_gap
        or "ble_hid_gap_rotate_native_recovery_identity" not in ble_gap
        or "ble_hid_gap_restore_random_identity_from_nvs" not in ble_gap
        or "ble_hs_id_gen_rnd(0, &addr)" not in ble_gap
        or "s_own_addr_type = BLE_OWN_ADDR_RANDOM;" not in ble_gap
    ):
        failures.append(
            "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c: non-Type Windows-native recovery must rotate and persist a BLE identity while Type-controlled recovery keeps the current identity"
        )
    if not re.search(
        r"static\s+void\s+ble_hid_gap_open_recovery_pairing_window[\s\S]*"
        r"s_recovery_pairing_window_active\s*=\s*true[\s\S]*"
        r"ble_hid_gap_set_recovery_power_blocker\(true,\s*\"pairing_window_open\"\)[\s\S]*"
        r"ble_hid_gap_arm_recovery_pairing_window_timer\(\)",
        ble_gap,
    ):
        failures.append(
            "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c: recovery pairing window must hold pairing/reconnect blockers and arm expiry before returning to idle logic"
        )
    if not re.search(
        r"static\s+void\s+ble_hid_gap_close_recovery_pairing_window[\s\S]*"
        r"s_recovery_pairing_window_active\s*=\s*false[\s\S]*"
        r"esp_timer_stop\(s_recovery_pairing_window_timer\)[\s\S]*"
        r"ble_hid_gap_set_recovery_power_blocker\(false,\s*reason\)",
        ble_gap,
    ):
        failures.append(
            "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c: recovery pairing window close must release pairing/reconnect blockers and stop the expiry timer"
        )
    if not re.search(
        r"static\s+void\s+ble_hid_gap_recovery_pairing_window_timer_cb[\s\S]*"
        r"ble_hid_gap_close_recovery_pairing_window\(\"pairing_window_expired\"\)",
        ble_gap,
    ):
        failures.append(
            "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c: recovery pairing window must have a timer-backed expiry path so pairing blockers cannot stick forever"
        )
    voice_recovery = (REPO_ROOT / "components/voice_recording_control/voice_recording_control.c").read_text(encoding="utf-8")
    if not re.search(
        r"static\s+void\s+voice_recording_control_recovery[\s\S]*"
        r"bool\s+pairing_window_open\s*=\s*ble_hid_gap_is_recovery_pairing_window_open\(\)[\s\S]*"
        r"recovery reset accepted; waiting for a fresh Windows/Type bond while BLE recovery pairing window is open[\s\S]*"
        r"else\s*\{[\s\S]*"
        r"POWER_MANAGER_BLOCKER_PAIRING\s*\|\s*POWER_MANAGER_BLOCKER_RECONNECT[\s\S]*"
        r"false[\s\S]*"
        r"voice_recording_control_log_device_status\(\"pairing\",\s*\"recovery_pairing_window_open\"\)[\s\S]*"
        r"voice_recording_control_log_device_status\(\"ready\",\s*\"recovery_complete_no_pairing_window\"\)",
        voice_recovery,
    ):
        failures.append(
            "components/voice_recording_control/voice_recording_control.c: recovery completion must keep pairing/reconnect blockers and report pairing, not ready, while the BLE recovery window remains open"
        )
    if "recovery_complete_pair_again" in voice_recovery:
        failures.append(
            "components/voice_recording_control/voice_recording_control.c: recovery must not report ready/recovery_complete_pair_again before the fresh Windows/Type bond is complete"
        )
    if not re.search(
        r"static\s+void\s+ble_hid_gap_keep_recovery_adv_connectable[\s\S]{0,760}"
        r"s_low_power_advertising\s*=\s*false[\s\S]{0,180}"
        r"s_key_wake_only_advertising\s*=\s*false[\s\S]{0,180}"
        r"s_directed_adv_pending\s*=\s*false",
        ble_gap,
    ):
        failures.append(
            "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c: recovery pairing window must force connectable undirected advertising over low-power/key-wake/direct modes"
        )
    if not re.search(
        r"esp_hid_ble_gap_adv_start[\s\S]{0,900}"
        r"ble_hid_gap_keep_recovery_adv_connectable\(\"advertising start\"\)[\s\S]{0,320}"
        r"if\s*\(\s*s_key_wake_only_advertising\s*\)",
        ble_gap,
    ):
        failures.append(
            "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c: advertising start must honor recovery pairing before key-wake suppression"
        )
    if not re.search(
        r"ble_hid_gap_set_low_power_advertising[\s\S]{0,320}"
        r"enabled\s*&&\s*ble_hid_gap_recovery_pairing_needs_connectable_adv\(\)[\s\S]{0,260}"
        r"return\s+ESP_OK",
        ble_gap,
    ):
        failures.append(
            "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c: low-power advertising requests must not override an active recovery pairing window"
        )
    if not re.search(
        r"ble_hid_gap_stop_advertising_for_key_wake[\s\S]{0,260}"
        r"ble_hid_gap_recovery_pairing_needs_connectable_adv\(\)[\s\S]{0,460}"
        r"ble_hid_gap_start_advertising\(\)",
        ble_gap,
    ):
        failures.append(
            "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c: key-wake-only advertising stop must restart connectable advertising during recovery pairing"
        )
    if not re.search(
        r"ble_hid_gap_set_connection_state[\s\S]{0,260}s_last_conn_param_mode\s*=\s*0",
        ble_gap,
    ) or not re.search(
        r"s_last_conn_param_mode\s*==\s*\(uint32_t\)mode",
        ble_gap,
    ):
        failures.append(
            "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c: connection parameter mode de-duplication must reset on connection state changes"
        )
    for label, pattern in (
        (
            "advertising start",
            r"esp_hid_ble_gap_adv_start[\s\S]*s_shutdown_quiesce[\s\S]*return\s+ESP_OK",
        ),
        (
            "disconnect callback",
            r"ble_hid_gap_handle_disconnect[\s\S]*s_shutdown_quiesce[\s\S]*suppressing advertising restart after disconnect[\s\S]*return",
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
        r"#define\s+BLE_HID_USB_READ_LOW_POWER_TIMEOUT_MS\s+5000\b",
        ble_hid,
    ):
        failures.append(
            "ports/esp32/ble_hid/ble_hid.c: low-power USB read timeout must stay at 5000 ms to avoid frequent idle wakeups"
        )
    if not re.search(
        r"#define\s+BLE_HID_USAGE_QUEUE_LENGTH\s+32\b",
        ble_hid,
    ):
        failures.append(
            "ports/esp32/ble_hid/ble_hid.c: HID usage queue must stay deep enough for fast EC11 rotation bursts without starving local LED feedback"
        )
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
        r"POWER:STATUS[\s\S]*POWER:IDLE[\s\S]*POWER:PM[\s\S]*BOARD:STATUS[\s\S]*BOARD:POWER[\s\S]*BOARD:POWER:FORCE[\s\S]*LED:STATUS[\s\S]*DEVICE:SETTINGS",
        ble_hid,
    ):
        failures.append(
            "ports/esp32/ble_hid/ble_hid.c: passive board, power/PM, LED, and device queries must be enumerated"
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

    status_led_header = (
        REPO_ROOT / "components/status_led/include/status_led.h"
    ).read_text(encoding="utf-8")
    status_led = (REPO_ROOT / "components/status_led/status_led.c").read_text(
        encoding="utf-8"
    )
    status_led_backend = (
        REPO_ROOT / "components/status_led/status_led_strip_backend.c"
    ).read_text(encoding="utf-8")
    if "STATUS_LED_LOW_POWER_STATUS_RESYNC_MS" in status_led:
        failures.append(
            "components/status_led/status_led.c: low-power idle must not keep retransmitting the status rail after the final PWR-only frame"
        )
    if "status_led_low_power_status_rewrite_needed" in status_led:
        failures.append(
            "components/status_led/status_led.c: low-power status rewrite helper must be removed; idle should settle then suspend all LED transports"
        )
    if (
        "const bool preserve_repair_cue =" not in status_led
        or "disabled && status_led_ble_repair_cue_active_locked(now_ms)" not in status_led
        or "const bool next_low_power_disabled = disabled && !preserve_repair_cue;" not in status_led
        or "if (next_low_power_disabled) {\n            status_led_force_transition_clear_locked(STATUS_LED_TRANSITION_CLEAR_NON_KEY_ACCENTS);" not in status_led
    ):
        failures.append(
            "components/status_led/status_led.c: entering low-power idle must schedule a scoped non-KEY accent clear frame, while active repair cue must defer the low-power LED cutoff"
        )
    if not re.search(
        r"status_led_idle_transport_release_pending[\s\S]{0,260}"
        r"index\s*=\s*0",
        status_led,
    ):
        failures.append(
            "components/status_led/status_led.c: low-power transport release pending must include the status rail"
        )
    quiet_suspend = re.search(
        r"static\s+void\s+status_led_suspend_quiet_idle_transports[\s\S]*?"
        r"static\s+void\s+status_led_transmit_changed_frame",
        status_led,
    )
    if quiet_suspend is None:
        failures.append(
            "components/status_led/status_led.c: missing low-power quiet transport suspend helper"
        )
    elif "STATUS_LED_STRIP_STATUS" in quiet_suspend.group(0):
        failures.append(
            "components/status_led/status_led.c: low-power quiet suspend must not skip the status rail"
        )
    if (
        "rmt_idle_drive=active_dma_low_power_all_zone_non_dma_final_frame_then_release_gpio_low" not in status_led
        or "low_power_transport_suspend_ms=%u" not in status_led
        or "low_power_status_tx=non_dma_clear_and_final_frame" not in status_led
        or "low_power_all_zone_tx=non_dma_clear_and_final_frame" not in status_led
        or "shutdown_final_status_tx=non_dma_pwr_only_latch" not in status_led
        or "shutdown_final_all_zone_tx=non_dma_pwr_only_latch_or_all_off" not in status_led
        or "low_power_final_latch_writes=%u" not in status_led
        or not re.search(r"#define\s+STATUS_LED_RMT_IDLE_RELEASE_MS\s+0U\b", status_led)
    ):
        failures.append(
            "components/status_led/status_led.c: LED contract must report active-DMA plus non-DMA low-power final-frame suspend policy"
        )
    if not re.search(r"#define\s+STATUS_LED_LOW_POWER_PWR_PERCENT\s+12U\b", status_led):
        failures.append(
            "components/status_led/status_led.c: low-power single-channel PWR brightness must be 12 percent for visible idle indication"
        )
    if not re.search(r"#define\s+STATUS_LED_LOW_POWER_PWR_WHITE_PERCENT\s+4U\b", status_led):
        failures.append(
            "components/status_led/status_led.c: low-power white PWR brightness must use 4 percent per RGB channel to match the 12 percent total target"
        )
    if not re.search(r"#define\s+STATUS_LED_LOW_POWER_FINAL_LATCH_WRITES\s+3U\b", status_led):
        failures.append(
            "components/status_led/status_led.c: low-power final PWR-only latch must be repeated three times before transport suspend"
        )
    if (
        not re.search(r"#define\s+STATUS_LED_LOW_POWER_STATUS_RETRY_WRITES\s+3U\b", status_led)
        or "status_writes < STATUS_LED_LOW_POWER_FINAL_LATCH_WRITES" not in status_led
        or "status_writes + STATUS_LED_LOW_POWER_STATUS_RETRY_WRITES" not in status_led
        or "status_led_strip_backend_suspend(s_strips[STATUS_LED_STRIP_STATUS].backend)" not in status_led
        or "status_led_force_all_off(true)" not in status_led
        or "status_led_transmit_changed_frame(&frame, STATUS_LED_STRIP_MASK_ALL, force_non_dma)" not in status_led
        or "status_led_force_all_off();" in status_led
    ):
        failures.append(
            "components/status_led/status_led.c: low-power/prepare-sleep LED writes must retry non-DMA final frames and suspend/recover after a failed status-strip latch"
        )
    if (
        not re.search(r"#define\s+STATUS_LED_RMT_WAIT_MS\s+[1-9][0-9]{2}\b", status_led_backend)
        or "status_led_strip_backend_suspend(backend)" not in status_led_backend
    ):
        failures.append(
            "components/status_led/status_led_strip_backend.c: status LED RMT wait must stay long enough for low-power latch writes and recover by suspending/driving the strip idle-low after tx failures"
        )
    if not re.search(r"#define\s+STATUS_LED_SHUTDOWN_FINAL_CONFIRM_MS\s+1400U\b", status_led):
        failures.append(
            "components/status_led/status_led.c: final shutdown PWR-only confirmation must outlive the 1200 ms power-manager wait so normal BLE/PWR status cannot flash before prepare_sleep"
        )
    if not re.search(
        r"status_led_key_feedback_color_locked[\s\S]{0,120}status_led_rgb\(160,\s*0,\s*255\)",
        status_led,
    ):
        failures.append(
            "components/status_led/status_led.c: confirmed key gesture feedback must use the shared purple confirmation color"
        )
    if not re.search(r"#define\s+STATUS_LED_KEY_GESTURE_FEEDBACK_MS\s+1800U\b", status_led):
        failures.append(
            "components/status_led/status_led.c: confirmed purple key gesture window must be 1800 ms so long/double confirmation stays visible"
        )
    if not re.search(r"#define\s+STATUS_LED_KEY_FLASH_ON_MS\s+420U\b", status_led):
        failures.append(
            "components/status_led/status_led.c: confirmed purple single/double flash on-time must be 420 ms for human-visible confirmation"
        )
    if not re.search(r"#define\s+STATUS_LED_KEY_FLASH_GAP_MS\s+220U\b", status_led):
        failures.append(
            "components/status_led/status_led.c: confirmed purple double-flash gap must be 220 ms"
        )
    if not re.search(r"#define\s+STATUS_LED_KEY_GESTURE_PERCENT\s+85U\b", status_led):
        failures.append(
            "components/status_led/status_led.c: confirmed purple key gesture brightness must be 85 percent"
        )
    if not re.search(r"#define\s+STATUS_LED_EC11_FEEDBACK_MS\s+1400U\b", status_led):
        failures.append(
            "components/status_led/status_led.c: EC11 press/rotation feedback window must remain 1400 ms"
        )
    if "STATUS_LED_EC11_FEEDBACK_DOUBLE" in status_led_header or "STATUS_LED_EC11_FEEDBACK_DOUBLE" in status_led:
        failures.append(
            "components/status_led: EC11 double-click recovery must use the BLE re-pair renderer, not a key-style EC11 feedback enum"
        )
    if not re.search(
        r"STATUS_LED_KEY_FEEDBACK_DOUBLE[\s\S]{0,700}"
        r"STATUS_LED_KEY_FLASH_ON_MS\s*\*\s*2U\s*\+\s*STATUS_LED_KEY_FLASH_GAP_MS\s*\+\s*"
        r"STATUS_LED_KEY_FADE_MS[\s\S]{0,360}"
        r"status_led_decay_percent",
        status_led,
    ) or not re.search(
        r"STATUS_LED_KEY_FEEDBACK_SINGLE[\s\S]{0,460}"
        r"elapsed\s*<\s*\(STATUS_LED_KEY_FLASH_ON_MS\s*\+\s*STATUS_LED_KEY_FADE_MS\)[\s\S]{0,320}"
        r"status_led_decay_percent",
        status_led,
    ) or not re.search(
        r"if\s*\(\s*color\.r\s*==\s*0U\s*&&\s*color\.g\s*==\s*0U\s*&&\s*color\.b\s*==\s*0U\s*\)\s*\{\s*return\s+true\s*;",
        status_led,
    ):
        failures.append(
            "components/status_led/status_led.c: key gesture feedback must end with a STATUS_LED_KEY_FADE_MS decay tail mirroring the EC11 press, instead of a hard one-frame cut to black"
        )
    if "pwr_only_final_latch || force_clear_tx" in status_led or "bool force_non_dma = pwr_only_final_latch;" not in status_led:
        failures.append(
            "components/status_led/status_led.c: interactive transition clears must keep SPI EC11/KEY strips on DMA; only low-power/final latch may force non-DMA"
        )
    if not re.search(
        r"physical_feedback_active\s*&&\s*!gesture_active[\s\S]{0,260}status_led_rgb\(255,\s*255,\s*255\)",
        status_led,
    ):
        failures.append(
            "components/status_led/status_led.c: physical key press/release feedback must render white; purple is reserved for confirmed single/double/long gestures"
        )
    if not re.search(
        r"status_led_rgb\(255,\s*255,\s*255\)[\s\S]{0,320}"
        r"s_state\.ec11_feedback\s*==\s*STATUS_LED_EC11_FEEDBACK_PRESS",
        status_led,
    ):
        failures.append(
            "components/status_led/status_led.c: EC11 press/rotation feedback must remain white; purple is reserved for confirmed gestures"
        )
    ec11_render_body = re.search(
        r"static\s+void\s+status_led_render_ec11_locked[\s\S]*?"
        r"\n\}\n\nstatic\s+void\s+status_led_render_key_active_work_locked",
        status_led,
    )
    ec11_render_text = ec11_render_body.group(0) if ec11_render_body is not None else ""
    if (
        not re.search(
            r"static\s+bool\s+status_led_ec11_feedback_is_rotation_locked\(void\)[\s\S]{0,260}"
            r"STATUS_LED_EC11_FEEDBACK_ROTATE_CW[\s\S]{0,160}"
            r"STATUS_LED_EC11_FEEDBACK_ROTATE_CCW",
            status_led,
        )
        or not re.search(
            r"static\s+bool\s+status_led_render_ec11_rotation_feedback_locked[\s\S]{0,420}"
            r"status_led_ec11_feedback_is_rotation_locked\(\)[\s\S]{0,260}"
            r"status_led_render_ec11_feedback_locked\(frame,\s*now_ms\)",
            status_led,
        )
        or ec11_render_body is None
        or "if (status_led_render_ec11_rotation_feedback_locked(frame, now_ms))" not in ec11_render_text
        or ec11_render_text.count("status_led_render_ec11_feedback_locked(frame, now_ms);") != 1
        or ec11_render_text.find("if (rec_percent > 0U)") > ec11_render_text.find("if (status_led_render_ec11_rotation_feedback_locked(frame, now_ms))")
        or re.search(
            r"if\s*\(\s*s_state\.processing_active\s*&&\s*rec_percent\s*>\s*0U\s*\)\s*\{(?:(?!return;)[\s\S]){0,220}"
            r"status_led_render_ec11_rotation_feedback_locked",
            ec11_render_text,
        )
        or re.search(
            r"if\s*\(\s*rec_percent\s*>\s*0U\s*\)\s*\{(?:(?!return;)[\s\S]){0,180}"
            r"status_led_render_ec11_rotation_feedback_locked",
            ec11_render_text,
        )
    ):
        failures.append(
            "components/status_led/status_led.c: EC11 white feedback must stay idle/non-recording only; recording and re-pair effects must own the EC11 ring before rotation feedback can render"
        )
    key_event_body = re.search(
        r"void\s+status_led_notify_key_event[\s\S]*?"
        r"\n\}\n\nvoid\s+status_led_notify_key_feedback",
        status_led,
    )
    key_feedback_body = re.search(
        r"void\s+status_led_notify_key_feedback[\s\S]*?"
        r"\n\}\n\nstatic\s+bool\s+status_led_ec11_press_feedback_should_yield_locked",
        status_led,
    )
    ec11_press_yield_body = re.search(
        r"static\s+bool\s+status_led_ec11_press_feedback_should_yield_locked[\s\S]*?"
        r"\n\}\n\nstatic\s+void\s+status_led_apply_ec11_feedback",
        status_led,
    )
    ec11_feedback_body = re.search(
        r"static\s+void\s+status_led_apply_ec11_feedback[\s\S]*?"
        r"\n\}\n\nvoid\s+status_led_notify_ec11_feedback",
        status_led,
    )
    recording_body = re.search(
        r"void\s+status_led_set_recording[\s\S]*?"
        r"\n\}\n\nvoid\s+status_led_set_recording_level",
        status_led,
    )
    processing_body = re.search(
        r"void\s+status_led_set_processing[\s\S]*?"
        r"\n\}\n\nvoid\s+status_led_set_ota_active",
        status_led,
    )
    interactive_resume_body = re.search(
        r"static\s+void\s+status_led_resume_interactive_output_locked[\s\S]*?"
        r"\n\}\n\nstatic\s+void\s+status_led_clear_key_feedback_locked",
        status_led,
    )
    if (
        interactive_resume_body is None
        or "s_state.low_power_disabled = false;" not in interactive_resume_body.group(0)
        or "s_state.output_disabled = false;" not in interactive_resume_body.group(0)
        or "status_led_force_transition_clear_locked(STATUS_LED_TRANSITION_CLEAR_NON_KEY_ACCENTS);" not in interactive_resume_body.group(0)
        or key_event_body is None
        or "status_led_resume_interactive_output_locked();" not in key_event_body.group(0)
        or key_feedback_body is None
        or "status_led_resume_interactive_output_locked();" not in key_feedback_body.group(0)
        or ec11_feedback_body is None
        or "status_led_resume_interactive_output_locked();" not in ec11_feedback_body.group(0)
    ):
        failures.append(
            "components/status_led/status_led.c: idle key/EC11 feedback must explicitly resume interactive output from low-power idle and preserve the scoped non-KEY transition-clear frame before feedback"
        )
    if (
        ec11_press_yield_body is None
        or "status_led_shutdown_confirm_active_locked(now_ms)" not in ec11_press_yield_body.group(0)
        or "s_state.ota_active" not in ec11_press_yield_body.group(0)
        or "status_led_ok_visual_percent_locked(now_ms) > 0U" not in ec11_press_yield_body.group(0)
        or "status_led_ble_repair_cue_active_locked(now_ms)" not in ec11_press_yield_body.group(0)
        or "s_state.processing_active" not in ec11_press_yield_body.group(0)
        or "status_led_recording_visual_percent_locked(now_ms) > 0U" not in ec11_press_yield_body.group(0)
        or "STATUS_LED_EC11_FEEDBACK_ROTATE_CW" not in ec11_press_yield_body.group(0)
        or "STATUS_LED_EC11_FEEDBACK_ROTATE_CCW" not in ec11_press_yield_body.group(0)
        or "STATUS_LED_PROFILE_AMBIENT" not in ec11_press_yield_body.group(0)
        or "STATUS_LED_BLE_TYPE_READY" not in ec11_press_yield_body.group(0)
        or ec11_feedback_body is None
        or "feedback == STATUS_LED_EC11_FEEDBACK_PRESS" not in ec11_feedback_body.group(0)
        or "status_led_ec11_press_feedback_should_yield_locked(now_ms)" not in ec11_feedback_body.group(0)
        or "status_led_clear_ec11_press_feedback_locked" not in status_led
        or recording_body is None
        or "status_led_clear_ec11_feedback_locked();" not in recording_body.group(0)
        or processing_body is None
        or "status_led_clear_ec11_press_feedback_locked();" not in processing_body.group(0)
    ):
        failures.append(
            "components/status_led/status_led.c: EC11 confirmed single-click press feedback must yield and clear pending PRESS instead of overwriting existing EC11 recording/processing/OTA/re-pair/rotation/ambient/shutdown effects"
        )
    if (
        "STATUS_LED_EC11_ROTATE_STEP_MS" in status_led
        or "ec11_feedback_last_step_ms" not in status_led
        or "ec11_feedback_motion_step" not in status_led
        or "STATUS_LED_EC11_ROTATION_HOLD_MS 2600U" not in status_led
        or "STATUS_LED_EC11_ROTATION_STEP_MS 150U" not in status_led
        or "STATUS_LED_EC11_ROTATION_HEAD_START_PERCENT 72U" not in status_led
        or "STATUS_LED_EC11_ROTATION_HEAD_END_PERCENT 44U" not in status_led
        or "STATUS_LED_EC11_ROTATION_BASE_START_PERCENT 4U" not in status_led
        or "STATUS_LED_EC11_ROTATION_BASE_END_PERCENT 1U" not in status_led
        or "status_led_decay_percent(" not in status_led
        or "status_led_refresh_ec11_feedback" not in status_led
        or "status_led_apply_ec11_feedback" not in status_led
        or "advance_motion" not in status_led
        or "status_led_ec11_feedback_motion_step_locked" not in status_led
        or "status_led_ec11_feedback_dot_from_step" not in status_led
        or "status_led_ec11_feedback_step_from_dot" not in status_led
        or "status_led_ec11_feedback_trail_index" not in status_led
        or "status_led_ec11_feedback_active_locked(now_ms)" not in status_led
        or not re.search(
            r"status_led_ec11_feedback_dot_from_step[\s\S]{0,360}"
            r"STATUS_LED_EC11_FEEDBACK_ROTATE_CW[\s\S]{0,80}"
            r"\?\s*\(STATUS_LED_EC11_COUNT\s*-\s*1U\s*-\s*motion_step\)\s*%\s*STATUS_LED_EC11_COUNT[\s\S]{0,120}"
            r":\s*motion_step\s*%\s*STATUS_LED_EC11_COUNT",
            status_led,
        )
        or not re.search(
            r"status_led_ec11_feedback_trail_index[\s\S]{0,260}"
            r"STATUS_LED_EC11_FEEDBACK_ROTATE_CW[\s\S]{0,120}"
            r"\?\s*\(dot\s*\+\s*offset\)\s*%\s*STATUS_LED_EC11_COUNT[\s\S]{0,120}"
            r":\s*\(dot\s*\+\s*STATUS_LED_EC11_COUNT\s*-\s*offset\)\s*%\s*STATUS_LED_EC11_COUNT",
            status_led,
        )
        or not re.search(
            r"feedback_ms\s*=\s*rotation_feedback[\s\S]{0,120}"
            r"STATUS_LED_EC11_ROTATION_HOLD_MS[\s\S]{0,120}"
            r"STATUS_LED_EC11_FEEDBACK_MS",
            status_led,
        )
        or not re.search(
            r"status_led_ec11_feedback_motion_step_locked\(uint32_t\s+now_ms\)[\s\S]*?"
            r"now_ms\s*-\s*s_state\.ec11_feedback_started_ms[\s\S]*?"
            r"STATUS_LED_EC11_ROTATION_STEP_MS[\s\S]*?"
            r"return\s+step\s*%\s*STATUS_LED_EC11_COUNT\s*;",
            status_led,
        )
        or not re.search(
            r"active_rotation_feedback\s*&&\s*s_state\.ec11_feedback\s*!=\s*feedback[\s\S]{0,520}"
            r"status_led_ec11_feedback_step_from_dot\(feedback,\s*current_dot\)[\s\S]{0,220}"
            r"s_state\.ec11_feedback_started_ms\s*=\s*now_ms",
            status_led,
        )
        or not re.search(
            r"status_led_decay_percent\(\s*motion_elapsed,\s*STATUS_LED_EC11_ROTATION_HOLD_MS,\s*STATUS_LED_EC11_ROTATION_HEAD_START_PERCENT,\s*STATUS_LED_EC11_ROTATION_HEAD_END_PERCENT\s*\)",
            status_led,
        )
        or not re.search(
            r"status_led_ec11_feedback_trail_index\(s_state\.ec11_feedback,\s*dot,\s*1U\)[\s\S]{0,320}"
            r"status_led_ec11_feedback_trail_index\(s_state\.ec11_feedback,\s*dot,\s*2U\)",
            status_led,
        )
    ):
        failures.append(
            "components/status_led/status_led.c: EC11 rotation must use a continuous time-driven white orbit, preserve the current rendered position when reversing, refresh brightness without restarting on same-direction detents, and keep 50 ms refresh while the cue is active"
        )
    ec11_notify_body = re.search(
        r"static\s+void\s+status_led_apply_ec11_feedback[\s\S]*?"
        r"\n\}\n\nvoid\s+status_led_notify_ec11_feedback",
        status_led,
    )
    if (
        ec11_notify_body is None
        or "rotation_feedback" not in ec11_notify_body.group(0)
        or "log_rotation_feedback" not in ec11_notify_body.group(0)
        or "diag_log(" not in ec11_notify_body.group(0)
    ):
        failures.append(
            "components/status_led/status_led.c: EC11 press activity must not write flash-backed status diag events on every bounce; only throttled rotation feedback may log"
        )
    if (
        "STATUS_LED_EC11_FEEDBACK_DIAG_MIN_MS 500U" not in status_led
        or "ec11_feedback_last_diag_ms" not in status_led
        or ec11_notify_body is None
        or "log_rotation_feedback" not in ec11_notify_body.group(0)
        or "STATUS_LED_EC11_FEEDBACK_DIAG_MIN_MS" not in ec11_notify_body.group(0)
    ):
        failures.append(
            "components/status_led/status_led.c: fast EC11 rotation feedback diagnostics must be throttled so flash-backed logs cannot make the ring cue go dark"
        )
    start_repair_body = re.search(
        r"static\s+void\s+status_led_start_ble_repair_locked[\s\S]*?"
        r"static\s+void\s+status_led_preview_clear_activity_locked",
        status_led,
    )
    if (
        start_repair_body is None
        or "status_led_clear_ec11_feedback_locked();" not in start_repair_body.group(0)
        or "hold_ms = status_led_repair_hold_ms(hold_ms);" not in start_repair_body.group(0)
        or "s_state.ble_repair_until_ms = repair_until_ms;" not in start_repair_body.group(0)
    ):
        failures.append(
            "components/status_led/status_led.c: BLE re-pair startup must clear transient EC11 feedback and start the bounded BLE plus EC11 re-pair cue"
        )
    elif (
        "cue_already_active = status_led_ble_repair_cue_active_locked(now_ms)" not in start_repair_body.group(0)
        or "if (!cue_already_active)" not in start_repair_body.group(0)
        or "status_led_force_transition_clear_locked(STATUS_LED_TRANSITION_CLEAR_REPAIR);" not in start_repair_body.group(0)
    ):
        failures.append(
            "components/status_led/status_led.c: repeated BLE re-pair notifications must not restart or clear the active three-cycle cue"
        )
    elif "preserve_double_feedback" in start_repair_body.group(0):
        failures.append(
            "components/status_led/status_led.c: BLE re-pair startup must not preserve EC11 double key feedback over the ordinary pairing cue"
        )
    if not re.search(
        r"uint8_t\s+write_count\s*=\s*force_clear_tx\s*\?\s*STATUS_LED_TRANSITION_CLEAR_WRITES\s*:\s*\(pwr_only_final_latch\s*\?\s*STATUS_LED_LOW_POWER_FINAL_LATCH_WRITES\s*:\s*1U\)",
        status_led,
    ):
        failures.append(
            "components/status_led/status_led.c: low-power and shutdown-final PWR-only frames must use repeated latch writes while normal active frames stay single-write"
        )
    if not re.search(
        r"if\s*\(!force_clear_tx\)\s*\{[\s\S]{0,360}status_led_suspend_quiet_idle_transports",
        status_led,
    ):
        failures.append(
            "components/status_led/status_led.c: low-power transition clear must not suspend transports before the final PWR-only latch frame"
        )
    if not re.search(
        r'\.name\s*=\s*"status"[\s\S]{0,620}\.prefer_dma\s*=\s*true',
        status_led,
    ):
        failures.append(
            "components/status_led/status_led.c: active status rail must keep DMA for advanced no-flicker REC/AI effects"
        )
    if (
        "rmt_tx_dma_strategy=status_strip_dma_full_frame_buffer" not in status_led
        or "status_tail_overlap_style=dma_audio_rec_ai_da_dada" not in status_led
        or "status_led_strip_backend_transmit_non_dma_once" not in status_led
    ):
        failures.append(
            "components/status_led/status_led.c: LED contract must preserve active status DMA and expose non-DMA low-power latch frame path"
        )
    if "bool force_non_dma = force_clear_tx || low_power_active;" in status_led:
        failures.append(
            "components/status_led/status_led.c: ordinary transition clear frames must not force non-DMA; scope force_non_dma to clear/latch paths only"
        )
    if not re.search(r"bool\s+force_non_dma\s*=\s*pwr_only_final_latch\s*;", status_led):
        failures.append(
            "components/status_led/status_led.c: only low-power/final latch frames may force every strip off DMA; interactive transition clears must keep EC11/KEY on SPI DMA"
        )
    if (
        "shutdown_final_all_zone_latched_started_ms" not in status_led
        or "shutdown_final_all_zone_latch_needed" not in status_led
        or "if (!force_clear_tx && shutdown_final_all_zone_latch_needed) {\n        tx_strip_mask = STATUS_LED_STRIP_MASK_ALL;" not in status_led
    ):
        failures.append(
            "components/status_led/status_led.c: shutdown-final latch must rewrite all strips once per final window so stale physical LED state is cleared"
        )

    keyboard = (REPO_ROOT / "components/keyboard/keyboard.c").read_text(encoding="utf-8")
    if not re.search(
        r"#define\s+KEYBOARD_CUSTOM_IDLE_BACKUP_POLL_MS\s+20\b",
        keyboard,
    ):
        failures.append(
            "components/keyboard/keyboard.c: KEY1-KEY4 normal idle backup scan must stay 20 ms so short physical taps do not depend on sampling luck"
        )
    if not re.search(
        r"#define\s+KEYBOARD_CUSTOM_DEBOUNCE_MS\s+20\b",
        keyboard,
    ):
        failures.append(
            "components/keyboard/keyboard.c: KEY1-KEY4 debounce must stay 20 ms so short physical taps can reach confirmed gestures"
        )
    if not re.search(
        r"#define\s+KEYBOARD_CUSTOM_DOUBLE_CLICK_WINDOW_MS\s+200\b",
        keyboard,
    ):
        failures.append(
            "components/keyboard/keyboard.c: KEY1-KEY4 double-click window must stay 200 ms so single-click feedback feels immediate"
        )
    if not re.search(
        r"#define\s+KEYBOARD_CUSTOM_LOW_POWER_IDLE_BACKUP_POLL_MS\s+20\b",
        keyboard,
    ):
        failures.append(
            "components/keyboard/keyboard.c: custom key low-power backup poll must be 20 ms so missed edge wake still catches short presses"
        )
    custom_start_body = re.search(
        r"static\s+esp_err_t\s+keyboard_custom_start[\s\S]*?"
        r"static\s+esp_err_t\s+keyboard_ec11_start",
        keyboard,
    )
    if "keyboard_custom_wake_task_from_isr" in keyboard or (
        custom_start_body is not None and "gpio_isr_handler_add" in custom_start_body.group(0)
    ):
        failures.append(
            "components/keyboard/keyboard.c: KEY1-KEY4 must not use runtime GPIO ISR; ISR storms can reset the board during press/hold"
        )
    if not re.search(
        r"keyboard_custom_apply_raw_feedback[\s\S]{0,900}"
        r"key->raw_feedback_tick\s*=\s*now[\s\S]{0,260}"
        r"status_led_notify_key_event\([^;]*true",
        keyboard,
    ) or not re.search(
        r"keyboard_custom_clear_raw_feedback[\s\S]{0,420}"
        r"key->raw_feedback_tick\s*=\s*0[\s\S]{0,260}"
        r"status_led_notify_key_event\([^;]*false",
        keyboard,
    ):
        failures.append(
            "components/keyboard/keyboard.c: raw low-power key press feedback must timestamp and use white physical press/release cues; confirmed gestures use purple later"
        )
    if not re.search(
        r"static\s+void\s+keyboard_custom_handle_raw_short_release[\s\S]*?"
        r"keyboard_custom_clear_raw_feedback\(key\);[\s\S]*?"
        r"key->pending_single[\s\S]*?"
        r"keyboard_custom_send_gesture\(key,\s*KEYBOARD_CUSTOM_GESTURE_DOUBLE\)[\s\S]*?"
        r"key->pending_single\s*=\s*true;[\s\S]*?"
        r"raw-only single pending",
        keyboard,
    ):
        failures.append(
            "components/keyboard/keyboard.c: raw KEY1-KEY4 taps released before debounce must still arm single/double purple gesture feedback"
        )
    if "raw_edge_release_before_debounce" not in keyboard:
        failures.append(
            "components/keyboard/keyboard.c: raw release before debounce must route through the raw-only short tap path"
        )
    if "low_power_raw_edge" in keyboard or "custom key low-power wake single synthesized" in keyboard:
        failures.append(
            "components/keyboard/keyboard.c: low-power KEY1-KEY4 wake must not bypass debounce or synthesize clicks from latched-only transients"
        )
    for pattern, description in [
        (
            "custom key low-power raw transition debounce armed",
            "low-power raw KEY1-KEY4 edge must be deferred to debounce before lighting",
        ),
        (
            "custom key low-power wake press pending debounce",
            "sampled low-power KEY1-KEY4 wake must wait for debounce confirmation",
        ),
        (
            "custom key low-power wake transient ignored",
            "latched-but-released low-power KEY1-KEY4 wake transient must be ignored",
        ),
    ]:
        if pattern not in keyboard:
            failures.append(f"components/keyboard/keyboard.c: {description}")
    clear_feedback_body = re.search(
        r"static\s+void\s+keyboard_custom_clear_raw_feedback[\s\S]*?"
        r"static\s+void\s+keyboard_custom_handle_timers",
        keyboard,
    )
    if clear_feedback_body is None:
        failures.append(
            "components/keyboard/keyboard.c: missing raw key feedback clear helper"
        )
    elif "status_led_notify_key_feedback" in clear_feedback_body.group(0):
        failures.append(
            "components/keyboard/keyboard.c: raw key feedback clear must not cancel a purple gesture confirmation; it should only end the white physical press cue"
        )
    if not re.search(
        r"long_hold_active[\s\S]{0,320}"
        r"STATUS_LED_KEY_FEEDBACK_LONG[\s\S]{0,320}"
        r"key_pressed_mask[\s\S]{0,420}"
        r"now_ms\s*>=\s*s_state\.key_feedback_until_ms\[index\]\s*&&\s*!long_hold_active",
        status_led,
    ):
        failures.append(
            "components/status_led/status_led.c: confirmed long key gesture must stay purple while the key remains held"
        )
    if not re.search(
        r"void\s+status_led_notify_key_event[\s\S]*?"
        r"bool\s+long_release_fade\s*=\s*false[\s\S]*?"
        r"s_state\.key_feedback\[key_index\]\s*==\s*STATUS_LED_KEY_FEEDBACK_LONG[\s\S]*?"
        r"long_release_fade\s*=\s*true[\s\S]*?"
        r"s_state\.key_until_ms\[key_index\]\s*=\s*long_release_fade\s*\?[\s\S]*?"
        r"0U\s*:[\s\S]*?now_ms\s*\+\s*STATUS_LED_KEY_FEEDBACK_MS",
        status_led,
    ):
        failures.append(
            "components/status_led/status_led.c: long key release fade must suppress the white physical release tail"
        )
    if not re.search(
        r"#define\s+KEYBOARD_EC11_LOW_POWER_IDLE_POLL_MS\s+20\b",
        keyboard,
    ):
        failures.append(
            "components/keyboard/keyboard.c: EC11 low-power backup poll must stay 20 ms so idle rotation cannot be swallowed by a missed edge wake"
        )
    if not re.search(
        r"#define\s+KEYBOARD_EC11_FEEDBACK_REVERSE_MIN_ACCUM\s+2\b[\s\S]*?"
        r"static\s+int8_t\s+keyboard_ec11_feedback_delta_from_accumulator\(\s*"
        r"const\s+keyboard_ec11_state_t\s+\*state,\s*int32_t\s+accumulator\s*\)[\s\S]{0,520}"
        r"state->last_feedback_delta[\s\S]{0,260}"
        r"magnitude\s*<\s*KEYBOARD_EC11_FEEDBACK_REVERSE_MIN_ACCUM[\s\S]{0,120}"
        r"return\s+0\s*;[\s\S]{0,180}"
        r"return\s+candidate\s*;",
        keyboard,
    ):
        failures.append(
            "components/keyboard/keyboard.c: EC11 rotation LED feedback direction must come from accumulated detent direction and require two opposite edges before reversing the cue"
        )
    if not re.search(
        r"was_low_power_idle\s*=\s*keyboard_power_state_is_low_power_idle\(\)[\s\S]{0,260}"
        r"power_manager_record_activity\(\"ec11_rotate\"\)[\s\S]{0,360}"
        r"feedback_delta\s*=\s*keyboard_ec11_feedback_delta_from_accumulator\(state,\s*state->detent_accumulator\)[\s\S]{0,260}"
        r"if\s*\(\s*\(was_low_power_idle\s*\|\|\s*raw_state\s*!=\s*KEYBOARD_EC11_DETENT_STATE\)\s*&&\s*feedback_delta\s*!=\s*0\s*\)[\s\S]{0,220}"
        r"keyboard_ec11_refresh_feedback_for_delta\(state,\s*feedback_delta,\s*was_low_power_idle\)",
        keyboard,
    ):
        failures.append(
            "components/keyboard/keyboard.c: low-power and partial EC11 rotation edges must wake power manager and refresh local rotation feedback from accumulated direction before detent/HID dispatch"
        )
    if not re.search(
        r"keyboard_ec11_refresh_feedback_for_delta[\s\S]{0,900}"
        r"direction_changed[\s\S]{0,500}"
        r"status_led_refresh_ec11_feedback\(delta\s*>\s*0[\s\S]{0,160}"
        r"STATUS_LED_EC11_FEEDBACK_ROTATE_CW[\s\S]{0,160}"
        r"STATUS_LED_EC11_FEEDBACK_ROTATE_CCW",
        keyboard,
    ):
        failures.append(
            "components/keyboard/keyboard.c: continuous EC11 feedback must refresh from valid edges without stepping the ring and reverse direction after the accumulated direction clears the bounce guard"
        )
    if not re.search(
        r"keyboard_enable_active_low_light_sleep_wake[\s\S]{0,900}"
        r"gpio_wakeup_enable\([^;]*GPIO_INTR_LOW_LEVEL\)[\s\S]{0,900}"
        r"esp_sleep_enable_gpio_wakeup\(\)",
        keyboard,
    ):
        failures.append(
            "components/keyboard/keyboard.c: physical keys/EC11 must configure active-low GPIO wake for light sleep"
        )
    if not re.search(
        r"keyboard_custom_start[\s\S]*GPIO_INTR_DISABLE[\s\S]*"
        r"keyboard_enable_active_low_light_sleep_wake\([\s\S]*"
        r"wake=active_low_gpio_wakeup\+20ms_scan[\s\S]*"
        r"low_power_wake=active_low_gpio_wakeup\+20ms_scan",
        keyboard,
    ):
        failures.append(
            "components/keyboard/keyboard.c: custom keys must use active-low light-sleep wake plus 20 ms scan, not runtime GPIO interrupts"
        )
    keyboard_start_body = re.search(
        r"esp_err_t\s+keyboard_start\(void\)[\s\S]*?"
        r"esp_err_t\s+keyboard_start_safe_mode\(void\)",
        keyboard,
    )
    if keyboard_start_body is None:
        failures.append("components/keyboard/keyboard.c: missing keyboard_start body")
    else:
        keyboard_start_text = keyboard_start_body.group(0)
        custom_index = keyboard_start_text.find("keyboard_custom_start()")
        ec11_index = keyboard_start_text.find("keyboard_ec11_start()")
        voice_index = keyboard_start_text.find("voice_recording_control_start()")
        if custom_index < 0 or ec11_index < 0 or voice_index < 0 or not (custom_index < voice_index and ec11_index < voice_index):
            failures.append(
                "components/keyboard/keyboard.c: cold boot input readiness must start KEY1-KEY4 and EC11 A/B before voice recording/audio control"
            )
    ec11_rotation_body = re.search(
        r"static\s+esp_err_t\s+keyboard_ec11_dispatch_rotation\([^)]*\)\s*\{[\s\S]*?"
        r"static\s+esp_err_t\s+keyboard_ble_control_write",
        keyboard,
    )
    if ec11_rotation_body is None:
        failures.append(
            "components/keyboard/keyboard.c: EC11 rotation must show local feedback before HID send, and pairing/not-connected invalid-state must not swallow the rotation light effect"
        )
    else:
        ec11_rotation_text = ec11_rotation_body.group(0)
        notify_index = ec11_rotation_text.find("status_led_notify_ec11_feedback")
        send_index = ec11_rotation_text.find("ble_hid_send_consumer_usage_async")
        invalid_state_index = ec11_rotation_text.find("ret == ESP_ERR_INVALID_STATE")
        local_feedback_index = ec11_rotation_text.find("local feedback only")
        kept_feedback_index = ec11_rotation_text.find("local_feedback=kept")
        if (
            notify_index < 0
            or send_index < 0
            or invalid_state_index < 0
            or local_feedback_index < 0
            or kept_feedback_index < 0
            or notify_index > send_index
            or "status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE" in ec11_rotation_text
        ):
            failures.append(
                "components/keyboard/keyboard.c: EC11 rotation must show local feedback before HID send, and HID backpressure must not light BLE/WARN or swallow the rotation light effect"
            )
    if not re.search(
        r"keyboard_ec11_queue_edge_from_isr[\s\S]*?"
        r"raw_state\s*==\s*s_ec11_isr_last_raw_state[\s\S]*?return;[\s\S]*?"
        r"s_ec11_overflow_raw_state\s*=\s*raw_state;[\s\S]*?"
        r"s_ec11_overflow_pending\s*=\s*true;",
        keyboard,
    ):
        failures.append(
            "components/keyboard/keyboard.c: EC11 ISR must compress duplicate raw states and preserve the latest overflow state"
        )
    if not re.search(
        r"keyboard_ec11_task[\s\S]*?"
        r"while\s*\(xQueueReceive\(s_ec11_event_queue,\s*&event,\s*0\)[\s\S]*?"
        r"s_ec11_overflow_pending[\s\S]*?"
        r"keyboard_ec11_handle_state\(&s_ec11_state,\s*overflow_raw_state\)[\s\S]*?"
        r"keyboard_ec11_handle_state\(&s_ec11_state,\s*keyboard_ec11_read_raw_state\(\)\)",
        keyboard,
    ):
        failures.append(
            "components/keyboard/keyboard.c: EC11 task must chase the latest physical A/B state after draining a burst"
        )
    if not re.search(
        r"keyboard_ec11_start[\s\S]*"
        r"keyboard_enable_active_low_light_sleep_wake\(BOARD_PINS_EC11_A_IO[\s\S]*"
        r"keyboard_enable_active_low_light_sleep_wake\(BOARD_PINS_EC11_B_IO",
        keyboard,
    ):
        failures.append(
            "components/keyboard/keyboard.c: EC11 A/B pins must both be light-sleep wake sources"
        )

    voice_key = (
        REPO_ROOT / "ports/esp32/voice_key_input/voice_key_input_esp32.c"
    ).read_text(encoding="utf-8")
    voice_recording = (
        REPO_ROOT / "components/voice_recording_control/voice_recording_control.c"
    ).read_text(encoding="utf-8")
    voice_start_body = re.search(
        r"esp_err_t\s+voice_recording_control_start\(void\)[\s\S]*?"
        r"bool\s+voice_recording_control_consume_usb_control_byte",
        voice_recording,
    )
    if voice_start_body is None:
        failures.append(
            "components/voice_recording_control/voice_recording_control.c: missing voice_recording_control_start body"
        )
    else:
        voice_start_text = voice_start_body.group(0)
        voice_key_index = voice_start_text.find("voice_key_input_start()")
        audio_index = voice_start_text.find("audio_capture_start()")
        if voice_key_index < 0 or audio_index < 0 or voice_key_index > audio_index:
            failures.append(
                "components/voice_recording_control/voice_recording_control.c: cold boot EC11 push input must start before audio_capture_start so audio init cannot delay wake/button feedback"
            )
    if (
        not re.search(r"#define\s+VOICE_KEY_INPUT_IDLE_BACKUP_POLL_MS\s+\(20\)", voice_key)
        or not re.search(r"#define\s+VOICE_KEY_INPUT_LOW_POWER_IDLE_BACKUP_POLL_MS\s+\(20\)", voice_key)
    ):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 push must keep 20 ms active/low-power backup scan around the runtime GPIO wake interrupt"
        )
    if not re.search(
        r"#define\s+VOICE_KEY_INPUT_DOUBLE_CLICK_MIN_GAP_MS\s+\(80\)",
        voice_key,
    ):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 recovery double-click must keep the 80 ms minimum gap so contact bounce cannot trigger recovery"
        )
    if not re.search(
        r"#define\s+VOICE_KEY_INPUT_DOUBLE_CLICK_WINDOW_MS\s+\(200\)",
        voice_key,
    ):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 single-click dispatch must match the 200 ms KEY1-KEY4 double-click feel"
        )
    if not re.search(
        r"#define\s+VOICE_KEY_INPUT_RECOVERY_DOUBLE_CLICK_WINDOW_MS\s+\(200\)",
        voice_key,
    ):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 recovery double-click window must match the 200 ms KEY1-KEY4 double-click feel"
        )
    min_gap_match = re.search(
        r"#define\s+VOICE_KEY_INPUT_DOUBLE_CLICK_MIN_GAP_MS\s+\((\d+)\)",
        voice_key,
    )
    generated_gap_match = re.search(
        r"#define\s+VOICE_KEY_INPUT_GENERATED_INTER_CLICK_RELEASE_MS\s+\((\d+)\)",
        voice_key,
    )
    if (
        min_gap_match is None
        or generated_gap_match is None
        or int(generated_gap_match.group(1)) < int(min_gap_match.group(1)) + 40
    ):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: generated EC11 double-click diagnostics must leave at least 40 ms margin above the physical min-gap so validation cannot collapse into single-click fallback"
        )
    if (
        "elapsed_lt_min_gap_ms" not in voice_key
        or "second_click_too_soon" not in voice_key
        or "stable_release" not in voice_key
        or "recovery_candidate_from_raw" not in voice_key
        or "!recovery_candidate_from_raw" not in voice_key
        or "Raw-only EC11 short clicks are accepted only as single-click candidates after stable idle" not in (
            REPO_ROOT / "docs/features/status_led.md"
        ).read_text(encoding="utf-8")
    ):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 recovery double-click must require the stable/debounced path and keep raw-only edges from triggering recovery"
        )
    raw_note_match = re.search(
        r"static void voice_key_input_note_raw_press_edge[\s\S]*?\n\}",
        voice_key,
    )
    raw_note_body = raw_note_match.group(0) if raw_note_match else ""
    if "voice_key_input_record_recovery_event(" in raw_note_body:
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: raw EC11 press edges must not directly dispatch BLE recovery"
        )
    if (
        "recovery_idle_guard" in voice_key
        or "VOICE_KEY_INPUT_RECOVERY_IDLE_GUARD" in voice_key
        or "s_recording_output_enabled" in voice_key
        or "recovery_cancels_active_recording=1" not in voice_key
        or "cannot be downgraded into an ordinary EC11 single click" not in (
            REPO_ROOT / "docs/features/firmware-feature-map.md"
        ).read_text(encoding="utf-8")
    ):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 double-click recovery must not be downgraded into a single-click fallback by recording-output or idle-guard state"
        )
    if not re.search(
        r"#define\s+VOICE_KEY_INPUT_LONG_PRESS_IGNORE_MS\s+\(800\)",
        voice_key,
    ):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 long-press shutdown cue must start at 800 ms so the hold does not feel dead"
        )
    if not re.search(
        r"voice_key_input_direct_gpio_init[\s\S]{0,420}"
        r"\.intr_type\s*=\s*GPIO_INTR_ANYEDGE[\s\S]{0,420}"
        r"gpio_isr_handler_add\(\s*VOICE_KEY_INPUT_DIRECT_GPIO\s*,\s*voice_key_input_direct_gpio_wake_from_isr[\s\S]{0,420}"
        r"voice_key_input_enable_light_sleep_wake\(\)",
        voice_key,
    ):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 push GPIO18 must use any-edge runtime wake with a lightweight edge latch plus active-low light-sleep wake"
        )
    required_voice_irq_tokens = [
        "static void IRAM_ATTR voice_key_input_direct_gpio_wake_from_isr",
        "vTaskNotifyGiveFromISR(task_handle",
        "s_direct_gpio_isr_press_pending",
        "voice_key_input_take_direct_gpio_isr_press_pending",
        "voice_key_input_note_raw_press_edge",
        "EC11 push raw press tracked from ISR edge latch",
        "double-click recovery detected",
        "runtime_irq=anyedge_notify_edge_latch",
    ]
    for token in required_voice_irq_tokens:
        if token not in voice_key:
            failures.append(
                f"ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 push runtime IRQ must keep a lightweight edge latch while debounced task-context sampling owns click/recovery decisions ({token})"
            )
    forbidden_voice_irq_tokens = [
        "GPIO_INTR_LOW_LEVEL)",
        "xTaskGetTickCountFromISR",
        "portENTER_CRITICAL_ISR",
        "portEXIT_CRITICAL_ISR",
        "voice_key_input_note_raw_press_edge",
        "voice_key_input_record_recovery_event(",
        "voice_key_input_dispatch_custom_key_event(",
    ]
    voice_isr_match = re.search(
        r"static void IRAM_ATTR voice_key_input_direct_gpio_wake_from_isr[\s\S]*?\n\}",
        voice_key,
    )
    voice_isr_body = voice_isr_match.group(0) if voice_isr_match else ""
    for token in forbidden_voice_irq_tokens:
        if token == "GPIO_INTR_LOW_LEVEL)":
            if "gpio_set_intr_type(VOICE_KEY_INPUT_DIRECT_GPIO, GPIO_INTR_LOW_LEVEL)" in voice_key:
                failures.append(
                    "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 push must not switch runtime GPIO IRQ to low-level mode"
                )
        elif token in voice_isr_body:
            failures.append(
                f"ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 push ISR must not do business logic ({token})"
            )
    if (
        "static uint32_t voice_key_input_elapsed_ms" not in voice_key
        or "portTICK_PERIOD_MS" not in voice_key
        or "TickType_t sample_started_tick;" not in voice_key
        or "TickType_t pressed_started_tick;" not in voice_key
        or "TickType_t pending_click_started_tick;" not in voice_key
    ):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 debounce, hold, and double-click timing must be based on real FreeRTOS ticks"
        )
    if (
        "pending_click_ms += VOICE_KEY_INPUT_POLL_MS" in voice_key
        or "pressed_ms + VOICE_KEY_INPUT_POLL_MS" in voice_key
    ):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 short-click and long-press timing must not accumulate by poll-loop count"
        )
    if not re.search(
        r"sample_stable_ms\s*=\s*voice_key_input_elapsed_ms\(now_tick,\s*button->sample_started_tick\)[\s\S]{0,180}"
        r"sample_stable_ms\s*<\s*VOICE_KEY_INPUT_DEBOUNCE_MS",
        voice_key,
    ):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 debounce must use elapsed time so ISR wake storms cannot fake 30 ms stability"
        )
    if not re.search(
        r"button->pressed_started_tick\s*=\s*now_tick[\s\S]{0,900}"
        r"voice_key_input_elapsed_ms\(now_tick,\s*button->pressed_started_tick\)",
        voice_key,
    ):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 long-press shutdown cue must use real hold duration from pressed_started_tick"
        )
    if not re.search(
        r"button->pending_click_started_tick\s*=\s*now_tick[\s\S]{0,900}"
        r"voice_key_input_elapsed_ms\(now_tick,\s*button->pending_click_started_tick\)",
        voice_key,
    ):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 double-click window must use real elapsed time, not fast poll iterations"
        )
    if (
        not re.search(r"#define\s+VOICE_KEY_INPUT_HOLD_FEEDBACK_REFRESH_MS\s+\(300\)", voice_key)
        or "TickType_t hold_feedback_tick;" not in voice_key
        or not re.search(
            r"pressed\s*&&\s*!button->long_press_reported[\s\S]{0,900}"
            r"VOICE_KEY_INPUT_HOLD_FEEDBACK_REFRESH_MS[\s\S]{0,420}"
            r"power_manager_record_activity\(\"ec11_key_hold\"\)",
            voice_key,
        )
    ):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 hold must refresh activity without white press feedback until long-press shutdown confirmation starts"
        )
    dispatch_body = re.search(
        r"static\s+void\s+voice_key_input_dispatch_custom_key_event[\s\S]*?"
        r"static\s+bool\s+voice_key_input_button_raw_pressed",
        voice_key,
    )
    if dispatch_body is None or "status_led_notify_ec11_feedback(STATUS_LED_EC11_FEEDBACK_PRESS)" not in dispatch_body.group(0):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 single-click fallback must restore the local white EC11 press cue only after the double-click window resolves"
        )
    recovery_body = re.search(
        r"static\s+void\s+voice_key_input_record_recovery_event[\s\S]*?"
        r"static\s+void\s+voice_key_input_drain_generated_events",
        voice_key,
    )
    if recovery_body is None or "status_led_notify_ec11_feedback" in recovery_body.group(0):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 double-click recovery must go through BLE re-pair state, not a key-style EC11 feedback enum"
        )
    if recovery_body is None or 'status_led_notify_ble_repairing("ec11_double_click_recovery")' not in recovery_body.group(0):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 double-click recovery must show the BLE repair cue immediately when the double click is detected"
        )
    if not re.search(
        r"voice_key_input_next_wait_ms[\s\S]{0,700}"
        r"VOICE_KEY_INPUT_LOW_POWER_IDLE_BACKUP_POLL_MS",
        voice_key,
    ) or "watchdog_platform_task_notify_take_low_power" not in voice_key:
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 push must keep bounded 20 ms scan during low-power waits alongside the edge-latched runtime GPIO ISR"
        )
    voice_raw_feedback_body = re.search(
        r"static\s+void\s+voice_key_input_apply_raw_feedback[\s\S]*?\n\}",
        voice_key,
    )
    if voice_raw_feedback_body is None:
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: missing EC11 raw press tracking helper"
        )
    elif not re.search(
        r"if\s*\(\s*button->raw_feedback_pressed\s*\)\s*\{\s*return\s*;",
        voice_raw_feedback_body.group(0),
    ):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 raw press tracking must suppress repeated ISR/raw bounce during one physical press"
        )
    elif "status_led_notify_ec11_feedback" in voice_raw_feedback_body.group(0):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 raw press tracking must not light the knob before the single/double-click decision window resolves"
        )
    if (
        "voice_key_input_handle_short_click_release(button, now_tick, \"raw-only\")" not in voice_key
        or not re.search(
            r"else\s+if\s*\(\s*!pressed\s*\)\s*\{[\s\S]{0,360}"
            r"button->raw_feedback_pressed[\s\S]{0,360}"
            r"voice_key_input_handle_short_click_release\(button,\s*now_tick,\s*\"raw-only\"\)[\s\S]{0,360}"
            r"voice_key_input_clear_raw_feedback\(button\);",
            voice_key,
        )
    ):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 raw-only short taps must be accepted on stable idle, then clear the raw press latch so the next real press can be tracked"
        )
    if "gpio_set_intr_type(VOICE_KEY_INPUT_DIRECT_GPIO" in voice_key:
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 push must not switch to low-level GPIO IRQ mode in idle"
        )
    if not re.search(
        r"voice_key_input_enable_light_sleep_wake[\s\S]{0,900}"
        r"gpio_wakeup_enable\([^;]*GPIO_INTR_LOW_LEVEL\)[\s\S]{0,900}"
        r"esp_sleep_enable_gpio_wakeup\(\)",
        voice_key,
    ):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: EC11 push/direct key must configure active-low GPIO wake for light sleep"
        )
    if not re.search(
        r"voice_key_input_direct_gpio_init[\s\S]{0,1100}"
        r"gpio_config\(&direct_cfg\)[\s\S]{0,900}"
        r"voice_key_input_enable_light_sleep_wake\(\)",
        voice_key,
    ):
        failures.append(
            "ports/esp32/voice_key_input/voice_key_input_esp32.c: direct key wake must be armed during GPIO init"
        )
    if not re.search(
        r"strcmp\(command,\s*\"STATUS\"\)[\s\S]*"
        r"power_manager_print_status\(\)[\s\S]*"
        r"strcmp\(command,\s*\"IDLE\"\)[\s\S]*"
        r"power_manager_print_idle_diag\(\)[\s\S]*"
        r"strcmp\(command,\s*\"SHUTDOWN\"\)[\s\S]*"
        r"power_manager_record_activity\(\"usb_power_command\"\)[\s\S]*"
        r"strcmp\(command,\s*\"ACTIVITY\"\)[\s\S]*"
        r"power_manager_record_activity\(\"usb_power_command\"\)[\s\S]*"
        r"POWER:\s+unknown command",
        power_manager,
    ):
        failures.append(
            "components/power_manager/power_manager.c: POWER unknown/status/idle commands must stay passive while SHUTDOWN/ACTIVITY record activity"
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
        "PASS: power manager static verification covers hardware shutdown, PWR_HOLD/GPIO9, "
        "external-power blockers, configurable idle actions, audio idle power-save idempotence, "
        "low-power input wake/poll guards, PWR_HOLD guard, diagnostics, and Deep Sleep removal."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
