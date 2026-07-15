#!/usr/bin/env python3
"""Verify the device-controlled portion of an EC11 second-host pairing session."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


GESTURE_TO_ADVERTISING_LIMIT_MS = 250
CONNECT_TO_SECURE_BOND_LIMIT_MS = 1200
TIMESTAMP_RE = re.compile(r"\b[IW]\s+\((\d+)\)")


def timestamp_ms(line: str) -> int | None:
    match = TIMESTAMP_RE.search(line)
    return int(match.group(1)) if match else None


def find_last_event(lines: list[str], marker: str) -> tuple[int, int] | None:
    match: tuple[int, int] | None = None
    for index, line in enumerate(lines):
        if marker not in line:
            continue
        value = timestamp_ms(line)
        if value is not None:
            match = (index, value)
    return match


def find_first_event_after(
    lines: list[str], start_index: int, marker: str
) -> tuple[int, int] | None:
    for index in range(start_index + 1, len(lines)):
        if marker not in lines[index]:
            continue
        value = timestamp_ms(lines[index])
        if value is not None:
            return index, value
    return None


def find_first_secure_bond_after(
    lines: list[str], start_index: int
) -> tuple[int, int] | None:
    for index in range(start_index + 1, len(lines)):
        line = lines[index]
        if (
            "security state after encryption:" not in line
            or "encrypted=1" not in line
            or "bonded=1" not in line
        ):
            continue
        value = timestamp_ms(line)
        if value is not None:
            return index, value
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("serial_log", type=Path, help="locked COM serial capture")
    args = parser.parse_args()

    if not args.serial_log.is_file():
        print(f"FAIL: serial log does not exist: {args.serial_log}")
        return 1

    lines = args.serial_log.read_text(encoding="utf-8", errors="replace").splitlines()
    gesture = find_last_event(lines, "recovery double-click accepted")
    failures: list[str] = []
    if gesture is None:
        failures.append("missing accepted physical EC11 recovery double-click")
        report = {"status": "FAIL", "serial_log": str(args.serial_log), "failures": failures}
        print(json.dumps(report, ensure_ascii=True, indent=2))
        return 1

    advertising = find_first_event_after(
        lines, gesture[0], "NimBLE undirected advertising started"
    )
    connection = find_first_event_after(
        lines, gesture[0], "global GAP listener connection established"
    )
    secure_bond = find_first_secure_bond_after(lines, gesture[0])

    if advertising is None:
        failures.append("missing undirected recovery advertising after EC11 double-click")
    if connection is None:
        failures.append("missing incoming second-host BLE connection after EC11 double-click")
    if secure_bond is None:
        failures.append("missing successful encryption after EC11 double-click")

    metrics: dict[str, int] = {}
    if advertising is not None:
        metrics["gesture_to_advertising_ms"] = advertising[1] - gesture[1]
        if metrics["gesture_to_advertising_ms"] > GESTURE_TO_ADVERTISING_LIMIT_MS:
            failures.append(
                "EC11 gesture to recovery advertising exceeded "
                f"{GESTURE_TO_ADVERTISING_LIMIT_MS} ms: "
                f"{metrics['gesture_to_advertising_ms']} ms"
            )
    if connection is not None and secure_bond is not None:
        metrics["connection_to_secure_bond_ms"] = secure_bond[1] - connection[1]
        if metrics["connection_to_secure_bond_ms"] > CONNECT_TO_SECURE_BOND_LIMIT_MS:
            failures.append(
                "Windows connection to secure bond exceeded "
                f"{CONNECT_TO_SECURE_BOND_LIMIT_MS} ms: "
                f"{metrics['connection_to_secure_bond_ms']} ms"
            )

    report = {
        "status": "PASS" if not failures else "FAIL",
        "serial_log": str(args.serial_log),
        "thresholds_ms": {
            "gesture_to_advertising": GESTURE_TO_ADVERTISING_LIMIT_MS,
            "connection_to_secure_bond": CONNECT_TO_SECURE_BOND_LIMIT_MS,
        },
        "observed_ms": metrics,
        "failures": failures,
    }
    print(json.dumps(report, ensure_ascii=True, indent=2))
    if failures:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
