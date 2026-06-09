#!/usr/bin/env python3
"""Monitor BLE advertisements for a named/addressed device and emit JSONL."""

from __future__ import annotations

import argparse
import asyncio
import json
from pathlib import Path
from datetime import datetime, timezone
from time import monotonic

from bleak import BleakScanner


def normalize_address(value: str | None) -> str:
    if not value:
        return ""
    return "".join(ch for ch in value.upper() if ch in "0123456789ABCDEF")


def utc_timestamp() -> str:
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="milliseconds")


def iter_discovered(result):
    if isinstance(result, dict):
        for item in result.values():
            if isinstance(item, tuple) and len(item) >= 2:
                yield item[0], item[1]
            else:
                yield item, None
    else:
        for device in result:
            yield device, None


async def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--address", default="")
    parser.add_argument("--name", default="listener")
    parser.add_argument("--duration", type=float, default=120.0)
    parser.add_argument("--interval", type=float, default=3.0)
    parser.add_argument("--output", default="")
    args = parser.parse_args()

    target_address = normalize_address(args.address)
    target_name = args.name.lower()
    deadline = monotonic() + args.duration
    output = Path(args.output) if args.output else None
    output_handle = output.open("a", encoding="utf-8") if output else None

    try:
        while monotonic() < deadline:
            timeout = max(0.5, min(args.interval, deadline - monotonic()))
            record = {
                "timestamp": utc_timestamp(),
                "target_seen": False,
                "matches": [],
                "error": None,
            }

            try:
                try:
                    discovered = await BleakScanner.discover(timeout=timeout, return_adv=True)
                except TypeError:
                    discovered = await BleakScanner.discover(timeout=timeout)

                for device, advertisement in iter_discovered(discovered):
                    address = getattr(device, "address", "") or ""
                    name = getattr(device, "name", "") or ""
                    address_matches = target_address and normalize_address(address) == target_address
                    name_matches = bool(target_name and name and name.lower() == target_name)
                    if not address_matches and not name_matches:
                        continue

                    record["target_seen"] = True
                    match = {
                        "address": address,
                        "name": name,
                        "rssi": getattr(device, "rssi", None),
                    }
                    if advertisement is not None:
                        match["adv_rssi"] = getattr(advertisement, "rssi", None)
                        match["local_name"] = getattr(advertisement, "local_name", None)
                        match["service_uuids"] = sorted(getattr(advertisement, "service_uuids", []) or [])
                    record["matches"].append(match)
            except Exception as exc:  # noqa: BLE scanners are platform dependent.
                record["error"] = f"{type(exc).__name__}: {exc}"

            line = json.dumps(record, sort_keys=True)
            print(line, flush=True)
            if output_handle:
                output_handle.write(line + "\n")
                output_handle.flush()
    finally:
        if output_handle:
            output_handle.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
