#!/usr/bin/env python3
"""Machine verifier for the plugged-power state machine.

The verifier is deliberately log-only: it never opens a serial port and never
changes device state.  A separate fixture (relay/physical input/Type log
collector) supplies the text captured for one run; this script turns that
evidence into one bounded JSON artifact and applies the product invariants.
"""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable


SCHEMA = "listener.power-state-machine.v1"
_STAMP = re.compile(r"^[^\n]*?\b(?:I|W|E)\s*\(\s*(\d+)\)")
_TRANSITION = re.compile(
    r"state\s+(?P<previous>[A-Z_]+)\s+->\s+(?P<next>[A-Z_]+)"
    r"\s+reason=(?P<reason>\S+)\s+external_power=(?P<external>[01])"
    r"\s+manual_off_latched=(?P<latched>[01])\s+"
    r"boot_generation=(?P<generation>\d+)"
)
_SOFT_OFF = re.compile(
    r"~POWER:SOFT_OFF\s+state=(?P<state>\S+)"
    r"\s+reason=(?P<reason>\S+)\s+external_power=(?P<external>[01])"
    r"\s+manual_off_latched=(?P<latched>[01])\s+"
    r"boot_generation=(?P<generation>\d+)"
)
_BOOT_GUARD = re.compile(
    r"plugged soft-off boot guard armed:.*?manual_off_latched=(?P<latched>[01])"
    r".*?boot_generation=(?P<generation>\d+)"
)
_POWER_EDGE = re.compile(
    r"power source changed: reason=(?P<reason>\S+)\s+"
    r"external_power=(?P<external>[01])"
)
_STATUS = re.compile(
    r"~POWER:STATUS\s+state=(?P<state>[A-Z_]+).*?"
    r"external_power=(?P<external>[01]).*?"
    r"manual_off_latched=(?P<latched>[01])"
)
_TYPE_FORBIDDEN = re.compile(
    r"(?:VREC:(?:START|TOGGLE)|recording toggle(?: accepted| queued)|"
    r"device_status state=(?:recording|processing)|"
    r"type audio ready accepted|BLE connection state changed: connected=1)",
    re.IGNORECASE,
)
_BOOT = re.compile(r"(?:app_main|power cold-boot status|SYS_BOOT|project_version|"
                   r"ESP-ROM|rst:0x)", re.IGNORECASE)


@dataclass(frozen=True)
class Event:
    kind: str
    line: int
    at_ms: int | None
    state: str | None = None
    previous: str | None = None
    next: str | None = None
    reason: str | None = None
    external_power: int | None = None
    manual_off_latched: int | None = None
    boot_generation: int | None = None
    text: str = ""


def _timestamp(text: str) -> int | None:
    match = _STAMP.search(text)
    return int(match.group(1)) if match else None


def parse_lines(lines: Iterable[str], source: str) -> list[Event]:
    events: list[Event] = []
    for line_number, raw in enumerate(lines, 1):
        text = raw.rstrip("\r\n")
        at_ms = _timestamp(text)
        match = _TRANSITION.search(text)
        if match:
            events.append(Event(
                kind="state_transition", line=line_number, at_ms=at_ms,
                previous=match["previous"], next=match["next"],
                reason=match["reason"], external_power=int(match["external"]),
                manual_off_latched=int(match["latched"]),
                boot_generation=int(match["generation"]), text=text))
            continue
        match = _SOFT_OFF.search(text)
        if match:
            events.append(Event(
                kind="soft_off", line=line_number, at_ms=at_ms,
                state=match["state"], reason=match["reason"],
                external_power=int(match["external"]),
                manual_off_latched=int(match["latched"]),
                boot_generation=int(match["generation"]), text=text))
            continue
        match = _BOOT_GUARD.search(text)
        if match:
            events.append(Event(
                kind="boot_guard", line=line_number, at_ms=at_ms,
                reason="boot_marker", manual_off_latched=int(match["latched"]),
                boot_generation=int(match["generation"]), text=text))
            continue
        match = _POWER_EDGE.search(text)
        if match:
            events.append(Event(
                kind="power_edge", line=line_number, at_ms=at_ms,
                reason=match["reason"], external_power=int(match["external"]),
                text=text))
            continue
        match = _STATUS.search(text)
        if match:
            events.append(Event(
                kind="status", line=line_number, at_ms=at_ms,
                state=match["state"], external_power=int(match["external"]),
                manual_off_latched=int(match["latched"]), text=text))
            continue
        if _TYPE_FORBIDDEN.search(text):
            events.append(Event(kind="type_activity", line=line_number,
                                at_ms=at_ms, text=text))
        elif _BOOT.search(text):
            events.append(Event(kind="boot", line=line_number, at_ms=at_ms,
                                text=text))
    return events


def _delta(start: Event, end: Event) -> int | None:
    if start.at_ms is None or end.at_ms is None:
        return None
    return max(0, end.at_ms - start.at_ms)


def _field_violations(events: list[Event]) -> list[str]:
    violations: list[str] = []
    for event in events:
        if event.kind not in {"state_transition", "soft_off", "boot_guard"}:
            continue
        missing = [name for name, value in (
            ("reason", event.reason),
            ("manual_off_latched", event.manual_off_latched),
            ("boot_generation", event.boot_generation),
        ) if value is None]
        if missing:
            violations.append(
                f"line {event.line} {event.kind} missing fields: {','.join(missing)}")
    return violations


def evaluate(events: list[Event], hold_ms: int) -> dict:
    violations = _field_violations(events)
    soft_off_events = [e for e in events if e.kind == "soft_off" and e.state == "guarded"]
    guard_events = [e for e in events if e.kind == "boot_guard"]
    transition_events = [e for e in events if e.kind == "state_transition"]
    power_edges = [e for e in events if e.kind == "power_edge"]

    manual_pass = False
    manual_detail = "no guarded manual-off event"
    if soft_off_events:
        start = soft_off_events[-1]
        if start.reason != "manual_command" or start.manual_off_latched != 1:
            violations.append("guarded soft-off does not carry manual_command latch")
        later = [e for e in events if e.line > start.line]
        bad = [e for e in later if e.kind in {"boot", "type_activity"}]
        active = [e for e in later if e.kind == "state_transition" and e.next == "ACTIVE"]
        end_ms = next((e for e in later if e.at_ms is not None and start.at_ms is not None
                       and e.at_ms - start.at_ms >= hold_ms), None)
        if bad:
            violations.append(f"manual-off hold has forbidden activity at line {bad[0].line}")
        if active:
            violations.append(f"manual-off hold returned ACTIVE at line {active[0].line}")
        if end_ms is not None and not bad and not active:
            manual_pass = True
            manual_detail = f"guarded for >= {hold_ms} ms"
        elif start.at_ms is None:
            manual_detail = "timestamps unavailable; external hold duration not proven"
        else:
            manual_detail = "hold duration not present in supplied log"

    wake_pass = False
    wake_detail = "no low-power insertion edge and ACTIVE transition"
    idle_indices = [i for i, e in enumerate(transition_events)
                    if e.next in {"CONNECTED_IDLE", "DISCONNECTED_IDLE"}]
    for edge in power_edges:
        if edge.external_power != 1:
            continue
        prior_idle = any(e.line < edge.line and e.next in {"CONNECTED_IDLE", "DISCONNECTED_IDLE"}
                         for e in transition_events)
        if not prior_idle:
            continue
        following = [e for e in transition_events if e.line > edge.line and e.next == "ACTIVE"]
        if not following:
            continue
        first = following[0]
        latency = _delta(edge, first)
        repeats = [e for e in following[1:] if _delta(edge, e) is not None and _delta(edge, e) <= 2000]
        if latency is None or latency > 2000:
            violations.append("low-power plug wake exceeded 2000 ms or lacked timestamps")
        elif repeats:
            violations.append("low-power plug wake produced repeated ACTIVE transitions")
        else:
            wake_pass = True
            wake_detail = f"ACTIVE once after {latency} ms"
        break

    reconnect_pass = False
    reconnect_detail = "not evaluated without a manual-off event"
    if soft_off_events:
        start_line = soft_off_events[-1].line
        forbidden = [e for e in events if e.line > start_line and e.kind == "type_activity"]
        active = [e for e in events if e.line > start_line and e.kind == "state_transition" and e.next == "ACTIVE"]
        reconnect_pass = not forbidden and not active
        reconnect_detail = "no Type wake/recording/active event while latched" if reconnect_pass else "forbidden Type or ACTIVE event observed"

    return {
        "schema": SCHEMA,
        "verdict": "PASS" if manual_pass and wake_pass and reconnect_pass and not violations else "INCOMPLETE" if not violations else "FAIL",
        "checks": {
            "manual_off_hold": {"pass": manual_pass, "detail": manual_detail},
            "low_power_plug_wake": {"pass": wake_pass, "detail": wake_detail},
            "type_reconnect_safety": {"pass": reconnect_pass, "detail": reconnect_detail},
            "telemetry_fields": {"pass": not _field_violations(events), "detail": "all required fields present" if not _field_violations(events) else "missing fields"},
        },
        "violations": violations,
        "event_counts": {kind: sum(e.kind == kind for e in events) for kind in sorted({e.kind for e in events})},
        "events": [asdict(event) for event in events],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware-log", required=True, type=Path)
    parser.add_argument("--type-log", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--hold-ms", type=int, default=60_000)
    args = parser.parse_args()

    lines: list[str] = []
    lines.extend(args.firmware_log.read_text(encoding="utf-8", errors="replace").splitlines())
    if args.type_log:
        lines.extend(args.type_log.read_text(encoding="utf-8", errors="replace").splitlines())
    result = evaluate(parse_lines(lines, str(args.firmware_log)), args.hold_ms)
    result["sources"] = {
        "firmware_log": str(args.firmware_log),
        "type_log": str(args.type_log) if args.type_log else None,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"verdict": result["verdict"], "checks": result["checks"], "violations": result["violations"]}, ensure_ascii=False))
    return 0 if result["verdict"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
