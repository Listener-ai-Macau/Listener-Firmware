#!/usr/bin/env python3
"""Export Listener diag_log events over the BLE diagnostic GATT service."""

from __future__ import annotations

import argparse
import asyncio
import json
import struct
import zlib
from datetime import datetime, timezone
from pathlib import Path

from bleak import BleakClient, BleakScanner


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


async def resolve_target(address: str, name: str, timeout: float) -> str:
    if address:
        wanted = normalize_address(address)
        devices = await BleakScanner.discover(timeout=timeout)
        for device in devices:
            if normalize_address(device.address) == wanted:
                return device.address
        return address

    devices = await BleakScanner.discover(timeout=timeout)
    for device in devices:
        if (device.name or "").lower() == name.lower():
            return device.address
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


async def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--address", default="A4:CB:8F:F4:59:A6")
    parser.add_argument("--name", default="listener")
    parser.add_argument("--count", type=int, default=120)
    parser.add_argument("--output", required=True)
    parser.add_argument("--scan-timeout", type=float, default=8.0)
    parser.add_argument("--notify-timeout", type=float, default=5.0)
    args = parser.parse_args()

    target = await resolve_target(args.address, args.name, args.scan_timeout)
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    queue: asyncio.Queue[bytes] = asyncio.Queue()
    chunks: list[dict] = []
    events: list[dict] = []

    def on_notify(_sender, data: bytearray):
        queue.put_nowait(bytes(data))

    async with BleakClient(target) as client:
        count = decode_count(await client.read_gatt_char(COUNT_UUID))
        start_offset = max(0, count - max(args.count, 1))
        await client.start_notify(DATA_UUID, on_notify)
        await client.write_gatt_char(CONTROL_UUID, b'{"op":"start"}', response=True)

        offset = start_offset
        while offset < count:
            await client.write_gatt_char(
                CONTROL_UUID,
                json.dumps({"op": "read", "offset": offset}, separators=(",", ":")).encode("ascii"),
                response=True,
            )
            data = await asyncio.wait_for(queue.get(), timeout=args.notify_timeout)
            if len(data) < CHUNK_HEADER.size:
                raise RuntimeError(f"Short BLE diag chunk at offset {offset}: {len(data)} bytes")

            event_count, chunk_offset, expected_crc = CHUNK_HEADER.unpack_from(data)
            event_bytes = data[CHUNK_HEADER.size:]
            actual_crc = zlib.crc32(event_bytes) & 0xFFFFFFFF
            chunk = {
                "captured_at": now_iso(),
                "requested_offset": offset,
                "chunk_offset": chunk_offset,
                "event_count": event_count,
                "expected_crc32": expected_crc,
                "actual_crc32": actual_crc,
                "crc32_matches": expected_crc == actual_crc,
                "byte_count": len(data),
            }
            chunks.append(chunk)

            if event_count == 0:
                break
            if len(event_bytes) < event_count * EVENT.size:
                raise RuntimeError(f"Truncated BLE diag events at offset {chunk_offset}")

            for index in range(event_count):
                begin = index * EVENT.size
                end = begin + EVENT.size
                events.append(decode_event(chunk_offset + index, event_bytes[begin:end]))
            offset = chunk_offset + event_count

        await client.write_gatt_char(CONTROL_UUID, b'{"op":"stop"}', response=True)
        await client.stop_notify(DATA_UUID)

    with output_path.open("w", encoding="utf-8") as handle:
        manifest = {
            "schema": "listener.diag_log.ble_export_manifest.v1",
            "captured_at": now_iso(),
            "target": target,
            "total_count": count,
            "requested_count": args.count,
            "start_offset": start_offset,
            "event_count": len(events),
            "chunks": chunks,
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
