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
READINESS_UUID = "710af845-6d9f-6583-0c4d-9e5b3bc3091c"
CAPABILITIES_UUID = "710af845-6d9f-6583-0c4d-9e5b3bc3091d"
MAX_CHUNK_BYTES = 512
CHUNK_BYTES = 500

UUID_BYTES = {
    "BLE_FIRMWARE_OTA_SERVICE_UUID": "0x2a, 0x09, 0xc3, 0x3b, 0x5b, 0x9e, 0x4d, 0x0c, 0x83, 0x65, 0x9f, 0x6d, 0x45, 0xf8, 0x0a, 0x71",
    "BLE_FIRMWARE_OTA_CONTROL_UUID": "0x2b, 0x09, 0xc3, 0x3b, 0x5b, 0x9e, 0x4d, 0x0c, 0x83, 0x65, 0x9f, 0x6d, 0x45, 0xf8, 0x0a, 0x71",
    "BLE_FIRMWARE_OTA_DATA_UUID": "0x2c, 0x09, 0xc3, 0x3b, 0x5b, 0x9e, 0x4d, 0x0c, 0x83, 0x65, 0x9f, 0x6d, 0x45, 0xf8, 0x0a, 0x71",
    "BLE_FIRMWARE_OTA_READINESS_UUID": "0x1c, 0x09, 0xc3, 0x3b, 0x5b, 0x9e, 0x4d, 0x0c, 0x83, 0x65, 0x9f, 0x6d, 0x45, 0xf8, 0x0a, 0x71",
    "BLE_FIRMWARE_OTA_CAPABILITIES_UUID": "0x1d, 0x09, 0xc3, 0x3b, 0x5b, 0x9e, 0x4d, 0x0c, 0x83, 0x65, 0x9f, 0x6d, 0x45, 0xf8, 0x0a, 0x71",
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
        "BLE_GATT_CHR_F_READ",
        "ble_gatts_add_svcs",
        "DIAG_OTA_ABORT_BLE_CONTROL",
        "DIAG_OTA_ABORT_BLE_WRITE_FAIL",
        "DIAG_OTA_ABORT_BLE_DISCONNECT",
        "listener_device_get_factory_readiness()",
        "listener_device_get_capabilities()",
        "BLE_FIRMWARE_OTA_GATT_ATTR_READINESS",
        "BLE_FIRMWARE_OTA_GATT_ATTR_CAPABILITIES",
        "readiness_rc",
        "capabilities_rc",
        "#define BLE_FIRMWARE_OTA_DATA_MAX_BYTES 512",
    ]
    for token in required_tokens:
        require(token in source, f"BLE OTA bridge is missing {token}")
    require(
        "status.expected_size != expected_size" in source and "firmware_ota_abort(DIAG_OTA_ABORT_BLE_CONTROL)" in source,
        "finish size mismatch must abort the OTA session",
    )
    require(
        "BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE" in source
        and "BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP" in source,
        "OTA control/data characteristics must remain readable so Windows can recover identity even when new GATT characteristics are cached out",
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
    require(
        "s_scan_rsp_fields.uuids128_is_complete = 0" in gap,
        "BLE advertising must not mark the single advertised 128-bit service UUID as complete while OTA is also present",
    )
    require("ble_firmware_ota" in hid_cmake, "ble_hid component does not require ble_firmware_ota")
    require("ble_firmware_ota" in gap_cmake, "ble_hid_gap component does not require ble_firmware_ota")
    for token in (
        "DIAG_OTA_ABORT_BLE_CONTROL",
        "DIAG_OTA_ABORT_BLE_WRITE_FAIL",
        "DIAG_OTA_ABORT_BLE_DISCONNECT",
    ):
        require(token in diag, f"diag_log_events.h is missing {token}")


def check_dis_identity(repo: Path) -> None:
    hid = read_text(repo / "ports/esp32/ble_hid/ble_hid.c")
    gap = read_text(repo / "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c")
    listener_device = read_text(repo / "protocols/listener_device/listener_device.c")
    listener_header = read_text(repo / "protocols/listener_device/include/listener_device.h")
    firmware_ota = read_text(repo / "components/firmware_ota/firmware_ota.c")
    firmware_ota_header = read_text(repo / "components/firmware_ota/include/firmware_ota.h")
    diag = read_text(repo / "components/diag_log/include/diag_log_events.h")
    default_configs = [
        repo / "sdkconfig.defaults",
        repo / "sdkconfig.defaults.esp32s3",
    ]

    for path in default_configs:
        config = read_text(path)
        for token in (
            "CONFIG_BT_NIMBLE_DIS_SERVICE=y",
            "CONFIG_BT_NIMBLE_SVC_DIS_MANUFACTURER_NAME=y",
            "CONFIG_BT_NIMBLE_SVC_DIS_SERIAL_NUMBER=y",
            "CONFIG_BT_NIMBLE_SVC_DIS_HARDWARE_REVISION=y",
            "CONFIG_BT_NIMBLE_SVC_DIS_FIRMWARE_REVISION=y",
            "CONFIG_BT_NIMBLE_SVC_DIS_SOFTWARE_REVISION=y",
            "CONFIG_BT_NIMBLE_SVC_DIS_PNP_ID=y",
        ):
            require(token in config, f"{path.name} is missing {token}")

    require(
        "ble_svc_dis_firmware_revision_set(listener_device_get_fw_version())" in hid,
        "DIS firmware revision must be set from listener_device_get_fw_version()",
    )
    require(
        "ble_svc_dis_manufacturer_name_set(LISTENER_DEVICE_MANUFACTURER)" in hid,
        "DIS manufacturer must be set from LISTENER_DEVICE_MANUFACTURER",
    )
    require(
        "ble_svc_dis_serial_number_set(listener_device_get_serial())" in hid,
        "DIS serial number must be set from listener_device_get_serial()",
    )
    require(
        "ble_svc_dis_hardware_revision_set(LISTENER_DEVICE_HW_REV)" in hid,
        "DIS hardware revision must be set from LISTENER_DEVICE_HW_REV",
    )
    require(
        ".vendor_id = LISTENER_VENDOR_ID" in hid
        and ".product_id = LISTENER_PRODUCT_ID" in hid
        and ".version = LISTENER_PROTOCOL_VERSION" in hid
        and "vid=0x%04x pid=0x%04x product_version=%u" in hid,
        "DIS PnP ID must be sourced from the HID Listener VID/PID/protocol identity",
    )
    require(
        "ble_svc_dis_software_revision_set(listener_device_get_protocol_version())" in hid,
        "DIS software revision must be set from listener_device_get_protocol_version()",
    )
    require(
        "esp_app_get_description()" in listener_device
        and "return app_desc->version;" in listener_device,
        "listener_device_get_fw_version() must return the ESP app description version",
    )
    require(
        "firmware_ota_v1" in listener_header,
        "Listener device capabilities must advertise firmware_ota_v1 for desktop preflight",
    )
    for token in (
        "LISTENER_DEVICE_READY_HID",
        "LISTENER_DEVICE_READY_AUDIO",
        "LISTENER_DEVICE_READY_OTA",
        "LISTENER_DEVICE_READY_DIAGNOSTIC",
        "listener_device_set_readiness",
        "listener_device_get_ready_mask",
        "listener_device_get_degraded_mask",
    ):
        require(token in listener_header, f"Listener device readiness contract is missing {token}")
    require(
        '"hid"' in listener_device
        and '"audio"' in listener_device
        and '"ota"' in listener_device
        and '"diagnostic"' in listener_device
        and '"%s_%s"' in listener_device
        and '"ready"' in listener_device
        and '"degraded"' in listener_device,
        "Listener readiness/capabilities must expose per-subsystem ready/degraded tokens",
    )
    require(
        "ble_hid_publish_readiness(" in hid
        and "LISTENER_DEVICE_READY_HID" in hid
        and "LISTENER_DEVICE_READY_AUDIO" in hid
        and "LISTENER_DEVICE_READY_OTA" in hid
        and "LISTENER_DEVICE_READY_DIAGNOSTIC" in hid,
        "BLE HID init must publish separate HID/audio/OTA/diagnostic readiness",
    )
    require(
        "ready_mask" in firmware_ota_header
        and "degraded_mask" in firmware_ota_header
        and "readiness" in firmware_ota_header
        and "capabilities" in firmware_ota_header
        and "listener_device_get_ready_mask()" in firmware_ota
        and "listener_device_get_degraded_mask()" in firmware_ota
        and "listener_device_get_factory_readiness()" in firmware_ota
        and "listener_device_get_capabilities()" in firmware_ota
        and "OTA STATUS" in firmware_ota
        and "ready_mask=0x%08" in firmware_ota
        and "capabilities=%s" in firmware_ota,
        "~OTA:STATUS must expose per-subsystem readiness and capabilities for hardware validation",
    )
    require(
        'strcmp(line, "~DIS:GATT")' in hid and "ble_hid_log_dis_gatt_state()" in hid,
        "serial ~DIS:GATT must log DIS service/characteristic handles for hardware validation",
    )
    require(
        "BLE_SVC_DIS_CHR_UUID16_FIRMWARE_REVISION" in hid
        and "ble_gatts_find_chr(" in hid,
        "DIS GATT state logging must verify Firmware Revision 2A26 is registered",
    )
    for token in (
        "BLE_SVC_DIS_CHR_UUID16_SERIAL_NUMBER",
        "BLE_SVC_DIS_CHR_UUID16_MANUFACTURER_NAME",
        "BLE_SVC_DIS_CHR_UUID16_PNP_ID",
    ):
        require(token in hid, f"DIS GATT state logging must include {token}")
    require(
        "ble_hid_gap_queue_service_changed(\"connect\")" in gap
        and "ble_hid_gap_service_changed_pending()" in gap
        and "BLE_HID_GAP_GATT_SCHEMA_REV" in gap
        and "diag_export_v2" in gap
        and "nvs_get_str" in gap
        and "nvs_set_str" in gap,
        "BLE connect path must version/schema-gate Service Changed so Windows refreshes OTA/DIS GATT once after firmware or GATT-shape updates",
    )
    require(
        "BLE_SVC_GATT_CHR_SERVICE_CHANGED_UUID16" in gap
        and "event->subscribe.reason == BLE_GAP_SUBSCRIBE_REASON_WRITE" in gap
        and "ble_hid_gap_indicate_service_changed(event->subscribe.conn_handle, \"central subscribe\")" in gap
        and "ble_hid_gap_indicate_service_changed(event->enc_change.conn_handle, \"encryption change\")" in gap
        and "ble_gatts_indicate_custom(conn_handle, service_changed_val_handle, om)" in gap
        and "service changed marked for %s" in gap
        and "service changed indication skipped" in gap
        and "service changed indication tx complete" in gap,
        "BLE subscribe path must mark GATT changed on connect, then send or skip Service Changed according to the version-gated pending state",
    )
    require(
        "DIAG_GAP_RECOVERY" in diag
        and "DIAG_GAP_RECOVERY" in gap
        and "recovery: clearing pairing bonds" in gap
        and "recovery: pairing reset complete" in gap,
        "BLE recovery actions must be logged in serial and diag_log",
    )
    require(
        "ble_gap_set_prefered_le_phy" not in gap
        and re.search(r'ble_hid_gap_request_connection_params\(\s*"audio"', gap) is None
        and "audio connection parameters left to central" in gap
        and "audio PHY preference left to central" in gap
        and "BLE_GAP_EVENT_PHY_UPDATE_COMPLETE" in gap
        and "DIAG_GAP_PHY" in gap
        and "DIAG_GAP_PHY" in diag,
        "GAP connect must not initiate audio LL parameter or 2M PHY requests that can leave Windows bonded but disconnected after an LL response timeout",
    )


def candidate_desktop_contracts(repo: Path) -> list[Path]:
    listener_root = repo.parent
    return [
        listener_root / "Listener-Type-wt-tai-voice-keyboard-ota-update-1.2/src/lib/firmwareOta.ts",
        listener_root / "Listener-Type/src/lib/firmwareOta.ts",
    ]


def candidate_desktop_ble_sources(repo: Path) -> list[Path]:
    listener_root = repo.parent
    return [
        listener_root / "Listener-Type-wt-tai-voice-keyboard-ota-update-1.2/src-tauri/src/embedded_ble.rs",
        listener_root / "Listener-Type/src-tauri/src/embedded_ble.rs",
    ]


def check_desktop_contract(path: Path) -> str:
    contract = read_text(path)
    expected_pairs = {
        SERVICE_UUID: "serviceUuid",
        CONTROL_UUID: "controlUuid",
        DATA_UUID: "dataUuid",
    }
    for value, field in expected_pairs.items():
        require(value in contract, f"desktop contract {path} is missing {field}={value}")
    for field in ("defaultChunkBytes", "maxChunkBytes"):
        match = re.search(rf"{field}\s*:\s*(\d+)", contract)
        require(match is not None, f"desktop contract {path} is missing {field}")
        chunk_bytes = int(match.group(1))
        require(
            chunk_bytes == CHUNK_BYTES,
            f"desktop contract {path} has {field}={chunk_bytes}, expected {CHUNK_BYTES}",
        )
    return str(path)


def check_desktop_ble_source(path: Path) -> str:
    source = read_text(path)
    expected_tokens = {
        SERVICE_UUID: "OTA_SERVICE_UUID",
        CONTROL_UUID: "OTA_CONTROL_UUID",
        DATA_UUID: "OTA_DATA_UUID",
        READINESS_UUID: "OTA_READINESS_UUID",
        CAPABILITIES_UUID: "OTA_CAPABILITIES_UUID",
    }
    for uuid, field in expected_tokens.items():
        token = f"0x{uuid.replace('-', '_')}"
        require(token in source, f"desktop BLE source {path} is missing {field}={uuid}")
    require(
        "OTA_CONTROL_UUID" in source
        and "OTA_DATA_UUID" in source
        and "readiness_field(&readiness, \"fw_version\")" in source
        and "split_capability_tokens(&capabilities)" in source,
        f"desktop BLE source {path} must read OTA identity/capabilities from the stable control/data UUIDs when Windows hides newer GATT characteristics",
    )
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
        check_dis_identity(repo)

        desktop_checked = None
        if args.desktop_contract:
            desktop_checked = check_desktop_contract(args.desktop_contract.resolve())
        else:
            for candidate in candidate_desktop_contracts(repo):
                if candidate.exists():
                    desktop_checked = check_desktop_contract(candidate.resolve())
                    break
        desktop_ble_checked = None
        for candidate in candidate_desktop_ble_sources(repo):
            if candidate.exists():
                desktop_ble_checked = check_desktop_ble_source(candidate.resolve())
                break

        if desktop_checked:
            suffix = f" and desktop BLE source at {desktop_ble_checked}" if desktop_ble_checked else ""
            print(f"PASS: BLE OTA GATT contract matches desktop contract at {desktop_checked}{suffix}")
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
