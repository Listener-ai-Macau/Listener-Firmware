from __future__ import annotations

import sys
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
        "hardware_shutdown_threshold_ms",
        "external_power_present",
        "usb_power_present",
        "charging",
        "charge_full",
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
        "power_manager_read_power_source",
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
        "audio_capture_set_idle_power_save",
        "status_led_prepare_sleep",
        'strcmp(command, "SHUTDOWN")',
    ],
    "ports/esp32/audio_capture/audio_capture_esp32.c": [
        "audio_capture_set_idle_power_save",
        "i2s_channel_disable",
        "DIAG_AUDIO_IDLE_POWER",
    ],
    "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c": [
        "ble_hid_gap_set_low_power_advertising",
        "ble_hid_gap_prepare_shutdown_disconnect",
        "ble_gap_terminate",
        "ble_gap_adv_stop",
        "BLE_GAP_ADV_ITVL_MS(s_low_power_advertising ? 1000 : 30)",
        "DIAG_GAP_RECOVERY",
    ],
    "ports/esp32/ble_hid/ble_hid.c": [
        "power_manager_consume_usb_command",
        "POWER_MANAGER_BLOCKER_DIAG_EXPORT",
        "power_manager_set_ble_connected",
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
        "CONFIG_POWER_MANAGER_CONNECTED_IDLE_MS=30000",
        "CONFIG_POWER_MANAGER_HARDWARE_SHUTDOWN_MS=1800000",
    ],
    "main/main.c": [
        "esp_pm_configure",
        "light_sleep_enable = true",
        "esp_reset_reason",
        "board_get_v2_power_hold_snapshot",
        "power cold-boot status",
    ],
    "components/board/board.c": [
        "Voice Keyboard V2",
        "EC11 push/GPIO18",
        "PWR_HOLD/GPIO11",
        "v2_gpio11_power_latch_hold_low_release_high_for_hardware_shutdown",
        "gpio_set_level(BOARD_PINS_PWR_HOLD_IO, 0)",
        "gpio_set_level(BOARD_PINS_PWR_HOLD_IO, level)",
        "released high for hardware shutdown",
        "hold-low",
        "~POWER:SHUTDOWN",
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
    ],
    "components/board/board.c": [
        "v2_gpio46_power_latch_hold_high_release_low_for_hardware_shutdown",
        "v2_gpio46_power_latch_hold_low_release_high_for_hardware_shutdown",
        "hold-high",
        "released low for hardware shutdown",
        "held high",
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

    if failures:
        print("FAIL: power manager static verification failed")
        for failure in failures:
            print(f" - {failure}")
        return 1

    print(
        "PASS: power manager static verification covers hardware shutdown, PWR_HOLD/GPIO11, "
        "external-power blockers, idle actions, diagnostics, and Deep Sleep removal."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
