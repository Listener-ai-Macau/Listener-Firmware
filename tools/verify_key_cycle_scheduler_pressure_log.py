from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


KEYS = (
    ("KEY1", "key1.gpio38.f13", "F13", "0x68"),
    ("KEY2", "key2.gpio39.f14", "F14", "0x69"),
    ("KEY3", "key3.gpio40.f15", "F15", "0x6A"),
    ("KEY4", "key4.gpio41.f16", "F16", "0x6B"),
)


def require(condition: bool, failures: list[str], message: str) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Verify four-key generated-cycle pressure does not let scheduler "
            "delay turn short singles into double/long gestures."
        )
    )
    parser.add_argument("log", type=Path)
    parser.add_argument("--min-singles-per-key", type=int, default=6)
    args = parser.parse_args()

    text = args.log.read_text(encoding="utf-8", errors="replace")
    failures: list[str] = []

    require("serial_opened port=COM3" in text, failures, "pressure log must be captured from COM3")
    require("ble_audio" in text or "audio_capture" in text, failures, "pressure log must include BLE/audio load")

    forbidden = (
        "WRITE_ERROR",
        "READ_ERROR",
        "drop generated custom key event",
        "raw-only short transition ignored",
        "gesture=double",
        "gesture=long",
        "usage=F17 gesture=double",
        "usage=F18 gesture=double",
        "usage=F19 gesture=double",
        "usage=F20 gesture=double",
        "usage=F21 gesture=long",
        "usage=F22 gesture=long",
        "usage=F23 gesture=long",
        "usage=F24 gesture=long",
        "ESP_ERR_TIMEOUT",
        "ESP_ERR_INVALID_STATE",
        "ESP_ERR_INVALID_ARG",
        "usage press failed",
        "usage release failed",
        "immediate HID usage dispatch failed",
        "custom key gesture dropped",
    )
    for token in forbidden:
        require(token not in text, failures, f"pressure log contains forbidden token: {token}")

    raw_duration_lines = [
        line
        for line in text.splitlines()
        if "custom key raw-duration tap accepted:" in line
    ]
    for line in raw_duration_lines:
        require(
            "synthetic_press_ms=20" in line,
            failures,
            f"raw-duration fallback must cap synthetic press duration to 20 ms: {line}",
        )

    for logical, source, usage_name, usage_hex in KEYS:
        require(
            text.count(f"~KEY:GENERATED logical={logical} gesture=single result=ESP_OK")
            >= args.min_singles_per_key,
            failures,
            f"{logical} pressure log missing generated single acknowledgements",
        )
        require(
            text.count(f"custom key raw debounce candidate: logical={logical}") >= args.min_singles_per_key,
            failures,
            f"{logical} pressure log missing local raw white previews",
        )
        require(
            text.count(f"custom key single pending: logical={logical}") >= args.min_singles_per_key,
            failures,
            f"{logical} pressure log missing immediate single feedback decisions",
        )
        require(
            text.count(
                f"custom key fallback queued: logical={logical} source={source} usage={usage_name} gesture=single"
            )
            >= args.min_singles_per_key,
            failures,
            f"{logical} pressure log missing single fallback dispatches",
        )
        require(
            text.count(f"ble_hid: {source} HID usage queued: usage={usage_hex}") >= args.min_singles_per_key,
            failures,
            f"{logical} pressure log missing BLE HID queued handoff evidence",
        )

    command_count = len(re.findall(r"^> ~KEY:KEY[1-4]:SINGLE$", text, flags=re.MULTILINE))
    require(
        command_count >= args.min_singles_per_key * len(KEYS),
        failures,
        f"pressure log must include at least {args.min_singles_per_key * len(KEYS)} generated commands",
    )

    if failures:
        print("FAIL: key cycle scheduler pressure verification failed")
        for failure in failures:
            print(f" - {failure}")
        return 1

    print(
        "PASS: key cycle scheduler pressure kept generated KEY1-KEY4 singles from "
        "turning into double/long gestures, with raw-duration fallback capped to "
        "a 20 ms synthetic short tap."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
