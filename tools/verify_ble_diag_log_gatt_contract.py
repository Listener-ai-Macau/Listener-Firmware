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
    diag_cmake = read_text("ports/esp32/ble_diag_log/CMakeLists.txt")
    platform_generated = read_text(
        "third_party/denzic-platform/observability/embedded/c/include/denzic_observability_v1_generated.h"
    )
    platform_codec = read_text(
        "third_party/denzic-platform/observability/embedded/c/include/denzic_diag_log_gatt_v1.h"
    )
    gap = read_text("ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c")
    hid = read_text("ports/esp32/ble_hid/ble_hid.c")
    diag_log_header = read_text("components/diag_log/include/diag_log.h")
    diag_platform = read_text(
        "third_party/denzic-platform/observability/embedded/c/include/denzic_diag_log_store.h"
    )

    require("BLE_DIAG_LOG_SERVICE_UUID" in diag_header, "diagnostic service UUID is missing")
    require("BLE_DIAG_LOG_CONTROL_UUID" in diag_header, "diagnostic control UUID is missing")
    require("BLE_DIAG_LOG_DATA_UUID" in diag_header, "diagnostic data UUID is missing")
    require("BLE_DIAG_LOG_COUNT_UUID" in diag_header, "diagnostic count UUID is missing")
    require(
        '#include "denzic_observability_v1_generated.h"' in diag_header,
        "diagnostic GATT adapter must include the shared generated UUID contract",
    )
    for local_macro, shared_macro in (
        ("BLE_DIAG_LOG_SERVICE_UUID", "DENZIC_OBSERVABILITY_V1_DIAG_LOG_GATT_SERVICE_UUID_BYTES"),
        ("BLE_DIAG_LOG_CONTROL_UUID", "DENZIC_OBSERVABILITY_V1_DIAG_LOG_GATT_CONTROL_UUID_BYTES"),
        ("BLE_DIAG_LOG_DATA_UUID", "DENZIC_OBSERVABILITY_V1_DIAG_LOG_GATT_DATA_UUID_BYTES"),
        ("BLE_DIAG_LOG_COUNT_UUID", "DENZIC_OBSERVABILITY_V1_DIAG_LOG_GATT_COUNT_UUID_BYTES"),
    ):
        require(
            f"BLE_UUID128_INIT({shared_macro})" in diag_header,
            f"{local_macro} must reference {shared_macro}",
        )
    for token in (
        '#define DENZIC_OBSERVABILITY_V1_DIAG_LOG_GATT_SERVICE_UUID_TEXT "710af845-6d9f-6583-0c4d-9e5b3bc3093a"',
        '#define DENZIC_OBSERVABILITY_V1_DIAG_LOG_GATT_CONTROL_UUID_TEXT "710af845-6d9f-6583-0c4d-9e5b3bc3093b"',
        '#define DENZIC_OBSERVABILITY_V1_DIAG_LOG_GATT_DATA_UUID_TEXT "710af845-6d9f-6583-0c4d-9e5b3bc3093c"',
        '#define DENZIC_OBSERVABILITY_V1_DIAG_LOG_GATT_COUNT_UUID_TEXT "710af845-6d9f-6583-0c4d-9e5b3bc3093d"',
        "#define DENZIC_OBSERVABILITY_V1_DIAG_LOG_EVENT_WIRE_BYTES (24u)",
        "#define DENZIC_OBSERVABILITY_V1_DIAG_LOG_CHUNK_HEADER_BYTES (10u)",
    ):
        require(token in platform_generated, f"shared generated diagnostic GATT contract is missing {token}")
    require(
        "uint32_t global_offset" in platform_codec,
        "platform diagnostic chunk codec must keep a 32-bit export offset",
    )
    require(
        "third_party/denzic-platform/observability/embedded/c/src/denzic_diag_log_gatt_v1.c" in diag_cmake
        and "third_party/denzic-platform/observability/embedded/c/include" in diag_cmake,
        "BLE diagnostic component must compile the shared platform chunk codec",
    )
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
        'BLE_HID_GAP_GATT_SCHEMA_REV "denzic_ota_v6_ota_control_cache_refresh"' in gap,
        "adding or reshaping GATT services must bump the schema rev so bonded Windows hosts refresh cached services",
    )
    require(
        "ble_hid_gap_reconnect_after_service_changed(event->notify_tx.conn_handle)" in gap,
        "confirmed GATT service changes must reconnect the bonded host before it reopens services",
    )

    require("ble_att_mtu(conn_handle)" in diag_service, "diagnostic export must query the current ATT MTU")
    require("BLE_DIAG_LOG_ATT_HEADER_BYTES 3U" in diag_service, "diagnostic export must account for ATT opcode+handle bytes")
    require(
        "BLE_DIAG_LOG_CHUNK_HEADER_BYTES DENZIC_OBSERVABILITY_V1_DIAG_LOG_CHUNK_HEADER_BYTES" in diag_service,
        "diagnostic chunk header size must come from the platform contract",
    )
    require(
        "BLE_DIAG_LOG_EVENT_BYTES DENZIC_OBSERVABILITY_V1_DIAG_LOG_EVENT_WIRE_BYTES" in diag_service,
        "diagnostic event wire size must come from the platform contract",
    )
    require(
        "denzic_diag_log_gatt_v1_encode_chunk_header(" in diag_service,
        "diagnostic export must encode chunk headers with the platform codec (32-bit export offset)",
    )
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
        "sizeof(denzic_diag_log_event_t) == DENZIC_DIAG_LOG_EVENT_WIRE_BYTES" in diag_platform,
        "diag log store must assert the BLE wire event size",
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
