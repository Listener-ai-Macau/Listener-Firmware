#!/usr/bin/env python3
"""Static guard for the BLE diagnostic-log GATT export contract."""

from __future__ import annotations

import pathlib
import sys


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_text(relative_path: str) -> str:
    return (REPO_ROOT / relative_path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    diag_header = read_text("ports/esp32/ble_diag_log/include/ble_diag_log.h")
    diag_service = read_text("ports/esp32/ble_diag_log/ble_diag_log_esp32.c")
    gap = read_text("ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c")
    hid = read_text("ports/esp32/ble_hid/ble_hid.c")
    diag_log_header = read_text("components/diag_log/include/diag_log.h")
    diag_platform = read_text("ports/esp32/diag_log_platform/diag_log_flash.c")

    require("BLE_DIAG_LOG_SERVICE_UUID" in diag_header, "diagnostic service UUID is missing")
    require("BLE_DIAG_LOG_CONTROL_UUID" in diag_header, "diagnostic control UUID is missing")
    require("BLE_DIAG_LOG_DATA_UUID" in diag_header, "diagnostic data UUID is missing")
    require("BLE_DIAG_LOG_COUNT_UUID" in diag_header, "diagnostic count UUID is missing")
    require("ble_diag_log_register_gatt()" in hid, "BLE HID init must register diagnostic GATT")
    require("ble_diag_log_log_gatt_state()" in hid, "BLE HID init must log diagnostic GATT handles")
    require('strcmp(line, "~DIAG:GATT")' in hid, "serial ~DIAG:GATT must log diagnostic GATT handles")

    require("void ble_diag_log_on_gap_connect" in diag_header, "diagnostic GAP connect hook is not public")
    require(
        "ble_hid_gap_handle_connect_established(event->connect.conn_handle" in gap
        and "ble_diag_log_on_gap_connect(conn_handle)" in gap,
        "GAP connect must notify diagnostic export through the shared connect-established helper",
    )
    require("ble_diag_log_on_gap_mtu(event->mtu.conn_handle, event->mtu.value)" in gap, "GAP MTU must notify diagnostic export")
    require(
        "ble_hid_gap_handle_disconnect(\n            event->disconnect.conn.conn_handle" in gap
        and "ble_diag_log_on_gap_disconnect(conn_handle)" in gap,
        "GAP disconnect must abort diagnostic export with the real connection handle through the shared disconnect helper",
    )
    require(
        'BLE_HID_GAP_GATT_SCHEMA_REV "ota_v1"' in gap,
        "adding or reshaping GATT services must bump the schema rev so bonded Windows hosts refresh cached services",
    )

    require("ble_att_mtu(conn_handle)" in diag_service, "diagnostic export must query the current ATT MTU")
    require("BLE_DIAG_LOG_ATT_HEADER_BYTES 3U" in diag_service, "diagnostic export must account for ATT opcode+handle bytes")
    require("BLE_DIAG_LOG_CHUNK_HEADER_BYTES 10U" in diag_service, "diagnostic chunk header must carry a 32-bit export offset")
    require("uint32_t global_offset" in diag_service, "diagnostic chunk offset must not truncate after 65535 events")
    require("s_att_value_max_bytes" in diag_service, "diagnostic export must track ATT value payload bytes")
    require(
        "data_len > s_att_value_max_bytes" in diag_service,
        "diagnostic chunks must be capped to negotiated ATT value size",
    )
    require(
        "BLE_DIAG_LOG_MAX_EVENTS_PER_CHUNK 4U" in diag_service,
        "diagnostic chunks must stay small enough to coexist with BLE audio notifications",
    )
    require(
        "BLE_DIAG_LOG_DEFAULT_MTU" not in diag_service,
        "diagnostic export must not keep a high hard-coded default MTU",
    )

    require("DIAG_LOG_EVENT_WIRE_BYTES 24U" in diag_log_header, "diag_log wire event size must be explicit")
    require(
        "_Static_assert(DIAG_EVENT_SIZE == DIAG_LOG_EVENT_WIRE_BYTES" in diag_platform,
        "diag_log platform must assert the BLE wire event size",
    )
    require(
        "power_manager_set_blocker(POWER_MANAGER_BLOCKER_DIAG_EXPORT, true)" in diag_service
        and "power_manager_set_blocker(POWER_MANAGER_BLOCKER_DIAG_EXPORT, false)" in diag_service,
        "diagnostic export must bracket sessions with the power-manager blocker",
    )

    require("ble_audio_stream_on_gap_connect" in gap, "audio GAP connect path is missing")
    require("ble_audio_stream_on_gap_mtu" in gap, "audio GAP MTU path is missing")
    require("ble_audio_stream_on_gap_disconnect" in gap, "audio GAP disconnect path is missing")

    print("PASS: BLE diagnostic log GATT contract covers MTU-safe chunking and GAP integration.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
