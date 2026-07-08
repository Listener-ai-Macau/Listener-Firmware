from __future__ import annotations

import argparse
import sys
from pathlib import Path


def require(condition: bool, failures: list[str], message: str) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify the idle-resume recording custom-key HID path."
    )
    parser.add_argument("log", type=Path)
    args = parser.parse_args()

    text = args.log.read_text(encoding="utf-8", errors="replace")
    failures: list[str] = []

    for token in (
        "serial_opened port=COM3",
        "power_manager: activity reason=usb_control_line resumed from CONNECTED_IDLE",
        "power_manager: state CONNECTED_IDLE -> ACTIVE",
        "~KEY:STATUS custom_keys=KEY1:F13/F17/F21,KEY2:F14/F18/F22,KEY3:F15/F19/F23,KEY4:F16/F20/F24",
        "low_power_idle_backup_ms=20",
        "debounce_ms=20",
        "task_priority=7 audio_preempt_safe=1",
        "> ~KEY:PENDING:KEY3:SINGLE",
        "ble_hid: key3.gpio40.f15 pending HID usage queued for reconnect: usage=0x6A",
        "ble_hid: key3.gpio40.f15 pending HID usage self-test releasing transport block: usage=0x6A",
        "hid_keyboard: send_usage done usage=0x6A modifier=0x00",
        "keyboard: custom key pending transport test queued: logical=KEY3 source=key3.gpio40.f15 usage=F15 gesture=single",
        "~KEY:PENDING logical=KEY3 gesture=single result=ESP_OK",
        "recording_level_lock_wait_ms=0",
        "suspended_strip_resume_dirty=1",
    ):
        require(token in text, failures, f"missing idle-wake recording-key evidence: {token}")

    for forbidden in (
        "WRITE_ERROR",
        "READ_ERROR",
        "drop generated custom key event",
        "usage press failed",
        "usage release failed",
        "custom key gesture dropped",
        "ESP_ERR_TIMEOUT",
        "ESP_ERR_INVALID_ARG",
    ):
        require(forbidden not in text, failures, f"log contains forbidden token: {forbidden}")

    if failures:
        print("FAIL: idle-wake recording key log verification failed")
        for failure in failures:
            print(f" - {failure}")
        return 1

    print(
        "PASS: idle-resume KEY3 recording custom-key log proves the F15 pending HID "
        "path queues during transport-not-ready, drains after recovery, and keeps "
        "the low-power debounce/status contract visible."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
