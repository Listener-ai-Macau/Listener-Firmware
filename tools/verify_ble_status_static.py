from __future__ import annotations

import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (REPO_ROOT / relative_path).read_text(encoding="utf-8")


def extract_function(source: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^)]*\)\s*\{{", source)
    if not match:
        return ""
    depth = 0
    for index in range(match.end() - 1, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[match.start() : index + 1]
    return ""


def main() -> int:
    failures: list[str] = []
    ble_hid = read("ports/esp32/ble_hid/ble_hid.c")
    ble_gap = read("ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c")
    ble_gap_header = read("ports/esp32/ble_hid_gap/include/ble_hid_gap.h")

    passive_query = extract_function(ble_hid, "ble_hid_usb_command_is_passive_query")
    if "BLE:STATUS" not in passive_query:
        failures.append("ble_hid.c: BLE:STATUS must be listed as a passive USB query")

    records_activity = extract_function(ble_hid, "ble_hid_usb_command_records_activity")
    if not re.search(
        r"ble_hid_usb_command_is_passive_query\(line\)[\s\S]{0,120}return\s+false",
        records_activity,
    ):
        failures.append("ble_hid.c: passive USB queries must bypass power activity recording")

    dispatch = extract_function(ble_hid, "ble_hid_dispatch_usb_command_line")
    if not re.search(
        r"BLE:STATUS[\s\S]{0,180}ble_hid_gap_print_status\(\)[\s\S]{0,80}return\s+true",
        dispatch,
    ):
        failures.append("ble_hid.c: BLE:STATUS must dispatch to ble_hid_gap_print_status")

    if "void ble_hid_gap_print_status(void);" not in ble_gap_header:
        failures.append("ble_hid_gap.h: ble_hid_gap_print_status must be public")

    status_fn = extract_function(ble_gap, "ble_hid_gap_print_status")
    if not status_fn:
        failures.append("ble_hid_gap_esp32.c: missing ble_hid_gap_print_status")
    else:
        required_tokens = [
            "ble_hid_gap_connection_snapshot()",
            "ble_gap_conn_find",
            "active_applied",
            "~BLE:STATUS",
            "active_required",
            "e11r",
            "last_conn_param_mode",
        ]
        for token in required_tokens:
            if token not in status_fn:
                failures.append(f"ble_hid_gap_esp32.c: BLE status printer missing {token}")
        if "ble_hid_gap_reconcile_connection_snapshot" in status_fn:
            failures.append("ble_hid_gap_esp32.c: BLE status printer must not reconcile or mutate connection state")

    if failures:
        print("FAIL: BLE status static verification failed")
        for failure in failures:
            print(f" - {failure}")
        return 1

    print("PASS: BLE status static verification")
    return 0


if __name__ == "__main__":
    sys.exit(main())
