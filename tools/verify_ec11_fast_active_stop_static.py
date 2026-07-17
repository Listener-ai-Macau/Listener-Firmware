from __future__ import annotations

import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
VOICE_KEY_INPUT = REPO_ROOT / "ports" / "esp32" / "voice_key_input" / "voice_key_input_esp32.c"
VOICE_KEY_HEADER = REPO_ROOT / "ports" / "esp32" / "voice_key_input" / "include" / "voice_key_input.h"
VOICE_RECORDING_CONTROL = REPO_ROOT / "components" / "voice_recording_control" / "voice_recording_control.c"


def fail(message: str) -> None:
    raise SystemExit(f"FAIL: {message}")


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def extract_function(source: str, name: str) -> str:
    match = re.search(
        r"static\s+[\w\s\*]+?\s+" + re.escape(name) + r"\s*\([^;]*?\)\s*\{",
        source,
        re.S,
    )
    if not match:
        fail(f"missing {name}()")

    start = match.end() - 1
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    fail(f"unterminated {name}()")
    return ""


def require(source: str, token: str, message: str) -> None:
    if token not in source:
        fail(message)


def require_order(source: str, first: str, second: str, message: str) -> None:
    first_index = source.find(first)
    second_index = source.find(second)
    if first_index < 0 or second_index < 0 or first_index >= second_index:
        fail(message)


def main() -> int:
    voice_key = read(VOICE_KEY_INPUT)
    header = read(VOICE_KEY_HEADER)
    recording_control = read(VOICE_RECORDING_CONTROL)

    require(
        voice_key,
        "#define VOICE_KEY_INPUT_FAST_ACTIVE_RECORDING_STOP_TARGET_MS (50)",
        "EC11 active recording stop dispatch target must remain 50 ms",
    )
    for token in [
        "voice_key_input_take_fast_active_recording_stop_event",
        "voice_key_input_complete_fast_active_recording_stop_event",
    ]:
        require(header, token, f"voice key header must expose {token}")

    request = extract_function(voice_key, "voice_key_input_request_fast_active_recording_stop")
    for token in [
        "device_settings_get_ec11_fast_recording_enabled()",
        "s_recording_output_active",
        "ble_audio_stream_is_type_link_ready()",
        "xSemaphoreGive(s_fast_active_recording_stop_event_sem)",
        "s_fast_active_recording_stop_hid_suppression_pending = true",
        "voice_key_input_notify_recording_control_task()",
        "fast active recording stop queued",
    ]:
        require(request, token, f"fast active stop request missing {token}")

    sample = extract_function(voice_key, "voice_key_input_handle_button_sample")
    require_order(
        sample,
        'voice_key_input_request_fast_active_recording_stop(button, "raw_edge")',
        'voice_key_input_apply_raw_feedback(button, "raw_edge")',
        "raw EC11 active stop must queue recording control before only showing key feedback",
    )

    pending = extract_function(voice_key, "voice_key_input_dispatch_pending_single_click")
    active_block_start = pending.find("if (s_fast_active_recording_stop_hid_suppression_pending)")
    active_block_end = pending.find("status_led_notify_ec11_feedback", active_block_start)
    if active_block_start < 0 or active_block_end < 0:
        fail("fast active stop must suppress the delayed fallback HID")
    active_block = pending[active_block_start:active_block_end]
    if "voice_key_input_dispatch_custom_key_event" in active_block:
        fail("fast active stop must not forward its delayed fallback HID")

    recovery = extract_function(voice_key, "voice_key_input_record_recovery_event")
    require(
        recovery,
        "s_fast_active_recording_stop_hid_suppression_pending = false",
        "second-click recovery must stay reachable after a fast active stop",
    )
    require(
        recovery,
        "s_fast_active_recording_stop_press_tick = 0",
        "second-click recovery must clear the fast active stop timing latch",
    )
    require(
        voice_key,
        "s_fast_active_recording_stop_hid_suppression_pending = false;\n            s_fast_active_recording_stop_press_tick = 0;\n            voice_key_input_clear_raw_feedback(button);",
        "long press must not leave a delayed fast active stop HID suppression behind",
    )

    controller = extract_function(recording_control, "voice_recording_control_handle_fast_active_ec11_stop")
    for token in [
        'voice_recording_control_toggle("ec11.fast_active_stop")',
        "state_before == VOICE_RECORDING_STATE_RECORDING",
        "s_state == VOICE_RECORDING_STATE_TRANSFERRING",
        "voice_key_input_complete_fast_active_recording_stop_event(suppress_fallback_hid)",
        "EC11 fast active recording stop dispatch: press_to_control_ms=",
        "target_ms=50",
    ]:
        require(controller, token, f"fast active stop controller missing {token}")

    task = extract_function(recording_control, "voice_recording_control_task")
    require_order(
        task,
        "voice_key_input_take_fast_active_recording_stop_event(&press_to_control_ms)",
        "voice_key_input_take_toggle_event()",
        "fast active stop must dispatch before any delayed generic toggle path",
    )

    print(
        "PASS: EC11 active recording stop queues from the raw edge to recording control "
        "within the 50 ms firmware target, suppresses only its delayed HID fallback, "
        "and preserves second-click recovery plus long-press cleanup."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
