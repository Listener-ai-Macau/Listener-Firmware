from __future__ import annotations

import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
KEYBOARD_C = REPO_ROOT / "components" / "keyboard" / "keyboard.c"
VOICE_KEY_INPUT_C = REPO_ROOT / "ports" / "esp32" / "voice_key_input" / "voice_key_input_esp32.c"


def fail(message: str) -> None:
    raise SystemExit(f"FAIL: {message}")


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def extract_function(source: str, name: str) -> str:
    pattern = r"static\s+[\w\s\*]+?\s+" + re.escape(name) + r"\s*\([^;]*?\)\s*\{"
    match = re.search(pattern, source, re.S)
    if not match:
        fail(f"missing {name}()")

    start = match.end() - 1
    depth = 0
    for index in range(start, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    fail(f"unterminated {name}()")
    return ""


def require_regex(source: str, pattern: str, message: str) -> None:
    if not re.search(pattern, source):
        fail(message)


def main() -> int:
    keyboard = read(KEYBOARD_C)
    voice_key_input = read(VOICE_KEY_INPUT_C)

    require_regex(
        keyboard,
        r"#define\s+KEYBOARD_CUSTOM_DOUBLE_CLICK_WINDOW_MS\s+500\b",
        "key1-key4 accepted double-click window must stay 500 ms",
    )
    require_regex(
        voice_key_input,
        r"#define\s+VOICE_KEY_INPUT_SINGLE_CLICK_DISPATCH_MS\s+\(500\)",
        "EC11 single-click dispatch delay must stay 500 ms to match key1-key4 double-click timing",
    )
    require_regex(
        voice_key_input,
        r"#define\s+VOICE_KEY_INPUT_RECOVERY_DOUBLE_CLICK_WINDOW_MS\s+\(500\)",
        "EC11 recovery double-click window must stay 500 ms",
    )
    require_regex(
        voice_key_input,
        r"#define\s+VOICE_KEY_INPUT_DOUBLE_CLICK_MIN_GAP_MS\s+\(60\)",
        "EC11 double-click minimum gap must keep contact-bounce protection",
    )

    dispatch_body = extract_function(voice_key_input, "voice_key_input_dispatch_custom_key_event")
    if "status_led_notify_ec11_feedback(STATUS_LED_EC11_FEEDBACK_PRESS)" in dispatch_body:
        fail("EC11 confirmed single dispatch must not replay the raw press LED cue")

    raw_feedback_body = extract_function(voice_key_input, "voice_key_input_apply_raw_feedback")
    if "status_led_notify_ec11_feedback(STATUS_LED_EC11_FEEDBACK_PRESS)" in raw_feedback_body:
        fail("EC11 raw press latch must not show the single-click LED cue before double-click can be ruled out")

    pending_single_body = extract_function(voice_key_input, "voice_key_input_dispatch_pending_single_click")
    if "status_led_notify_ec11_feedback(STATUS_LED_EC11_FEEDBACK_PRESS)" not in pending_single_body:
        fail("EC11 confirmed single-click dispatch must show the single-click LED cue after the double-click window expires")
    feedback_index = pending_single_body.find(
        "status_led_notify_ec11_feedback(STATUS_LED_EC11_FEEDBACK_PRESS)"
    )
    dispatch_index = pending_single_body.find("voice_key_input_dispatch_custom_key_event")
    if dispatch_index != -1 and feedback_index > dispatch_index:
        fail("EC11 confirmed single-click feedback must be emitted before forwarding the custom key event")

    recovery_body = extract_function(voice_key_input, "voice_key_input_record_recovery_event")
    if 'status_led_notify_ble_repairing("ec11_double_click_recovery")' not in recovery_body:
        fail("EC11 double-click recovery must keep the accepted BLE repairing light cue")

    print(
        "PASS: EC11 input contract keeps 500 ms timing parity with key1-key4, "
        "raw press only latches the gesture, confirmed single-click LED feedback is delayed, "
        "and double-click BLE repair cue intact."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
