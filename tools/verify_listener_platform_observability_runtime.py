#!/usr/bin/env python3
"""Validate bounded OTA diagnostics captured from a live Listener device."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


EVENT_PATTERN = re.compile(r"\{\"t\":(?P<t>\d+),\"src\":\"(?P<src>[^\"]+)\",\"evt\":(?P<evt>\d+),\"sev\":\"(?P<sev>[^\"]+)\",\"a1\":(?P<a1>\d+),\"a2\":(?P<a2>\d+),\"a3\":(?P<a3>\d+),\"a4\":(?P<a4>\d+)\}")

DIAG_OTA_BEGIN = 2
DIAG_OTA_VERIFY = 4
DIAG_OTA_SET_BOOT = 5
DIAG_OTA_MARK_VALID = 8
DIAG_OTA_CORRELATION = 14


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Check a bounded Listener OTA diagnostic capture for persistent causal evidence."
    )
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def load_events(raw: str) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    for index, line in enumerate(raw.splitlines(), start=1):
        match = EVENT_PATTERN.search(line)
        if not match:
            continue
        event = {key: int(value) if key not in {"src", "sev"} else value for key, value in match.groupdict().items()}
        event["line"] = index
        events.append(event)
    return events


def main() -> int:
    args = parse_args()
    raw = args.input.read_text(encoding="utf-8", errors="replace")
    events = load_events(raw)
    failures: list[str] = []

    if "serial_closed" not in raw:
        failures.append("serial capture did not reach terminal serial_closed")
    if "DIAGLOG LAST" not in raw or "source=ota:" not in raw:
        failures.append("capture is not a bounded ota diagnostic export")
    if not events:
        failures.append("capture contains no diagnostic JSON events")

    ota_events = [event for event in events if event["src"] == "ota"]
    if any(event["sev"] != "INFO" for event in ota_events):
        failures.append("OTA diagnostic export contains WARN or ERROR events")

    correlation_indexes = [
        index for index, event in enumerate(ota_events) if event["evt"] == DIAG_OTA_CORRELATION
    ]
    begin_indexes = [
        index for index, event in enumerate(ota_events) if event["evt"] == DIAG_OTA_BEGIN
    ]
    verify_events = [event for event in ota_events if event["evt"] == DIAG_OTA_VERIFY]
    set_boot_events = [event for event in ota_events if event["evt"] == DIAG_OTA_SET_BOOT]
    valid_events = [event for event in ota_events if event["evt"] == DIAG_OTA_MARK_VALID]

    if not correlation_indexes:
        failures.append("missing persisted OTA correlation event")
    if not begin_indexes:
        failures.append("missing OTA begin event")
    if correlation_indexes and begin_indexes and max(correlation_indexes) >= max(begin_indexes):
        failures.append("latest OTA correlation must precede its begin event")
    if not any(event["a3"] == 0 for event in verify_events):
        failures.append("missing successful OTA verify event")
    if not any(event["a3"] == 0 for event in set_boot_events):
        failures.append("missing successful OTA set-boot event")
    if not any(event["a3"] == 0 for event in valid_events):
        failures.append("missing successful post-reboot OTA mark-valid event")

    result = {
        "status": "PASS" if not failures else "FAIL",
        "input": str(args.input),
        "event_count": len(events),
        "ota_event_count": len(ota_events),
        "event_counts": {
            "correlation": len(correlation_indexes),
            "begin": len(begin_indexes),
            "verify_success": sum(event["a3"] == 0 for event in verify_events),
            "set_boot_success": sum(event["a3"] == 0 for event in set_boot_events),
            "mark_valid_success": sum(event["a3"] == 0 for event in valid_events),
        },
        "failures": failures,
    }
    rendered = json.dumps(result, ensure_ascii=True, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(f"{rendered}\n", encoding="utf-8")
    print(rendered)
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
