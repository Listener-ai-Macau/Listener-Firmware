#!/usr/bin/env python3
"""Static contract check for the Listener BLE firmware OTA GATT bridge."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


SERVICE_UUID = "710af845-6d9f-6583-0c4d-9e5b3bc3092a"
CONTROL_UUID = "710af845-6d9f-6583-0c4d-9e5b3bc3092b"
DATA_UUID = "710af845-6d9f-6583-0c4d-9e5b3bc3092c"
CHUNK_BYTES = 180

UUID_BYTES = {
    "BLE_FIRMWARE_OTA_SERVICE_UUID": "0x2a, 0x09, 0xc3, 0x3b, 0x5b, 0x9e, 0x4d, 0x0c, 0x83, 0x65, 0x9f, 0x6d, 0x45, 0xf8, 0x0a, 0x71",
    "BLE_FIRMWARE_OTA_CONTROL_UUID": "0x2b, 0x09, 0xc3, 0x3b, 0x5b, 0x9e, 0x4d, 0x0c, 0x83, 0x65, 0x9f, 0x6d, 0x45, 0xf8, 0x0a, 0x71",
    "BLE_FIRMWARE_OTA_DATA_UUID": "0x2c, 0x09, 0xc3, 0x3b, 0x5b, 0x9e, 0x4d, 0x0c, 0x83, 0x65, 0x9f, 0x6d, 0x45, 0xf8, 0x0a, 0x71",
}


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except FileNotFoundError:
        raise AssertionError(f"missing file: {path}") from None


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def compact(text: str) -> str:
    return re.sub(r"\s+", " ", text)


def check_header(repo: Path) -> None:
    header = read_text(repo / "ports/esp32/ble_firmware_ota/include/ble_firmware_ota.h")
    for macro, byte_list in UUID_BYTES.items():
        match = re.search(rf"#define\s+{macro}\s+\\?\s*BLE_UUID128_INIT\(([^)]*)\)", header, re.MULTILINE)
        require(match is not None, f"{macro} is not defined")
        require(
            compact(match.group(1)) == compact(byte_list),
            f"{macro} does not match the canonical Listener OTA UUID bytes",
        )


def check_bridge(repo: Path) -> None:
    source = read_text(repo / "ports/esp32/ble_firmware_ota/ble_firmware_ota_esp32.c")
    required_tokens = [
        "firmware_ota_begin(",
        "firmware_ota_write(",
        "firmware_ota_finish(false)",
        "firmware_ota_abort(",
        "BLE_GATT_CHR_F_WRITE",
        "BLE_GATT_CHR_F_WRITE_NO_RSP",
        "ble_gatts_add_svcs",
        "DIAG_OTA_ABORT_BLE_CONTROL",
        "DIAG_OTA_ABORT_BLE_WRITE_FAIL",
        "DIAG_OTA_ABORT_BLE_DISCONNECT",
    ]
    for token in required_tokens:
        require(token in source, f"BLE OTA bridge is missing {token}")
    require(
        "status.expected_size != expected_size" in source and "firmware_ota_abort(DIAG_OTA_ABORT_BLE_CONTROL)" in source,
        "finish size mismatch must abort the OTA session",
    )


def check_integration(repo: Path) -> None:
    hid = read_text(repo / "ports/esp32/ble_hid/ble_hid.c")
    gap = read_text(repo / "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c")
    hid_cmake = read_text(repo / "ports/esp32/ble_hid/CMakeLists.txt")
    gap_cmake = read_text(repo / "ports/esp32/ble_hid_gap/CMakeLists.txt")
    diag = read_text(repo / "components/diag_log/include/diag_log_events.h")

    require("ble_firmware_ota_register_gatt()" in hid, "BLE HID init does not register firmware OTA GATT")
    require("ble_firmware_ota_log_gatt_state()" in hid, "BLE HID init does not log firmware OTA GATT state")
    require("ble_firmware_ota_on_gap_disconnect(" in gap, "GAP disconnect does not abort firmware OTA")
    require("ble_firmware_ota" in hid_cmake, "ble_hid component does not require ble_firmware_ota")
    require("ble_firmware_ota" in gap_cmake, "ble_hid_gap component does not require ble_firmware_ota")
    for token in (
        "DIAG_OTA_ABORT_BLE_CONTROL",
        "DIAG_OTA_ABORT_BLE_WRITE_FAIL",
        "DIAG_OTA_ABORT_BLE_DISCONNECT",
    ):
        require(token in diag, f"diag_log_events.h is missing {token}")


def candidate_desktop_contracts(repo: Path) -> list[Path]:
    listener_root = repo.parent
    return [
        listener_root / "Listener-Type-wt-tai-voice-keyboard-ota-update-1.2/src/lib/firmwareOta.ts",
        listener_root / "Listener-Type/src/lib/firmwareOta.ts",
    ]


def check_desktop_contract(path: Path) -> str:
    contract = read_text(path)
    expected_pairs = {
        SERVICE_UUID: "serviceUuid",
        CONTROL_UUID: "controlUuid",
        DATA_UUID: "dataUuid",
        str(CHUNK_BYTES): "chunkBytes",
    }
    for value, field in expected_pairs.items():
        require(value in contract, f"desktop contract {path} is missing {field}={value}")
    return str(path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path.cwd(), help="firmware repository root")
    parser.add_argument("--desktop-contract", type=Path, help="optional Listener-Type src/lib/firmwareOta.ts")
    args = parser.parse_args()

    repo = args.repo.resolve()
    try:
        check_header(repo)
        check_bridge(repo)
        check_integration(repo)

        desktop_checked = None
        if args.desktop_contract:
            desktop_checked = check_desktop_contract(args.desktop_contract.resolve())
        else:
            for candidate in candidate_desktop_contracts(repo):
                if candidate.exists():
                    desktop_checked = check_desktop_contract(candidate.resolve())
                    break

        if desktop_checked:
            print(f"PASS: BLE OTA GATT contract matches desktop contract at {desktop_checked}")
        else:
            print(
                "PASS: BLE OTA GATT contract matches canonical Listener OTA UUIDs "
                f"({SERVICE_UUID}, {CONTROL_UUID}, {DATA_UUID}) and {CHUNK_BYTES}-byte desktop chunks"
            )
        return 0
    except AssertionError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
