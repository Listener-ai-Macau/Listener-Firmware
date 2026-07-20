#!/usr/bin/env python3
"""Guard the bounded serial-only idle setup used by machine diagnostics."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parent.parent
POWER_MANAGER = (ROOT / "components" / "power_manager" / "power_manager.c").read_text(
    encoding="utf-8"
)
BLE_HID = (ROOT / "ports" / "esp32" / "ble_hid" / "ble_hid.c").read_text(
    encoding="utf-8"
)

failures: list[str] = []

helper = re.search(
    r"static void power_manager_request_test_idle\(void\)\n\{(?P<body>[\s\S]*?)\n\}\n\nbool power_manager_consume_usb_command",
    POWER_MANAGER,
)
if helper is None:
    failures.append("missing power_manager_request_test_idle helper")
else:
    body = helper.group("body")
    for token in (
        "external_power_required",
        "ble_connection_required",
        "power_manager_awake_blockers(blockers)",
        "power_manager_plugged_low_power_enabled()",
        "s_test_idle_override_active = true",
        "next = POWER_MANAGER_STATE_CONNECTED_IDLE",
        "power_manager_apply_state(previous, next, blockers)",
        "~POWER:TEST:IDLE result=%s",
    ):
        if token not in body:
            failures.append(f"test-idle helper missing {token}")
    if "power_manager_record_activity" in body:
        failures.append("test-idle helper must not synthesize activity before forcing idle")

if not re.search(
    r"void power_manager_record_activity\(const char \*reason\)[\s\S]*"
    r"s_last_radio_activity_ms\s*=\s*now_ms;[\s\S]*"
    r"s_test_idle_override_active\s*=\s*false;",
    POWER_MANAGER,
):
    failures.append("ordinary activity must clear the RAM-only test-idle override")

if not re.search(
    r"strcmp\(command, \"TEST:IDLE\"\) == 0\) \{\s*"
    r"power_manager_request_test_idle\(\);\s*return true;\s*\}",
    POWER_MANAGER,
):
    failures.append("TEST:IDLE command is not routed to the bounded idle helper")

activity = re.search(
    r"static bool ble_hid_usb_command_records_activity\(const char \*line\)\n\{"
    r"(?P<body>[\s\S]*?)\n\}\n\nstatic bool ble_hid_dispatch_usb_command_line",
    BLE_HID,
)
if activity is None:
    failures.append("missing USB activity classifier")
elif "POWER:TEST:IDLE" in activity.group("body"):
    failures.append("TEST:IDLE must not reset idle clocks in the USB activity classifier")

if failures:
    print("FAIL: test idle control static verification failed")
    for failure in failures:
        print(f" - {failure}")
    sys.exit(1)

print("PASS: test-only idle control enters the existing connected-idle path without timer waiting")
