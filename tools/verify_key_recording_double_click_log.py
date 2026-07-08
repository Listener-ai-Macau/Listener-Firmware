from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


KEYS = (
    ("KEY3", "key3.gpio40.f19", "F19", "0x6E", "key3.gpio40.f15", "F15"),
    ("KEY4", "key4.gpio41.f20", "F20", "0x6F", "key4.gpio41.f16", "F16"),
)


def require(condition: bool, failures: list[str], message: str) -> None:
    if not condition:
        failures.append(message)


def section_for(text: str, command: str) -> str:
    marker = f"> {command}"
    start = text.find(marker)
    if start < 0:
        return ""
    next_command = text.find("\n> ", start + len(marker))
    return text[start:] if next_command < 0 else text[start:next_command]


def key_rgb_lines(text: str) -> list[str]:
    return [
        line
        for line in text.splitlines()
        if "~LED:STATUS detail=rgb_key" in line and "key_rgb=" in line
    ]


def multiple_key_pixels_lit(line: str) -> bool:
    lit = 0
    for match in re.finditer(r"px\d+:(\d+),(\d+),(\d+)", line):
        rgb = tuple(int(part) for part in match.groups())
        if rgb != (0, 0, 0):
            lit += 1
    return lit > 1


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify KEY double-clicks stay double-clicks while recording/REC level LED load is active."
    )
    parser.add_argument("log", type=Path)
    args = parser.parse_args()

    text = args.log.read_text(encoding="utf-8", errors="replace")
    failures: list[str] = []

    require("serial_opened port=COM3" in text, failures, "log must be captured from COM3")
    require(
        "~VREC:TOGGLE" in text or "~LED:PREVIEW recording_processing" in text,
        failures,
        "recording double-click log must enter the Type/recording path or its LED preview equivalent",
    )
    require(
        "recording start source=ble_audio_control" in text
        or "recording start source=usb" in text
        or "~LED:PREVIEW recording_processing" in text,
        failures,
        "recording state evidence missing",
    )
    require(
        "recording_level_lock_wait_ms=0" in text,
        failures,
        "LED contract must prove live recording levels do not block on the LED mutex",
    )
    require(
        "task_priority=7 audio_preempt_safe=1" in text,
        failures,
        "~KEY:STATUS or startup log must prove KEY scan priority stays above the audio task",
    )
    require("~KEY:STATUS custom_keys=KEY1:F13/F17/F21" in text, failures, "missing ~KEY:STATUS diagnostic readback")

    forbidden_global = (
        "WRITE_ERROR",
        "READ_ERROR",
        "drop generated custom key event",
        "usage press failed",
        "usage release failed",
        "immediate HID usage dispatch failed",
        "custom key gesture dropped",
        "ESP_ERR_TIMEOUT",
        "ESP_ERR_INVALID_STATE",
        "ESP_ERR_INVALID_ARG",
    )
    for token in forbidden_global:
        require(token not in text, failures, f"log contains forbidden token: {token}")

    for logical, double_source, double_usage, double_hex, single_source, single_usage in KEYS:
        section = section_for(text, f"~KEY:{logical}:DOUBLE")
        require(section, failures, f"missing generated double command section for {logical}")
        if not section:
            continue
        required = (
            f"~KEY:GENERATED logical={logical} gesture=double result=ESP_OK",
            f"custom key generated gesture armed: logical={logical} source={single_source} gesture=double",
            f"custom key generated gesture completed: logical={logical} source={single_source} gesture=double",
            f"custom key gesture queued: logical={logical} source={double_source} usage={double_usage} gesture=double",
            f"ble_hid: {double_source} HID usage queued: usage={double_hex}",
            f"hid_keyboard: send_usage done usage={double_hex}",
        )
        for token in required:
            require(token in section, failures, f"{logical} double section missing evidence: {token}")
        forbidden_single = (
            f"custom key fallback queued: logical={logical} source={single_source} usage={single_usage} gesture=single",
            f"ble_hid: {single_source} HID usage queued",
            f"hid_keyboard: send_usage done usage={'0x6A' if logical == 'KEY3' else '0x6B'}",
        )
        for token in forbidden_single:
            require(token not in section, failures, f"{logical} double section fell back to single: {token}")

    for line in key_rgb_lines(text):
        require(not multiple_key_pixels_lit(line), failures, f"recording double-click leaked multiple key LEDs: {line}")

    if failures:
        print("FAIL: recording double-click log verification failed")
        for failure in failures:
            print(f" - {failure}")
        return 1

    print(
        "PASS: recording-load KEY3/KEY4 double-clicks remained double-click HID gestures, "
        "did not fall through to single-click fallback, and key RGB samples stayed isolated."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
