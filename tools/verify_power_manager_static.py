from __future__ import annotations

import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


CHECKS = {
    "components/power_manager/include/power_manager.h": [
        "POWER_MANAGER_STATE_ACTIVE",
        "POWER_MANAGER_STATE_CONNECTED_IDLE",
        "POWER_MANAGER_STATE_DISCONNECTED_IDLE",
        "POWER_MANAGER_STATE_OVERNIGHT_SLEEP",
        "POWER_MANAGER_BLOCKER_RECORDING",
        "POWER_MANAGER_BLOCKER_BLE_AUDIO",
        "POWER_MANAGER_BLOCKER_DIAG_EXPORT",
        "POWER_MANAGER_BLOCKER_EXTERNAL_POWER",
        "POWER_MANAGER_WAKE_POLICY_V2_EC11_PROVISIONAL",
        "sleep_blockers",
        "user_idle_ms",
        "radio_idle_ms",
        "external_power_present",
        "usb_power_present",
        "charging",
        "charge_full",
        "automatic_sleep_blocked_by_external_power",
        "last_sleep_duration_ms",
        "sleep_entry_battery_mv",
        "wake_battery_mv",
        "sleep_drain_mv_per_hour",
        "voice_key_limitation",
        "power_manager_consume_usb_command",
    ],
    "components/power_manager/power_manager.c": [
        "~POWER:STATUS",
        "wake_policy=%s",
        "wake_capable_keys=%s",
        "voice_key_deep_sleep_wake=%u",
        "V2 EC11-KEY_IO/GPIO11 is the RTC-capable wake candidate",
        "deep-sleep wake remains disabled until power-latch isolation",
        "use USB reset or power cycle until EC11 wake is signed off",
        "POWER_MANAGER_WAKE_POLICY_ACTIVE POWER_MANAGER_WAKE_POLICY_V2_EC11_PROVISIONAL",
        "s_last_user_activity_ms",
        "s_last_radio_activity_ms",
        "power_manager_user_idle_ms_locked",
        "power_manager_radio_idle_ms_locked",
        "user_idle_ms=%",
        "radio_idle_ms=%",
        "sleep_stats_valid=%u",
        "sleep_duration_ms=%",
        "sleep_entry_battery_mv=%",
        "wake_battery_mv=%",
        "sleep_drain_level_per_hour_x100=%",
        "sleep_blockers=0x%08",
        "external_power_present=%u",
        "charging=%u",
        "charge_full=%u",
        "usb_det_level=%s",
        "bat_chg_level=%s",
        "bat_std_level=%s",
        "automatic overnight sleep blocked by external power",
        "POWER_MANAGER_BLOCKER_EXTERNAL_POWER",
        "power_manager_read_power_source",
        "power_manager_sleep_blockers_for_source",
        "power_manager_automatic_sleep_blocked_by_external_power_locked",
        "board_get_v2_power_input_snapshot",
        "rtc_time_get",
        "rtc_time_slowclk_to_us",
        "CONFIG_POWER_MANAGER_AUDIO_IDLE_MS",
        "POWER_MANAGER_SLEEP_REASON_OVERNIGHT_IDLE",
        "esp_deep_sleep_start",
        "esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)",
        "esp_sleep_enable_ext1_wakeup_io",
        "DIAG_POWER_SLEEP_ENTRY",
        "DIAG_POWER_SLEEP_BLOCKED",
        "DIAG_POWER_WAKE_POLICY",
        "DIAG_POWER_EXTERNAL_POWER",
        "battery_monitor_read",
        "ble_hid_gap_request_low_power_connection",
        "ble_hid_gap_set_low_power_advertising",
        "audio_capture_set_idle_power_save",
    ],
    "ports/esp32/audio_capture/audio_capture_esp32.c": [
        "audio_capture_set_idle_power_save",
        "i2s_channel_disable",
        "DIAG_AUDIO_IDLE_POWER",
    ],
    "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c": [
        "ble_hid_gap_set_low_power_advertising",
        "ble_hid_gap_request_low_power_connection",
        "BLE_GAP_ADV_ITVL_MS(s_low_power_advertising ? 1000 : 30)",
        "DIAG_GAP_CONN_PARAM_REQ",
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
        "DIAG_POWER_BATTERY_WARN",
        "DIAG_POWER_WAKE_POLICY",
        "DIAG_POWER_EXTERNAL_POWER",
    ],
    "components/power_manager/Kconfig.projbuild": [
        "POWER_MANAGER_ENABLE",
        "POWER_MANAGER_AUDIO_IDLE_MS",
        "POWER_MANAGER_OVERNIGHT_SLEEP_MS",
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
        "CONFIG_POWER_MANAGER_OVERNIGHT_SLEEP_MS=1800000",
    ],
    "main/main.c": [
        "esp_pm_configure",
        "light_sleep_enable = true",
    ],
    "components/board/board.c": [
        "Voice Keyboard V2",
        "EC11 push/GPIO11",
        "V2 EC11-KEY/GPIO11 deep-sleep wake remains disabled",
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

    power_manager = (REPO_ROOT / "components/power_manager/power_manager.c").read_text(encoding="utf-8")
    for stale in (
        "N4 validation profile: EC11-KEY/GPIO35",
        "V2 EC11-KEY_IO/GPIO18 is the RTC-capable wake candidate",
        "press KEY4/GPIO21 after deep sleep on N4",
        "POWER_MANAGER_WAKE_POLICY_CODE ((uint32_t)POWER_MANAGER_WAKE_POLICY_KEY4_ONLY)",
    ):
        if stale in power_manager:
            failures.append(f"components/power_manager/power_manager.c: stale token {stale!r}")

    board = (REPO_ROOT / "components/board/board.c").read_text(encoding="utf-8")
    for stale in ("Voice Keyboard N4", "EC11 push/GPIO18", "EC11 push/GPIO35", "N4 deep sleep wakes by KEY4/GPIO21"):
        if stale in board:
            failures.append(f"components/board/board.c: stale token {stale!r}")

    if failures:
        print("FAIL: power manager static verification failed")
        for failure in failures:
            print(f" - {failure}")
        return 1

    print("PASS: power manager static verification covers states, blockers, idle actions, diagnostics, and sleep path.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
