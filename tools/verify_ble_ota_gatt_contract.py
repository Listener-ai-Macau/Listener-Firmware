#!/usr/bin/env python3
"""Static contract check for the Listener Denzic OTA v1 product adapter."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


READINESS_UUID = "710af845-6d9f-6583-0c4d-9e5b3bc3091c"
CAPABILITIES_UUID = "710af845-6d9f-6583-0c4d-9e5b3bc3091d"
CHUNK_BYTES = 500

UUID_BYTES = {
    "BLE_FIRMWARE_OTA_READINESS_UUID": "0x1c, 0x09, 0xc3, 0x3b, 0x5b, 0x9e, 0x4d, 0x0c, 0x83, 0x65, 0x9f, 0x6d, 0x45, 0xf8, 0x0a, 0x71",
    "BLE_FIRMWARE_OTA_CAPABILITIES_UUID": "0x1d, 0x09, 0xc3, 0x3b, 0x5b, 0x9e, 0x4d, 0x0c, 0x83, 0x65, 0x9f, 0x6d, 0x45, 0xf8, 0x0a, 0x71",
}

SHARED_UUID_MACROS = {
    "BLE_FIRMWARE_OTA_SERVICE_UUID": "DENZIC_OTA_V1_GATT_SERVICE_UUID_BYTES",
    "BLE_FIRMWARE_OTA_V1_CONTROL_UUID": "DENZIC_OTA_V1_GATT_CONTROL_UUID_BYTES",
    "BLE_FIRMWARE_OTA_V1_DATA_UUID": "DENZIC_OTA_V1_GATT_DATA_UUID_BYTES",
    "BLE_FIRMWARE_OTA_V1_STATUS_UUID": "DENZIC_OTA_V1_GATT_STATUS_UUID_BYTES",
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


def check_shared_core(repo: Path) -> None:
    gitmodules = read_text(repo / ".gitmodules")
    cmake = read_text(repo / "ports/esp32/ble_firmware_ota/CMakeLists.txt")
    generated = read_text(
        repo / "third_party/denzic-platform/ota/embedded/c/include/denzic_ota_v1_generated.h"
    )
    spec = json.loads(read_text(repo / "third_party/denzic-platform/ota/protocol/ota_v1.json"))
    core = read_text(
        repo / "third_party/denzic-platform/ota/embedded/c/src/denzic_ota_v1.c"
    )
    reorder_core = read_text(
        repo / "third_party/denzic-platform/ota/embedded/c/src/denzic_ota_v1_reorder.c"
    )
    require(
        "third_party/denzic-platform" in gitmodules
        and "Listener-ai-Macau/Denzic-Platform.git" in gitmodules,
        "firmware must pin the shared Denzic-Platform repository as a submodule",
    )
    require(
        "third_party/denzic-platform/ota/embedded/c/src/denzic_ota_v1.c" in cmake
        and "third_party/denzic-platform/ota/embedded/c/include" in cmake,
        "BLE OTA component must compile the shared embedded C core",
    )
    for token in (
        '#define DENZIC_OTA_V1_PROTOCOL_NAME "denzic_ota_v1"',
        '#define DENZIC_OTA_V1_MAGIC "DOV1"',
        "#define DENZIC_OTA_V1_PROTOCOL_VERSION (1u)",
    ):
        require(token in generated, f"shared generated contract is missing {token}")
    for name, value in spec["gatt"].items():
        macro = name.removesuffix("_uuid").upper()
        require(
            f'#define DENZIC_OTA_V1_GATT_{macro}_UUID_TEXT "{value}"' in generated,
            f"shared generated GATT contract is missing {name}",
        )
    for token in (
        "denzic_ota_v1_handle_control",
        "denzic_ota_v1_handle_data",
        "denzic_ota_v1_encode_status",
        "DENZIC_OTA_V1_ERROR_OFFSET_MISMATCH",
    ):
        require(token in core, f"shared embedded core is missing {token}")
    require(
        "denzic_ota_v1_handle_data_ordered" in reorder_core
        and "third_party/denzic-platform/ota/embedded/c/src/denzic_ota_v1_reorder.c" in cmake,
        "shared ordered dual-lane OTA core must be compiled into the product adapter",
    )


def check_header(repo: Path) -> None:
    header = read_text(repo / "ports/esp32/ble_firmware_ota/include/ble_firmware_ota.h")
    require('#include "denzic_ota_v1_generated.h"' in header,
            "product GATT adapter must include the shared generated UUID contract")
    for macro, shared_macro in SHARED_UUID_MACROS.items():
        require(
            re.search(
                rf"#define\s+{macro}\s+\\?\s*BLE_UUID128_INIT\({shared_macro}\)",
                header,
                re.MULTILINE,
            ) is not None,
            f"{macro} must reference {shared_macro}",
        )
    for macro, byte_list in UUID_BYTES.items():
        match = re.search(
            rf"#define\s+{macro}\s+\\?\s*BLE_UUID128_INIT\(([^)]*)\)",
            header,
            re.MULTILINE,
        )
        require(match is not None, f"{macro} is not defined")
        require(
            compact(match.group(1)) == compact(byte_list),
            f"{macro} does not match the canonical Listener product UUID",
        )
    require("0x2b, 0x09" not in header and "0x2c, 0x09" not in header,
            "old JSON control/data characteristics must not remain")


def check_bridge(repo: Path) -> None:
    source = read_text(repo / "ports/esp32/ble_firmware_ota/ble_firmware_ota_esp32.c")
    required = (
        '#include "denzic_ota_v1.h"',
        "denzic_ota_v1_init(",
        "denzic_ota_v1_handle_control(",
        "denzic_ota_v1_handle_data_ordered(",
        "denzic_ota_v1_encode_status(",
        "firmware_ota_begin(image_size, DENZIC_OTA_V1_PROTOCOL_NAME)",
        "firmware_ota_write(s_ota_storage_batch, length)",
        "firmware_ota_finish(false)",
        "BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP",
        "DENZIC_OTA_V1_STATUS_FLAG_ACTIVE_LINK_CONFIRMED",
        "ble_hid_gap_schedule_active_connection",
        "listener_device_get_factory_readiness()",
        "listener_device_get_capabilities()",
    )
    for token in required:
        require(token in source, f"Listener OTA product adapter is missing {token}")
    for token in (
        "handle_control_json",
        "LOV1",
    ):
        require(token not in source, f"obsolete OTA path remains in product adapter: {token}")
    require(
        source.count(".uuid = &s_control_uuid.u") == 1
        and source.count(".uuid = &s_data_uuid.u") == 1
        and source.count(".uuid = &s_status_uuid.u") == 1,
        "product adapter must register exactly one control/data/status data plane",
    )
    service_start = source.find("static const struct ble_gatt_svc_def s_ota_svcs[]")
    service_end = source.find("esp_err_t ble_firmware_ota_register_gatt", service_start)
    require(service_start >= 0 and service_end >= 0, "OTA GATT service definition is missing")
    service = source[service_start:service_end]
    readiness_index = service.find(".uuid = &s_readiness_uuid.u")
    control_index = service.find(".uuid = &s_control_uuid.u")
    data_index = service.find(".uuid = &s_data_uuid.u")
    status_index = service.find(".uuid = &s_status_uuid.u")
    capabilities_index = service.find(".uuid = &s_capabilities_uuid.u")
    require(
        0 <= readiness_index < control_index < data_index < status_index < capabilities_index,
        "new OTA capabilities must append after the legacy readiness/control/data/status handles",
    )


def check_product_identity(repo: Path) -> None:
    listener_header = read_text(repo / "protocols/listener_device/include/listener_device.h")
    listener_source = read_text(repo / "protocols/listener_device/listener_device.c")
    package = read_text(repo / "tools/package_ota_firmware.ps1")
    combined = listener_header + listener_source + package
    require("denzic_ota_v1" in listener_header and "denzic_ota_v1" in listener_source,
            "device capabilities must advertise denzic_ota_v1")
    for token in (
        'third_party\\denzic-platform\\ota\\protocol\\ota_v1.json',
        'name = [string]$ota_protocol.name',
        'version = [int]$ota_protocol.version',
        'firmware_capability = [string]$ota_protocol.name',
        'service_uuid = [string]$ota_protocol.gatt.service_uuid',
        'control_uuid = [string]$ota_protocol.gatt.control_uuid',
        'data_uuid = [string]$ota_protocol.gatt.data_uuid',
        'status_uuid = [string]$ota_protocol.gatt.status_uuid',
    ):
        require(token in package, f"OTA manifest generator is missing {token}")
    require("firmware_ota_v1" not in combined and "listener_ble_ota_v1" not in combined,
            "old OTA identity must not remain in firmware capability or package metadata")


def check_runtime_integration(repo: Path) -> None:
    hid = read_text(repo / "ports/esp32/ble_hid/ble_hid.c")
    gap = read_text(repo / "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c")
    audio = read_text(repo / "ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c")
    adapter = read_text(repo / "ports/esp32/ble_firmware_ota/ble_firmware_ota_esp32.c")
    ota = read_text(repo / "components/firmware_ota/firmware_ota.c")
    main = read_text(repo / "main/main.c")
    require("ble_firmware_ota_register_gatt()" in hid, "BLE init must register OTA GATT")
    require("ble_firmware_ota_on_gap_disconnect(" in gap, "disconnect must reach the OTA adapter")
    require(
        "preserving same-image session" in adapter
        and "denzic_ota_v1_set_status_flags(&s_ota, 0)" in adapter,
        "disconnect must preserve an interrupted same-image OTA session while clearing link confirmation",
    )
    disconnect = re.search(
        r"void\s+ble_firmware_ota_on_gap_disconnect\s*\([^)]*\)\s*\{(?P<body>[\s\S]*?)\n\}",
        adapter,
    )
    require(disconnect is not None, "OTA disconnect adapter body is missing")
    disconnect_body = disconnect.group("body")
    require(
        "firmware_ota_abort" not in disconnect_body
        and "ble_firmware_ota_core_init" not in disconnect_body,
        "BLE disconnect must pause OTA until the bounded inactivity timeout, not erase resume state",
    )
    require(
        "ble_hid_gap_schedule_active_connection" in gap
        and "ble_hid_gap_active_connection_applied" in gap,
        "Listener driver must expose deferred active-link control and confirmation",
    )
    begin_active_request = re.search(
        r"BEGIN starts bulk WWR immediately[\s\S]{0,800}"
        r"ble_hid_gap_request_active_connection\s*!=\s*NULL[\s\S]{0,240}"
        r"\?\s*ble_hid_gap_request_active_connection\(\)[\s\S]{0,120}"
        r":\s*ble_hid_gap_schedule_active_connection\(\)",
        adapter,
    )
    require(
        begin_active_request is not None,
        "accepted OTA BEGIN must request the bounded active link immediately and keep deferred scheduling only as fallback",
    )
    ota_handler = re.search(
        r'if\s*\(strcmp\(command, "TYPE:OTA"\)\s*==\s*0\)\s*\{(?P<body>[\s\S]*?)\n\s{4}\}',
        audio,
    )
    require(ota_handler is not None, "TYPE:OTA control handler is missing")
    ota_handler_body = ota_handler.group("body")
    require(
        "ble_hid_gap_schedule_ota_reconnect()" in ota_handler_body
        and "type OTA reconnect handoff" in ota_handler_body
        and "ble_hid_gap_request_active_connection()" not in ota_handler_body,
        "desktop pre-transfer hint must schedule the dedicated OTA reconnect handoff",
    )
    require(
        "BLE_HID_GAP_OTA_RECONNECT_DEFER_MS 750U" in gap
        and "ble_hid_gap_ota_connection_ready" in gap
        and "OTA reconnect handoff terminated low-power connection" in gap
        and "ble_gap_terminate(conn.conn_handle, BLE_ERR_REM_USER_CONN_TERM)" in gap,
        "OTA handoff must keep fast links and replace only an existing low-power connection",
    )
    require(
        "ble_hid_gap_ota_connection_ready" in adapter
        and "transfer_link_ready" in adapter
        and "if (active_link_applied)" in adapter,
        "OTA must start on a fresh 15 ms link while still converging to the preferred 7.5 ms interval",
    )
    require(
        "FIRMWARE_OTA_INACTIVITY_TIMEOUT_MS (3U * 60U * 1000U)" in ota
        and "firmware_ota_inactivity_timer_callback" in ota,
        "stale OTA sessions must exit after three minutes",
    )
    require(
        "bool self_check_recorded;" in ota
        and "bool boot_ble_ready;" in ota
        and "if (!self_check_recorded)" in ota,
        "pending-verify must defer decisions until startup self-check evidence is recorded",
    )
    require(
        "s_ota.boot_ble_ready = ble_ready;" in ota
        and "s_ota.self_check_recorded = true;" in ota
        and "s_ota.ble_ready = ble_ready;" not in ota,
        "startup BLE readiness must not overwrite an early encrypted-link readiness event",
    )
    require(
        "firmware_ota_record_self_check(ota_post_ok, ota_ble_ready, ota_keyboard_ready);" in main
        and re.search(
            r"firmware_ota_record_self_check\([^;]+;[\s\S]{0,240}"
            r"firmware_ota_confirm_pending_verify_if_ready\(\);",
            main,
        ),
        "app_main must re-evaluate pending-verify after recording all startup evidence",
    )
    require(
        "if (s_ota.self_check_recorded && s_ota.boot_ble_ready)" in ota,
        "clean-uptime fallback must promote successful BLE initialization only after self-check",
    )


def check_desktop(repo: Path, explicit: Path | None) -> str:
    type_repo = repo.parent / "Listener-Type"
    contract_path = explicit or type_repo / "src/lib/firmwareOta.ts"
    rust_paths = (
        type_repo / "src-tauri/src/embedded_ble/mod.rs",
        type_repo / "src-tauri/src/embedded_ble/windows_ble/mod.rs",
        type_repo / "src-tauri/src/embedded_ble/windows_ble/ota_transfer.rs",
    )
    cargo_path = type_repo / "src-tauri/Cargo.toml"
    contract = read_text(contract_path)
    rust = "\n".join(read_text(path) for path in rust_paths)
    cargo = read_text(cargo_path)
    for token in (
        "DENZIC_OTA_V1_PROTOCOL_NAME",
        "DENZIC_OTA_V1_GATT_SERVICE_UUID",
        "DENZIC_OTA_V1_GATT_CONTROL_UUID",
        "DENZIC_OTA_V1_GATT_DATA_UUID",
        "DENZIC_OTA_V1_GATT_STATUS_UUID",
    ):
        require(token in contract, f"desktop OTA contract is missing {token}")
    for token in (
        "impl denzic_ota_core::OtaV1Transport",
        "denzic_ota_core::transfer",
        "LISTENER_OTA_V1_INACTIVE_LINK_WINDOW_CHUNKS",
        "LISTENER_OTA_V1_DEFAULT_WINDOW_CHUNKS",
        "TYPE:OTA",
        "denzic_ota_core::GATT_SERVICE_UUID_U128",
        "denzic_ota_core::GATT_CONTROL_UUID_U128",
    ):
        require(token in rust, f"desktop Listener driver is missing {token}")
    require("third_party/denzic-platform/ota/host/rust" in cargo,
            "desktop must compile the shared host OTA core")
    return str(contract_path.resolve())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--desktop-contract", type=Path)
    args = parser.parse_args()
    repo = args.repo.resolve()
    try:
        check_shared_core(repo)
        check_header(repo)
        check_bridge(repo)
        check_product_identity(repo)
        check_runtime_integration(repo)
        desktop = check_desktop(repo, args.desktop_contract.resolve() if args.desktop_contract else None)
        print(f"PASS: Denzic OTA v1 host/embedded contract matches Listener driver and {desktop}")
        return 0
    except AssertionError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
