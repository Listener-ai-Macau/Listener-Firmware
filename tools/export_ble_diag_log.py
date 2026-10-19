#!/usr/bin/env python3
"""Export Listener diag_log events over the BLE diagnostic GATT service."""

from __future__ import annotations

import argparse
import asyncio
import json
import struct
import time
import zlib
from datetime import datetime, timezone
from pathlib import Path

from bleak import BleakClient, BleakScanner
from bleak.backends.device import BLEDevice


SERVICE_UUID = "710af845-6d9f-6583-0c4d-9e5b3bc3093a"
CONTROL_UUID = "710af845-6d9f-6583-0c4d-9e5b3bc3093b"
DATA_UUID = "710af845-6d9f-6583-0c4d-9e5b3bc3093c"
COUNT_UUID = "710af845-6d9f-6583-0c4d-9e5b3bc3093d"

CHUNK_HEADER = struct.Struct("<HII")
EVENT = struct.Struct("<I H B B I I I I")


def now_iso() -> str:
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="milliseconds")


def normalize_address(value: str) -> str:
    return "".join(ch for ch in value.upper() if ch in "0123456789ABCDEF")


async def resolve_target(address: str, name: str, timeout: float) -> BLEDevice:
    if address:
        wanted = normalize_address(address)
        if len(wanted) != 12:
            raise ValueError("An explicit Listener Bluetooth address must contain12 hex digits")
        # A connected or low-power bonded device commonly stops advertising.
        # Supplying a BLEDevice lets Bleak's WinRT backend open the cached
        # Bluetooth address directly instead of performing a second mandatory
        # discovery pass that can never see the device in that state.
        # The explicit address is the target, not a discovery hint. Starting a
        # scanner here reconfigures Windows' shared radio schedule even when
        # this exact bonded device is already connected and discoverable via
        # GATT. Name-only discovery remains available below.
        canonical = ":".join(wanted[i:i+2] for i in range(0, 12, 2))
        return BLEDevice(canonical, name, None)

    devices = await BleakScanner.discover(timeout=timeout)
    for device in devices:
        if (device.name or "").lower() == name.lower():
            return device
    raise RuntimeError(f"Unable to find BLE device named {name!r}")


def decode_count(raw: bytes) -> int:
    text = raw.decode("utf-8", errors="replace").strip("\x00\r\n ")
    parsed = json.loads(text)
    return int(parsed["count"])


def decode_event(offset: int, raw: bytes) -> dict:
    timestamp_ms, source, event, severity, a1, a2, a3, a4 = EVENT.unpack(raw)
    return {
        "schema": "listener.diag_log.ble_event.v1",
        "captured_at": now_iso(),
        "global_offset": offset,
        "timestamp_ms": timestamp_ms,
        "source": source,
        "event": event,
        "severity": severity,
        "a1": a1,
        "a2": a2,
        "a3": a3,
        "a4": a4,
    }


async def read_matching_chunk(
    queue: asyncio.Queue[bytes], requested_offset: int, timeout: float
) -> tuple[bytes, int, int, int]:
    """Ignore non-export notifications sharing the diagnostic data characteristic.

    The firmware also sends the ASCII OTA-ready marker on this characteristic.
    A normal Listener Type connection can therefore leave that marker ahead of
    the requested binary log chunk in the WinRT notification queue.
    """
    deadline = time.monotonic() + timeout
    last_rejection = "no notification"
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise RuntimeError(
                f"Timed out waiting for BLE diag chunk at offset {requested_offset}; "
                f"last rejected notification: {last_rejection}"
            )
        data = await asyncio.wait_for(queue.get(), timeout=remaining)
        if len(data) < CHUNK_HEADER.size:
            last_rejection = f"short packet ({len(data)} bytes)"
            continue

        event_count, chunk_offset, expected_crc = CHUNK_HEADER.unpack_from(data)
        event_bytes = data[CHUNK_HEADER.size:]
        expected_len = event_count * EVENT.size
        if event_count <= 0 or event_count > 4:
            last_rejection = f"invalid event count {event_count}"
            continue
        if chunk_offset != requested_offset:
            last_rejection = (
                f"offset mismatch firmware={chunk_offset} host={requested_offset}"
            )
            continue
        if len(event_bytes) != expected_len:
            last_rejection = (
                f"payload mismatch bytes={len(event_bytes)} expected={expected_len}"
            )
            continue
        actual_crc = zlib.crc32(event_bytes) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            last_rejection = (
                f"CRC mismatch firmware=0x{expected_crc:08x} host=0x{actual_crc:08x}"
            )
            continue
        return data, event_count, chunk_offset, actual_crc


async def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--address", default="A4:CB:8F:F4:59:A6")
    parser.add_argument("--name", default="listener")
    parser.add_argument("--count", type=int, default=120)
    parser.add_argument("--output", required=True)
    parser.add_argument("--scan-timeout", type=float, default=8.0)
    parser.add_argument("--notify-timeout", type=float, default=5.0)
    parser.add_argument("--connect-timeout", type=float, default=8.0)
    args = parser.parse_args()

    started = time.monotonic()
    def phase(label: str) -> None:
        print(f"BLE_DIAG_PHASE: {label} elapsed_ms={int((time.monotonic()-started)*1000)}", flush=True)
    phase("resolve_explicit_address" if args.address else "discover_name")
    target = await resolve_target(args.address, args.name, args.scan_timeout)
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    queue: asyncio.Queue[bytes] = asyncio.Queue()
    chunks: list[dict] = []
    events: list[dict] = []
    count = start_offset = 0

    def persist_partial() -> None:
        # Keep validated evidence even if a later GATT operation or cleanup
        # stalls. An incomplete file never authorizes a completed export.
        with output_path.open("w", encoding="utf-8") as handle:
            handle.write(json.dumps({"schema": "listener.diag_log.ble_export_manifest.v1",
                "captured_at": now_iso(), "target": target.address,
                "total_count": count, "requested_count": args.count,
                "start_offset": start_offset, "event_count": len(events),
                "chunks": chunks, "complete": False}, sort_keys=True)+"\n")
            for event in events: handle.write(json.dumps(event, sort_keys=True)+"\n")

    async def bounded(operation):
        return await asyncio.wait_for(operation, timeout=args.notify_timeout)

    def on_notify(_sender, data: bytearray):
        queue.put_nowait(bytes(data))

    phase("connect_and_discover")
    async with BleakClient(target, timeout=args.connect_timeout,
                           winrt={"use_cached_services": False}) as client:
        phase("read_count")
        count = decode_count(await bounded(client.read_gatt_char(COUNT_UUID, use_cached=False)))
        start_offset = max(0, count - max(args.count, 1))
        persist_partial()
        await bounded(client.start_notify(DATA_UUID, on_notify))
        phase("export_chunks")
        export_started = False
        try:
            await bounded(client.write_gatt_char(CONTROL_UUID, b'{"op":"start"}', response=True))
            export_started = True

            offset = start_offset
            while offset < count:
                phase(f"read_chunk_{offset}")
                await bounded(client.write_gatt_char(
                    CONTROL_UUID,
                    json.dumps({"op": "read", "offset": offset}, separators=(",", ":")).encode("ascii"),
                    response=True,
                ))
                data, event_count, chunk_offset, actual_crc = await read_matching_chunk(
                    queue, offset, args.notify_timeout
                )
                _, _, expected_crc = CHUNK_HEADER.unpack_from(data)
                event_bytes = data[CHUNK_HEADER.size:]
                chunks.append({
                    "captured_at": now_iso(),
                    "requested_offset": offset,
                    "chunk_offset": chunk_offset,
                    "event_count": event_count,
                    "expected_crc32": expected_crc,
                    "actual_crc32": actual_crc,
                    "crc32_matches": True,
                    "byte_count": len(data),
                })

                for index in range(event_count):
                    begin = index * EVENT.size
                    end = begin + EVENT.size
                    events.append(decode_event(chunk_offset + index, event_bytes[begin:end]))
                offset = chunk_offset + event_count
                persist_partial()
        finally:
            persist_partial()
            if export_started:
                try:
                    phase("stop_export")
                    await bounded(client.write_gatt_char(CONTROL_UUID, b'{"op":"stop"}', response=True))
                except Exception:
                    pass
            phase("stop_own_notify")
            await bounded(client.stop_notify(DATA_UUID))

    with output_path.open("w", encoding="utf-8") as handle:
        manifest = {
            "schema": "listener.diag_log.ble_export_manifest.v1",
            "captured_at": now_iso(),
            "target": target.address,
            "total_count": count,
            "requested_count": args.count,
            "start_offset": start_offset,
            "event_count": len(events),
            "chunks": chunks,
            "complete": True,
        }
        handle.write(json.dumps(manifest, sort_keys=True) + "\n")
        for event in events:
            handle.write(json.dumps(event, sort_keys=True) + "\n")

    print(f"BLE_DIAG_LOG: {output_path}")
    print(f"BLE_DIAG_EVENTS: {len(events)}")
    print(f"BLE_DIAG_TOTAL_COUNT: {count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
