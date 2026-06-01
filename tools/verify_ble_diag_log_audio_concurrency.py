#!/usr/bin/env python3
"""Validate BLE diagnostic-log export while BLE audio capture is active."""

from __future__ import annotations

import argparse
import asyncio
import hashlib
import json
import pathlib
import struct
import time
import uuid
import zlib
from types import SimpleNamespace

from ble_audio_regression_common import validate_transport_summary
from capture_audio_ble_wav import (
    close_ble_objects,
    configure_utf8_stdio,
    disable_notify_best_effort,
    ensure_host_ble_connection,
    get_paired_device_address_hex,
    open_ble_device,
    run_capture_with_args,
)
from winrt.windows.devices.bluetooth import BluetoothCacheMode
from winrt.windows.devices.bluetooth.genericattributeprofile import (
    GattCharacteristicProperties,
    GattClientCharacteristicConfigurationDescriptorValue as GattCccdValue,
    GattCommunicationStatus,
    GattWriteOption,
)
from winrt.windows.devices.enumeration import DeviceAccessStatus
from winrt.windows.storage.streams import DataReader, DataWriter


DIAG_SERVICE_UUID = uuid.UUID("710af845-6d9f-6583-0c4d-9e5b3bc3093a")
DIAG_CONTROL_UUID = uuid.UUID("710af845-6d9f-6583-0c4d-9e5b3bc3093b")
DIAG_DATA_UUID = uuid.UUID("710af845-6d9f-6583-0c4d-9e5b3bc3093c")
DIAG_COUNT_UUID = uuid.UUID("710af845-6d9f-6583-0c4d-9e5b3bc3093d")
DIAG_EVENT_BYTES = 24
DIAG_CHUNK_HEADER_BYTES = 8
DEFAULT_ARTIFACT_DIR = pathlib.Path("tests") / "artifacts" / "ble_diag_log"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Start a normal BLE audio capture session and pull firmware diag_log "
            "over the BLE diagnostic GATT service during that capture."
        )
    )
    parser.add_argument("--port", required=True)
    parser.add_argument("--device-name", default="listener")
    parser.add_argument("--bluetooth-address", default="")
    parser.add_argument("--capture-seconds", type=int, default=6)
    parser.add_argument("--timeout-seconds", type=int, default=75)
    parser.add_argument("--boot-timeout-seconds", type=int, default=15)
    parser.add_argument("--ble-connect-timeout-seconds", type=int, default=15)
    parser.add_argument("--notify-ready-timeout-seconds", type=int, default=45)
    parser.add_argument("--diag-start-delay-seconds", type=float, default=0.75)
    parser.add_argument("--diag-page-delay-seconds", type=float, default=0.05)
    parser.add_argument("--diag-timeout-seconds", type=float, default=45.0)
    parser.add_argument("--min-diag-events", type=int, default=1)
    parser.add_argument("--min-diag-chunks", type=int, default=2)
    parser.add_argument("--artifacts-dir", default=str(DEFAULT_ARTIFACT_DIR))
    parser.add_argument(
        "--no-reset-before-capture",
        action="store_false",
        dest="reset_before_capture",
    )
    parser.set_defaults(reset_before_capture=True)
    return parser.parse_args()


def normalize_address(raw: str) -> str:
    normalized = "".join(ch for ch in str(raw or "") if ch in "0123456789ABCDEFabcdef").upper()
    if len(normalized) != 12:
        raise RuntimeError(f"Bluetooth address must contain 12 hex digits, got {raw!r}")
    return normalized


def buffer_to_bytes(buffer) -> bytes:
    reader = DataReader.from_buffer(buffer)
    payload = bytearray(reader.unconsumed_buffer_length)
    reader.read_bytes(payload)
    reader.close()
    return bytes(payload)


def bytes_to_buffer(payload: bytes):
    writer = DataWriter()
    writer.write_bytes(payload)
    return writer.detach_buffer()


def status_name(status) -> str:
    try:
        return status.name
    except AttributeError:
        return str(status)


async def get_characteristic(service, characteristic_uuid: uuid.UUID, label: str):
    last_status = None
    for cache_mode in (BluetoothCacheMode.UNCACHED, BluetoothCacheMode.CACHED):
        result = await service.get_characteristics_for_uuid_with_cache_mode_async(
            characteristic_uuid,
            cache_mode,
        )
        last_status = result.status
        if result.status == GattCommunicationStatus.SUCCESS and len(result.characteristics) > 0:
            return result.characteristics[0]
    raise RuntimeError(
        f"diagnostic {label} characteristic {characteristic_uuid} discovery failed: "
        f"status={status_name(last_status)}"
    )


async def discover_service(device, service_uuid: uuid.UUID):
    last_error = None
    last_status = None

    for cache_mode in (BluetoothCacheMode.CACHED, BluetoothCacheMode.UNCACHED):
        try:
            result = await device.get_gatt_services_for_uuid_with_cache_mode_async(
                service_uuid,
                cache_mode,
            )
            last_status = result.status
            if result.status == GattCommunicationStatus.SUCCESS and len(result.services) > 0:
                return result.services[0]
        except Exception as exc:
            last_error = exc

    for cache_mode in (BluetoothCacheMode.CACHED, BluetoothCacheMode.UNCACHED):
        try:
            result = await device.get_gatt_services_with_cache_mode_async(cache_mode)
            last_status = result.status
            if result.status != GattCommunicationStatus.SUCCESS:
                continue
            for candidate in result.services:
                if candidate.uuid == service_uuid:
                    return candidate
                try:
                    candidate.close()
                except Exception:
                    pass
        except Exception as exc:
            last_error = exc

    detail = f"status={status_name(last_status)}"
    if last_error is not None:
        detail += f" last_error={type(last_error).__name__}:{last_error}"
    raise RuntimeError(f"diagnostic service discovery failed: {detail}")


def require_property(characteristic, property_flag, label: str) -> None:
    properties = int(characteristic.characteristic_properties)
    if not properties & int(property_flag):
        raise RuntimeError(
            f"diagnostic {label} characteristic lacks {property_flag}: "
            f"properties={characteristic.characteristic_properties}"
        )


class DiagnosticLogPuller:
    def __init__(
        self,
        *,
        device_name: str,
        address_hex: str,
        page_delay_seconds: float,
        operation_timeout_seconds: float,
    ) -> None:
        self.device_name = device_name
        self.address_hex = address_hex
        self.page_delay_seconds = page_delay_seconds
        self.operation_timeout_seconds = operation_timeout_seconds
        self.device = None
        self.service = None
        self.gatt_session = None
        self.control = None
        self.data = None
        self.count = None
        self.notify_token = None
        self.notifications: asyncio.Queue[bytes] = asyncio.Queue()
        self.loop = None
        self.export_started = False

    async def open(self) -> None:
        try:
            ensure_host_ble_connection(
                self.device_name,
                self.address_hex,
                duration_seconds=12,
                poll_interval_seconds=2,
            )
        except Exception:
            pass

        self.device = await open_ble_device(self.address_hex)
        if self.device is None:
            raise RuntimeError(f"unable to open BluetoothLEDevice for {self.address_hex}")

        self.service = await discover_service(self.device, DIAG_SERVICE_UUID)
        access_status = await self.service.request_access_async()
        if access_status not in (DeviceAccessStatus.ALLOWED, DeviceAccessStatus.UNSPECIFIED):
            raise RuntimeError(f"diagnostic service access denied: {access_status}")

        self.gatt_session = self.service.session
        if self.gatt_session is not None and self.gatt_session.can_maintain_connection:
            self.gatt_session.maintain_connection = True

        self.control = await get_characteristic(self.service, DIAG_CONTROL_UUID, "control")
        self.data = await get_characteristic(self.service, DIAG_DATA_UUID, "data")
        self.count = await get_characteristic(self.service, DIAG_COUNT_UUID, "count")
        require_property(self.control, GattCharacteristicProperties.WRITE, "control")
        require_property(self.data, GattCharacteristicProperties.NOTIFY, "data")
        require_property(self.count, GattCharacteristicProperties.READ, "count")

        await self.enable_notifications()

    async def enable_notifications(self) -> None:
        self.loop = asyncio.get_running_loop()

        def on_value_changed(_sender, event_args) -> None:
            payload = buffer_to_bytes(event_args.characteristic_value)
            self.loop.call_soon_threadsafe(self.notifications.put_nowait, payload)

        result = await asyncio.wait_for(
            self.data.write_client_characteristic_configuration_descriptor_with_result_async(
                GattCccdValue.NOTIFY
            ),
            timeout=8.0,
        )
        if result.status != GattCommunicationStatus.SUCCESS:
            raise RuntimeError(
                "diagnostic notify enable failed: "
                f"status={status_name(result.status)} protocol_error={getattr(result, 'protocol_error', None)}"
            )
        self.notify_token = self.data.add_value_changed(on_value_changed)

    async def read_count(self) -> int:
        result = await asyncio.wait_for(
            self.count.read_value_with_cache_mode_async(BluetoothCacheMode.UNCACHED),
            timeout=self.operation_timeout_seconds,
        )
        if result.status != GattCommunicationStatus.SUCCESS:
            raise RuntimeError(f"diagnostic count read failed: status={status_name(result.status)}")
        payload = buffer_to_bytes(result.value).decode("utf-8", errors="replace")
        parsed = json.loads(payload)
        return int(parsed["count"])

    async def write_control(self, payload: dict[str, object]) -> None:
        raw = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        result = await asyncio.wait_for(
            self.control.write_value_with_result_and_option_async(
                bytes_to_buffer(raw),
                GattWriteOption.WRITE_WITH_RESPONSE,
            ),
            timeout=self.operation_timeout_seconds,
        )
        if result.status != GattCommunicationStatus.SUCCESS:
            raise RuntimeError(
                f"diagnostic control write failed op={payload.get('op')}: "
                f"status={status_name(result.status)} protocol_error={getattr(result, 'protocol_error', None)}"
            )

    async def pull(self) -> tuple[dict[str, object], bytes]:
        await self.open()

        initial_count = await self.read_count()
        chunks: list[dict[str, object]] = []
        event_bytes = bytearray()
        offset = 0

        try:
            await self.write_control({"op": "start"})
            self.export_started = True
            export_count = await self.read_count()

            while offset < export_count:
                await self.write_control({"op": "read", "offset": offset})
                packet = await asyncio.wait_for(
                    self.notifications.get(),
                    timeout=self.operation_timeout_seconds,
                )
                if len(packet) < DIAG_CHUNK_HEADER_BYTES:
                    raise RuntimeError(f"diagnostic notification too short: {len(packet)} bytes")

                event_count, global_offset, firmware_crc = struct.unpack_from("<HHI", packet, 0)
                payload = packet[DIAG_CHUNK_HEADER_BYTES:]
                expected_payload_len = event_count * DIAG_EVENT_BYTES
                if event_count <= 0:
                    raise RuntimeError(f"diagnostic empty chunk at offset {offset}")
                if len(payload) != expected_payload_len:
                    raise RuntimeError(
                        "diagnostic chunk payload length mismatch: "
                        f"offset={offset} count={event_count} bytes={len(payload)} "
                        f"expected={expected_payload_len}"
                    )
                if global_offset != (offset & 0xFFFF):
                    raise RuntimeError(
                        "diagnostic chunk offset mismatch: "
                        f"host={offset} firmware_header={global_offset}"
                    )

                host_crc = zlib.crc32(payload) & 0xFFFFFFFF
                if host_crc != firmware_crc:
                    raise RuntimeError(
                        "diagnostic chunk CRC mismatch: "
                        f"offset={offset} firmware=0x{firmware_crc:08x} host=0x{host_crc:08x}"
                    )

                chunks.append(
                    {
                        "offset": offset,
                        "event_count": event_count,
                        "value_bytes": len(packet),
                        "events_crc": f"0x{firmware_crc:08x}",
                    }
                )
                event_bytes.extend(payload)
                offset += event_count

                if self.page_delay_seconds > 0 and offset < export_count:
                    await asyncio.sleep(self.page_delay_seconds)

            await self.write_control({"op": "stop"})
            self.export_started = False
            final_count = await self.read_count()
        finally:
            if self.export_started:
                try:
                    await self.write_control({"op": "stop"})
                except Exception:
                    pass
                self.export_started = False

        summary = {
            "initial_count": initial_count,
            "export_count_snapshot": export_count,
            "final_count": final_count,
            "exported_event_count": offset,
            "chunk_count": len(chunks),
            "max_events_per_chunk_observed": max((chunk["event_count"] for chunk in chunks), default=0),
            "max_value_bytes_observed": max((chunk["value_bytes"] for chunk in chunks), default=0),
            "event_bytes": len(event_bytes),
            "aggregate_crc32": f"0x{(zlib.crc32(event_bytes) & 0xFFFFFFFF):08x}",
            "events_sha256": hashlib.sha256(event_bytes).hexdigest(),
            "chunks": chunks,
        }
        return summary, bytes(event_bytes)

    async def close(self) -> None:
        await disable_notify_best_effort(self.data)
        close_ble_objects(
            characteristic=self.data,
            token=self.notify_token,
            service=self.service,
            gatt_session=self.gatt_session,
            requester=self.device,
        )


def build_capture_args(args: argparse.Namespace, artifacts_dir: pathlib.Path, address_hex: str, callback):
    return SimpleNamespace(
        port=args.port,
        device_name=args.device_name,
        bluetooth_address=address_hex,
        capture_seconds=args.capture_seconds,
        output_dir=str(artifacts_dir),
        serial_log_path=str(artifacts_dir / "ble_diag_audio_concurrency_serial.log"),
        timeout_seconds=args.timeout_seconds,
        boot_timeout_seconds=args.boot_timeout_seconds,
        ble_connect_timeout_seconds=args.ble_connect_timeout_seconds,
        notify_ready_timeout_seconds=args.notify_ready_timeout_seconds,
        trigger_mode="serial-toggle",
        max_sessions=1,
        reset_before_capture=args.reset_before_capture,
        session_start_callbacks=[callback],
    )


def extract_evidence_lines(serial_log_path: pathlib.Path) -> dict[str, object]:
    if not serial_log_path.exists():
        return {"serial_log_exists": False}

    lines = serial_log_path.read_text(encoding="utf-8", errors="replace").splitlines()
    diag_lines = [
        line
        for line in lines
        if "ble_diag_log:" in line
        or "diag export" in line
        or "sent chunk offset=" in line
    ]
    audio_lines = [
        line
        for line in lines
        if "audio session transport summary" in line
        or "stream session start queued" in line
        or "stream session stop queued" in line
    ]
    return {
        "serial_log_exists": True,
        "diag_line_count": len(diag_lines),
        "audio_line_count": len(audio_lines),
        "last_diag_lines": diag_lines[-8:],
        "last_audio_lines": audio_lines[-8:],
        "unexpected_reset_count": sum(
            1 for line in lines if "rst:0x" in line or "ESP-ROM:esp32" in line
        ),
    }


async def main_async(args: argparse.Namespace) -> None:
    artifacts_dir = pathlib.Path(args.artifacts_dir)
    artifacts_dir.mkdir(parents=True, exist_ok=True)

    address_hex = (
        normalize_address(args.bluetooth_address)
        if args.bluetooth_address
        else get_paired_device_address_hex(args.device_name)
    )
    if address_hex is None:
        raise RuntimeError(f"unable to resolve paired BLE device {args.device_name!r}")

    diag_task = None
    audio_toggle_time = None

    async def run_diag_pull() -> tuple[dict[str, object], bytes]:
        await asyncio.sleep(args.diag_start_delay_seconds)
        start = time.monotonic()
        puller = DiagnosticLogPuller(
            device_name=args.device_name,
            address_hex=address_hex,
            page_delay_seconds=args.diag_page_delay_seconds,
            operation_timeout_seconds=args.diag_timeout_seconds,
        )
        try:
            summary, raw_events = await puller.pull()
        finally:
            await puller.close()
        end = time.monotonic()
        summary["duration_seconds"] = end - start
        summary["_start_monotonic"] = start
        summary["_end_monotonic"] = end
        return summary, raw_events

    def start_diag_from_audio_session() -> None:
        nonlocal diag_task, audio_toggle_time
        audio_toggle_time = time.monotonic()
        diag_task = asyncio.create_task(run_diag_pull())

    capture_args = build_capture_args(args, artifacts_dir, address_hex, start_diag_from_audio_session)
    audio_summaries = await run_capture_with_args(capture_args)
    if diag_task is None:
        raise RuntimeError("diagnostic export task was not started by the audio session callback")

    diag_summary, raw_events = await asyncio.wait_for(
        diag_task,
        timeout=args.diag_timeout_seconds + args.timeout_seconds,
    )
    events_path = artifacts_dir / "ble_diag_log_export_latest.bin"
    events_path.write_bytes(raw_events)

    audio_summary = audio_summaries[0]
    audio_transport = validate_transport_summary(
        audio_summary,
        capture_seconds=args.capture_seconds,
    )
    if audio_transport["transport_result"] == "fail":
        raise RuntimeError(
            "BLE audio transport failed during diagnostic export: "
            f"reason={audio_transport['transport_failure_reason']}"
        )

    if diag_summary["exported_event_count"] < args.min_diag_events:
        raise RuntimeError(
            "diagnostic export returned too few events: "
            f"{diag_summary['exported_event_count']} < {args.min_diag_events}"
        )
    if diag_summary["chunk_count"] < args.min_diag_chunks:
        raise RuntimeError(
            "diagnostic export returned too few chunks for pagination evidence: "
            f"{diag_summary['chunk_count']} < {args.min_diag_chunks}"
        )
    if diag_summary["exported_event_count"] != diag_summary["export_count_snapshot"]:
        raise RuntimeError(
            "diagnostic export did not complete the snapshot count: "
            f"exported={diag_summary['exported_event_count']} "
            f"snapshot={diag_summary['export_count_snapshot']}"
        )

    diag_start_after_toggle = None
    diag_end_after_toggle = None
    overlap_inferred = False
    if audio_toggle_time is not None:
        diag_start_after_toggle = diag_summary["_start_monotonic"] - audio_toggle_time
        diag_end_after_toggle = diag_summary["_end_monotonic"] - audio_toggle_time
        overlap_inferred = (
            diag_start_after_toggle <= float(args.capture_seconds) + 1.0
            and diag_end_after_toggle >= 0.0
        )
    if not overlap_inferred:
        raise RuntimeError(
            "diagnostic export did not overlap the audio capture window: "
            f"start_after_toggle={diag_start_after_toggle} end_after_toggle={diag_end_after_toggle}"
        )

    diag_summary["diag_start_after_audio_toggle_seconds"] = diag_start_after_toggle
    diag_summary["diag_end_after_audio_toggle_seconds"] = diag_end_after_toggle
    diag_summary["overlap_with_audio_capture_inferred"] = True
    del diag_summary["_start_monotonic"]
    del diag_summary["_end_monotonic"]

    serial_log_path = pathlib.Path(str(audio_summary["serial_log_path"]))
    summary = {
        "status": "PASS",
        "device_name": args.device_name,
        "bluetooth_address": address_hex,
        "audio": {
            "summary": audio_summary,
            "transport": audio_transport,
        },
        "diagnostic_export": diag_summary,
        "serial_evidence": extract_evidence_lines(serial_log_path),
        "artifacts": {
            "events_bin": str(events_path),
            "serial_log": str(serial_log_path),
            "wav": str(audio_summary["wav_path"]),
        },
    }
    summary_path = artifacts_dir / "ble_diag_log_audio_concurrency_summary.json"
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")

    print("PASS: BLE diagnostic log export completed during BLE audio capture.")
    print(f"summary_path={summary_path}")
    print(f"events_bin={events_path}")
    print(f"serial_log={serial_log_path}")
    print(f"wav_path={audio_summary['wav_path']}")
    print(f"diag_exported_event_count={diag_summary['exported_event_count']}")
    print(f"diag_chunk_count={diag_summary['chunk_count']}")
    print(f"diag_aggregate_crc32={diag_summary['aggregate_crc32']}")
    print(f"audio_expected_packet_count={audio_transport['expected_packet_count']}")
    print(f"audio_received_packet_count={audio_transport['received_packet_count']}")
    print(f"audio_missing_packet_count={audio_transport['missing_packet_count']}")
    print(f"audio_packet_loss_ratio={audio_transport['packet_loss_ratio']:.4f}")


def main() -> None:
    configure_utf8_stdio()
    args = parse_args()
    asyncio.run(main_async(args))


if __name__ == "__main__":
    main()
