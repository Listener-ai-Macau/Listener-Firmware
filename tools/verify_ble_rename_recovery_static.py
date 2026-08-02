from __future__ import annotations

import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


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
    gap = (
        REPO_ROOT / "ports" / "esp32" / "ble_hid_gap" / "ble_hid_gap_esp32.c"
    ).read_text(encoding="utf-8")
    worker = extract_function(gap, "ble_hid_gap_recovery_bond_delete_task")
    if not worker:
        print("FAIL: BLE rename recovery worker is missing")
        return 1

    ordered_tokens = [
        "ulTaskNotifyTake(",
        'ble_hid_gap_reconcile_connection_snapshot("bond_delete_disconnect_wait")',
        "if (remaining_conn.connected)",
        "ble_gap_terminate(",
        "ulTaskNotifyTake(",
        'ble_hid_gap_reconcile_connection_snapshot("bond_delete_disconnect_retry")',
        "if (remaining_conn.connected)",
        'status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_HARD, "ble_recovery_disconnect_timeout")',
    ]
    cursor = 0
    failures: list[str] = []
    for token in ordered_tokens:
        index = worker.find(token, cursor)
        if index < 0:
            failures.append(f"missing or out-of-order token: {token}")
            break
        cursor = index + len(token)

    if worker.count("BLE_HID_GAP_RECOVERY_BOND_DELETE_WAIT_MS") < 2:
        failures.append("disconnect recovery must retain two bounded wait windows")
    if worker.count("ble_recovery_disconnect_timeout") != 1:
        failures.append("disconnect timeout must have exactly one final hard-error site")
    if "DIAG_SEV_INFO,\n            15,\n            3," not in worker:
        failures.append("the first timeout/retry must remain informational, not a WARN/error")

    if failures:
        print("FAIL: BLE rename recovery static verification failed")
        for failure in failures:
            print(f" - {failure}")
        return 1

    print("PASS: BLE rename recovery uses controller reconciliation and two bounded waits")
    return 0


if __name__ == "__main__":
    sys.exit(main())
