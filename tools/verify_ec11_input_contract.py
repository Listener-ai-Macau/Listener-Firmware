from __future__ import annotations

import argparse
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


def require_in_order(source: str, tokens: list[str], message: str) -> None:
    cursor = -1
    for token in tokens:
        index = source.find(token, cursor + 1)
        if index == -1:
            fail(f"{message}: missing {token!r}")
        cursor = index


def verify_single_no_repair_log(path: Path) -> None:
    log = read(path)
    require_in_order(
        log,
        [
            "~KEY:GENERATED logical=EC11 gesture=single result=ESP_OK",
            "single click pending for double-click window",
            "confirmed single-click feedback",
            "single-click custom fallback queued",
        ],
        "EC11 generated single-click log must stay pending until confirmed single dispatch",
    )

    forbidden = [
        "~KEY:GENERATED logical=EC11 gesture=double",
        "recovery double-click accepted",
        "double-click recovery detected",
        "opening BLE re-pair window",
        "recovery_pairing_window_open",
        "forget_pairing_and_clear_session",
        "PairAsync",
    ]
    for token in forbidden:
        if token in log:
            fail(f"EC11 generated single-click log unexpectedly entered recovery/pairing path: {token}")

    for field in ["ble_repair_ms_left", "ble_repair_cue_ms_left"]:
        for match in re.finditer(rf"\b{field}=(\d+)\b", log):
            if int(match.group(1)) != 0:
                fail(f"EC11 generated single-click log reported {field}={match.group(1)}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--single-no-repair-log", type=Path)
    args = parser.parse_args()

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

    release_ready_body = extract_function(
        voice_key_input,
        "voice_key_input_recovery_double_click_ready",
    )
    if (
        "button->recovery_double_candidate ||" not in release_ready_body
        or "voice_key_input_recovery_double_gap_ready(button, now_tick)" not in release_ready_body
    ):
        fail("EC11 double-click release must re-check the 60 ms guard so fast real double-clicks do not fall through to single-click")

    short_release_body = extract_function(voice_key_input, "voice_key_input_handle_short_click_release")
    if "voice_key_input_recovery_double_click_ready(button, now_tick)" not in short_release_body:
        fail("EC11 short-click release must use the double-click release-ready helper")
    if (
        "!s_fast_idle_recording_hid_suppression_pending &&" not in short_release_body
        or "recovery double-click skipped" not in short_release_body
    ):
        fail(
            "EC11 fast idle recording must own its double-click window so a second press cannot cancel recording or open pairing"
        )

    checked_log = args.single_no_repair_log is not None
    if checked_log:
        verify_single_no_repair_log(args.single_no_repair_log)

    message = (
        "PASS: EC11 input contract keeps 500 ms timing parity with key1-key4, "
        "raw press only latches the gesture, confirmed single-click LED feedback is delayed, "
        "fast non-idle double-clicks re-check the 60 ms guard on release, "
        "fast idle recording owns its second press, and double-click BLE repair cue intact"
    )
    if checked_log:
        message += "; generated single-click log stayed out of BLE repair/pairing"
    print(message + ".")
    return 0


if __name__ == "__main__":
    sys.exit(main())
