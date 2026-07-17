from __future__ import annotations

import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


CHECKS = {
    "ports/esp32/voice_key_input/voice_key_input_esp32.c": (
        "VOICE_KEY_INPUT_FAST_IDLE_RECORDING_TARGET_MS (50)",
        "device_settings_get_ec11_fast_recording_enabled()",
        "voice_key_input_power_state_is_low_power_idle()",
        "ble_audio_stream_is_type_link_ready()",
        "fast Idle recording skipped: origin=%s ec11_fast_recording=%u low_power_idle=%u type_link_ready=%u event_queue=%u suppression_pending=%u target_ms=%d",
        "voice_key_input_request_fast_idle_recording(button, \"raw_edge\")",
        "voice_key_input_request_fast_idle_recording(&s_direct_gpio_state, \"isr_edge\")",
        "voice_key_input_notify_recording_control_task();",
        "s_fast_idle_recording_hid_suppression_pending",
        "confirmed single-click handled by fast Idle recording",
        "fast Idle recording active: recovery double-click skipped",
        "!s_fast_idle_recording_hid_suppression_pending &&\n        voice_key_input_recovery_double_click_ready(button, now_tick);",
        "s_fast_idle_recording_cancelled = true;",
        "voice_key_input_cancel_fast_idle_recording_for_long_press();",
        "voice_key_input_take_fast_idle_recording_cancel_event",
    ),
    "components/voice_recording_control/voice_recording_control.c": (
        "voice_recording_control_handle_fast_idle_ec11_start",
        "voice_key_input_take_fast_idle_recording_event(&press_to_control_ms)",
        "voice_key_input_complete_fast_idle_recording_event(suppress_fallback_hid)",
        "voice_key_input_take_fast_idle_recording_cancel_event()",
        "watchdog_platform_task_notify_take",
        "press_to_control_ms=%",
        "target_ms=50",
    ),
    "components/device_settings/device_settings.c": (
        'DEVICE_SETTINGS_NVS_EC11_FAST_RECORDING_KEY "ec11_rec"',
        "nvs_get_u8(nvs, DEVICE_SETTINGS_NVS_EC11_FAST_RECORDING_KEY",
        "nvs_set_u8(\n            nvs,\n            DEVICE_SETTINGS_NVS_EC11_FAST_RECORDING_KEY",
        'strcmp(key, "e11r") == 0',
        "ec11_fast_recording=",
        "ble_hid_gap_set_ec11_fast_recording_enabled",
        "s_settings.ec11_fast_recording_enabled",
    ),
    "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c": (
        "s_ec11_fast_recording_armed",
        "low-power idle connection retained active: e11r fast recording is armed",
        "ble_hid_gap_set_ec11_fast_recording_enabled",
        "power_manager_get_state() == POWER_MANAGER_STATE_CONNECTED_IDLE",
    ),
    "components/device_settings/include/device_settings.h": (
        "DEVICE_SETTINGS_DEFAULT_EC11_FAST_RECORDING_ENABLED 1",
        "bool ec11_fast_recording_enabled;",
        "device_settings_get_ec11_fast_recording_enabled",
    ),
    "ports/esp32/voice_key_input/include/voice_key_input.h": (
        '#include "freertos/FreeRTOS.h"',
        '#include "freertos/task.h"',
        "voice_key_input_set_recording_control_task",
    ),
}


ORDERED_PATHS = {
    "ports/esp32/voice_key_input/voice_key_input_esp32.c": (
        "voice_key_input_request_fast_idle_recording(button, \"raw_edge\");\n                }\n                power_manager_record_activity(\"ec11_key_press\");",
        "voice_key_input_request_fast_idle_recording(&s_direct_gpio_state, \"isr_edge\");\n                power_manager_record_activity(\"ec11_key_press\");",
        "bool recovery_double_click =\n        !s_fast_idle_recording_hid_suppression_pending &&\n        voice_key_input_recovery_double_click_ready(button, now_tick);",
    ),
}


def main() -> int:
    failures: list[str] = []
    for relative_path, required_fragments in CHECKS.items():
        path = REPO_ROOT / relative_path
        source = path.read_text(encoding="utf-8")
        for fragment in required_fragments:
            if fragment not in source:
                failures.append(f"{relative_path}: missing {fragment!r}")

    for relative_path, required_sequences in ORDERED_PATHS.items():
        source = (REPO_ROOT / relative_path).read_text(encoding="utf-8")
        for sequence in required_sequences:
            if sequence not in source:
                failures.append(
                    f"{relative_path}: fast recording must queue before restoring power activity"
                )

    if failures:
        print("FAIL: EC11 fast Idle recording static contract")
        print("\n".join(failures))
        return 1

    print("PASS: EC11 fast Idle recording static contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
