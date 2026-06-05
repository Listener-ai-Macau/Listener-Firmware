from __future__ import annotations

import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


CHECKS = {
    "ports/esp32/board_pins/include/board_pins.h": [
        r"BOARD_PINS_RGB_STATUS_IO\s+\(GPIO_NUM_1\)",
        r"BOARD_PINS_RGB_EC11_IO\s+\(GPIO_NUM_5\)",
        r"BOARD_PINS_RGB_KEY_IO\s+\(GPIO_NUM_13\)",
        r"BOARD_PINS_RGB_EDGE_IO\s+\(GPIO_NUM_4\)",
        r"BOARD_PINS_BAT_CHG_IO\s+\(GPIO_NUM_14\)",
    ],
    "components/status_led/include/status_led.h": [
        "status_led_init",
        "status_led_start",
        "status_led_consume_usb_command",
        "status_led_set_ble_state",
        "status_led_set_recording",
        "status_led_set_processing",
        "status_led_prepare_sleep",
        "STATUS_LED_REC_SOURCE_DEVICE_MIC",
        "STATUS_LED_REC_SOURCE_DESKTOP_MIC",
        "STATUS_LED_REC_SOURCE_NOT_AVAILABLE",
        "STATUS_LED_ERROR_DOMAIN_OTA",
    ],
    "components/status_led/status_led.c": [
        "STATUS_LED_STATUS_COUNT 6",
        "STATUS_LED_EC11_COUNT 12",
        "STATUS_LED_KEY_COUNT 4",
        "STATUS_LED_EDGE_COUNT 6",
        "STATUS_LED_STRIP_COUNT 4",
        "STATUS_LED_STRIP_STATUS",
        "STATUS_LED_STRIP_EC11",
        "STATUS_LED_STRIP_KEY",
        "STATUS_LED_STRIP_EDGE",
        "STATUS_LED_PROFILE_OFF",
        "STATUS_LED_PROFILE_LOW",
        "STATUS_LED_PROFILE_STANDARD",
        "STATUS_LED_PROFILE_AMBIENT",
        "STATUS_LED_PROFILE_FACTORY",
        "STATUS_LED_COLOR_ORDER_GRB",
        "STATUS_LED_COLOR_ORDER_RGB",
        "status_led_new_ws2812_encoder",
        "RMT_CLK_SRC_DEFAULT",
        "STATUS_LED_RMT_RESOLUTION_HZ 10000000U",
        "STATUS_LED_WS2812_RESET_TICKS 250U",
        "~LED:STATUS",
        "~LED:BUDGET",
        "~LED:PRIVACY",
        "TEST:RGBW",
        "TEST:MAP",
        "ec11_order=LED7..LED10+LED15..LED16+LED23..LED28",
        "PREVIEW ",
        "ERROR ",
        "PROFILE ",
        "PWM_RGB_EC11_GPIO5",
        "PWM_RGB_KEY_GPIO13",
        "gpio14_reserved=BAT_CHG_IO",
        "vdd_led_enable=always_on_assumed",
        "semantic_order=LED1:PWR,LED2:BLE,LED3:REC,LED4:AI,LED5:OK,LED6:WARN",
        "rec_not_available_shows=WARN+REC",
        "status_led_profile_budget_ma_locked",
        "status_led_estimate_current_ma",
        "status_led_clamp_current_locked",
        "status_led_render_error_locked",
        "status_led_render_recording_locked",
        "status_led_render_processing_locked",
        "status_led_render_edge_locked",
    ],
    "components/diag_log/include/diag_log_events.h": [
        "DIAG_SRC_STATUS_LED",
        "DIAG_LED_STATE",
        "DIAG_LED_ERROR",
        "DIAG_LED_OUTPUT_FAIL",
        "DIAG_LED_PROFILE",
    ],
    "main/main.c": [
        "status_led_init()",
        "status_led_start()",
    ],
    "ports/esp32/ble_hid/ble_hid.c": [
        "status_led_consume_usb_command(line)",
        "status_led_set_ble_state(STATUS_LED_BLE_CONNECTED, true)",
        "status_led_set_ble_state(STATUS_LED_BLE_RECONNECTING, false)",
    ],
    "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c": [
        "status_led_set_ble_state(STATUS_LED_BLE_PAIRING, false)",
        "status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE",
        "status_led_clear_error(STATUS_LED_ERROR_DOMAIN_BLE)",
    ],
    "components/keyboard/keyboard.c": [
        "status_led_notify_key_event",
        "status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE",
    ],
    "components/voice_recording_control/voice_recording_control.c": [
        "status_led_set_recording(true, STATUS_LED_REC_SOURCE_DEVICE_MIC)",
        "status_led_set_recording(false, STATUS_LED_REC_SOURCE_NOT_AVAILABLE)",
        "status_led_set_processing(true, \"audio_session_finishing\")",
        "status_led_notify_success(\"recording_session_finished\")",
    ],
    "components/firmware_ota/firmware_ota.c": [
        "status_led_set_processing(true, \"ota_begin\")",
        "status_led_set_processing(false, \"ota_finish\")",
        "status_led_set_error(STATUS_LED_ERROR_DOMAIN_OTA",
    ],
    "components/power_manager/power_manager.c": [
        "status_led_prepare_sleep",
        "status_led_set_low_power_disabled",
    ],
    "docs/features/status_led.md": [
        "GPIO5",
        "GPIO13",
        "`GPIO14` is reserved for `BAT_CHG_IO`",
        "`LED1=PWR`",
        "`LED6=WARN`",
        "`~LED:TEST:RGBW <status|ec11|knob|ring|key|edge|all>`",
    ],
}


def read(relative: str) -> str:
    path = REPO_ROOT / relative
    if not path.is_file():
        raise FileNotFoundError(f"missing file: {relative}")
    return path.read_text(encoding="utf-8")


def main() -> int:
    failures: list[str] = []
    for relative, tokens in CHECKS.items():
        try:
            text = read(relative)
        except FileNotFoundError as exc:
            failures.append(str(exc))
            continue
        for token in tokens:
            if token.startswith("r\""):
                raise AssertionError("internal verifier token format error")
            if token.startswith("^"):
                if not re.search(token, text, re.MULTILINE):
                    failures.append(f"{relative}: missing pattern {token!r}")
            elif "\\" in token and re.search(r"\\[sSdDwW]", token):
                if not re.search(token, text, re.MULTILINE):
                    failures.append(f"{relative}: missing pattern {token!r}")
            elif token not in text:
                failures.append(f"{relative}: missing token {token!r}")

    board = read("ports/esp32/board_pins/include/board_pins.h")
    if re.search(r"BOARD_PINS_RGB_KEY_IO\s+\(GPIO_NUM_14\)", board):
        failures.append("board_pins.h: RGB key strip must not use GPIO14; GPIO14 is BAT_CHG_IO")
    if re.search(r"BOARD_PINS_RGB_EC11_IO\s+\(GPIO_NUM_4\)", board):
        failures.append("board_pins.h: EC11 knob ring must use GPIO5, not the edge GPIO4 strip")

    status_led = read("components/status_led/status_led.c")
    if "bit-bang" in status_led.lower():
        failures.append("status_led.c: do not bit-bang WS2812 timing")
    if "STATUS_LED_EC11_COUNT 4" in status_led:
        failures.append("status_led.c: stale EC11 four-LED strip count")
    if "STATUS_LED_EDGE_COUNT 14" in status_led:
        failures.append("status_led.c: stale edge/frame fourteen-LED strip count")

    board_leds = read("components/board/board.c")
    if 'key="LED7..LED10" edge="LED11..LED16"' in board_leds:
        failures.append("board.c: stale three-zone LED map")
    if "LED15..LED28" in board_leds and "LED23..LED28" not in board_leds:
        failures.append("board.c: stale edge LED15..LED28 map")

    if failures:
        print("FAIL: status LED static verification failed")
        for failure in failures:
            print(f" - {failure}")
        return 1

    print(
        "PASS: status LED static verification covers V2 four-zone WS2812 resources, "
        "EC11 GPIO5/count12, key GPIO13/count4, edge GPIO4/count6, diagnostics, "
        "USB validation hooks, and low-power off path."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
