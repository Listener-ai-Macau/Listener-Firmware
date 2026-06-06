#!/usr/bin/env python3
"""Decode firmware diag_log JSONL into a stable AI-readable JSON bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys
from collections import Counter, defaultdict
from typing import Any


BUNDLE_SCHEMA_VERSION = 1
BUNDLE_SCHEMA_ID = "listener.firmware.diag_log.ai_bundle.v1"
ARG_SLOTS = ("a1", "a2", "a3", "a4")

DEFINE_RE = re.compile(
    r"^\s*#define\s+([A-Z0-9_]+)\s+([^\s/]+)(?:\s*/\*\s*(.*?)\s*\*/)?\s*$"
)
SECTION_RE = re.compile(r"/\*\s*.*?events\s*\((DIAG_SRC_[A-Z0-9_]+)\)\s*\*/")


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_int_token(token: str) -> int | None:
    try:
        return int(token.rstrip("ULul"), 0)
    except ValueError:
        return None


def source_name_from_macro(macro: str) -> str:
    return macro.removeprefix("DIAG_SRC_").lower()


def event_name_from_macro(macro: str) -> str:
    return macro.removeprefix("DIAG_").lower()


def split_top_level_commas(text: str) -> list[str]:
    parts: list[str] = []
    current: list[str] = []
    depth = 0
    for char in text:
        if char == "(":
            depth += 1
        elif char == ")" and depth > 0:
            depth -= 1
        if char == "," and depth == 0:
            part = "".join(current).strip()
            if part:
                parts.append(part)
            current = []
            continue
        current.append(char)
    part = "".join(current).strip()
    if part:
        parts.append(part)
    return parts


def sanitize_arg_name(name: str, slot: str) -> str:
    clean = name.strip()
    if clean == "0":
        return f"unused_{slot}"
    clean = re.sub(r"\([^)]*\)", "", clean)
    clean = clean.replace("/", "_or_")
    clean = re.sub(r"[^0-9A-Za-z_]+", "_", clean).strip("_").lower()
    if not clean:
        clean = slot
    if clean[0].isdigit():
        clean = f"value_{clean}"
    return clean


def parse_enum_hints(detail: str) -> dict[str, str]:
    match = re.search(r"\(([^)]*)\)", detail)
    if not match:
        return {}
    hints: dict[str, str] = {}
    for part in split_top_level_commas(match.group(1)):
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        key = key.strip()
        value = value.strip()
        if re.fullmatch(r"0x[0-9A-Fa-f]+|\d+", key) and value:
            parsed = parse_int_token(key)
            hints[str(parsed if parsed is not None else key)] = value
    return hints


def parse_arg_specs(comment: str | None) -> dict[str, dict[str, Any]]:
    if not comment:
        return {}
    specs: dict[str, dict[str, Any]] = {}
    for part in split_top_level_commas(comment):
        match = re.match(r"^(a[1-4])\s*=\s*(.+)$", part.strip())
        if not match:
            continue
        slot = match.group(1)
        detail = match.group(2).strip()
        raw_name = re.sub(r"\([^)]*\)", "", detail).strip()
        name = sanitize_arg_name(raw_name, slot)
        specs[slot] = {
            "name": name,
            "raw_name": raw_name,
            "detail": detail,
            "enum": parse_enum_hints(detail),
        }
    return specs


def load_event_schema(header_path: pathlib.Path) -> dict[str, Any]:
    macros: dict[str, int] = {}
    sources_by_macro: dict[str, dict[str, Any]] = {}
    sources_by_name: dict[str, dict[str, Any]] = {}
    sources_by_id: dict[int, dict[str, Any]] = {}
    severity_by_name: dict[str, dict[str, Any]] = {}
    severity_by_id: dict[int, dict[str, Any]] = {}
    events_by_source_value: dict[tuple[str, int], dict[str, Any]] = {}
    constants_by_group: dict[str, dict[str, dict[str, Any]]] = defaultdict(dict)

    current_event_source: str | None = None
    for line_number, line in enumerate(header_path.read_text(encoding="utf-8").splitlines(), 1):
        section = SECTION_RE.search(line)
        if section:
            current_event_source = section.group(1)
            continue

        match = DEFINE_RE.match(line)
        if not match:
            continue

        macro, raw_value, comment = match.groups()
        value = parse_int_token(raw_value)
        if value is None:
            continue
        macros[macro] = value

        if macro.startswith("DIAG_SRC_"):
            if value in sources_by_id:
                existing = sources_by_id[value]
                raise ValueError(
                    f"duplicate diag_log source id 0x{value:02X}: "
                    f"{existing['macro']} line {existing['line']} and {macro} line {line_number}"
                )
            source = {
                "id": value,
                "name": source_name_from_macro(macro),
                "macro": macro,
                "line": line_number,
            }
            sources_by_macro[macro] = source
            sources_by_name[source["name"]] = source
            sources_by_id[value] = source
            continue

        if macro.startswith("DIAG_SEV_"):
            name = macro.removeprefix("DIAG_SEV_")
            severity = {
                "level": value,
                "name": name,
                "macro": macro,
                "line": line_number,
            }
            severity_by_name[name] = severity
            severity_by_id[value] = severity
            continue

        if current_event_source and comment and "a1=" in comment:
            event_key = (current_event_source, value)
            if event_key in events_by_source_value:
                existing = events_by_source_value[event_key]
                raise ValueError(
                    f"duplicate diag_log event id {value} for {current_event_source}: "
                    f"{existing['macro']} line {existing['line']} and {macro} line {line_number}"
                )
            event = {
                "value": value,
                "name": event_name_from_macro(macro),
                "macro": macro,
                "source_macro": current_event_source,
                "comment": comment,
                "args": parse_arg_specs(comment),
                "line": line_number,
            }
            events_by_source_value[event_key] = event
            continue

        group = "_".join(macro.split("_")[:2])
        constants_by_group[group][str(value)] = {
            "value": value,
            "macro": macro,
            "name": event_name_from_macro(macro),
            "line": line_number,
        }

    return {
        "header_path": str(header_path),
        "header_sha256": sha256_file(header_path),
        "sources_by_macro": sources_by_macro,
        "sources_by_name": sources_by_name,
        "sources_by_id": sources_by_id,
        "severity_by_name": severity_by_name,
        "severity_by_id": severity_by_id,
        "events_by_source_value": events_by_source_value,
        "constants_by_group": dict(constants_by_group),
        "macro_count": len(macros),
    }


def extract_json_object(line: str) -> dict[str, Any] | None:
    start = line.find("{")
    if start < 0:
        return None
    candidate = line[start:].strip()
    try:
        parsed, _end = json.JSONDecoder().raw_decode(candidate)
    except json.JSONDecodeError:
        return None
    if isinstance(parsed, dict):
        return parsed
    return None


def normalize_severity(raw: Any, schema: dict[str, Any]) -> dict[str, Any]:
    if isinstance(raw, str):
        name = raw.upper()
        info = schema["severity_by_name"].get(name)
    else:
        value = int(raw) if isinstance(raw, int) else None
        info = schema["severity_by_id"].get(value) if value is not None else None
    if info:
        return dict(info)
    return {"level": None, "name": str(raw).upper() if raw is not None else "UNKNOWN", "macro": None}


def normalize_source(raw: Any, schema: dict[str, Any]) -> dict[str, Any]:
    if isinstance(raw, str):
        name = raw.lower()
        info = schema["sources_by_name"].get(name)
    else:
        value = int(raw) if isinstance(raw, int) else None
        info = schema["sources_by_id"].get(value) if value is not None else None
    if info:
        return dict(info)
    return {"id": raw if isinstance(raw, int) else None, "name": str(raw), "macro": None}


def constant_label(group: str, value: Any, schema: dict[str, Any]) -> str | None:
    if not isinstance(value, int):
        return None
    entry = schema["constants_by_group"].get(group, {}).get(str(value))
    if not entry:
        return None
    return entry["name"]


def decode_arg_value(
    source: dict[str, Any],
    event_def: dict[str, Any] | None,
    arg_name: str,
    value: Any,
    schema: dict[str, Any],
) -> str | None:
    if not isinstance(value, int):
        return None

    if event_def:
        spec = event_def.get("args", {}).get(arg_name)
        if spec:
            label = spec.get("enum", {}).get(str(value))
            if label:
                return label

    if arg_name in ("a1", "a2", "a3", "a4") and event_def:
        spec_name = event_def.get("args", {}).get(arg_name, {}).get("name", "")
    else:
        spec_name = arg_name

    if spec_name == "boot_reason":
        return constant_label("DIAG_BOOT", value, schema)
    if spec_name in ("component", "component_id"):
        return constant_label("DIAG_COMP", value, schema)
    if spec_name == "event" and source.get("macro"):
        ota_event = schema["events_by_source_value"].get((source["macro"], value))
        return ota_event["name"] if ota_event else None
    if spec_name == "key_ascii" and 32 <= value <= 126:
        return chr(value)
    return None


def decode_event(raw: dict[str, Any], line_number: int, event_index: int, schema: dict[str, Any]) -> dict[str, Any]:
    source = normalize_source(raw.get("src"), schema)
    severity = normalize_severity(raw.get("sev"), schema)
    evt_value = raw.get("evt")
    if isinstance(evt_value, str) and evt_value.isdigit():
        evt_value = int(evt_value)
    event_def = None
    if source.get("macro") and isinstance(evt_value, int):
        event_def = schema["events_by_source_value"].get((source["macro"], evt_value))

    raw_args = {slot: raw.get(slot) for slot in ARG_SLOTS}
    arg_definitions: dict[str, Any] = {}
    args_named: dict[str, Any] = {}
    args_decoded: dict[str, Any] = {}

    for slot in ARG_SLOTS:
        value = raw_args[slot]
        spec = event_def.get("args", {}).get(slot) if event_def else None
        if spec:
            arg_definitions[slot] = spec
            name = spec["name"]
            if not name.startswith("unused_"):
                key = name
                if key in args_named:
                    key = f"{name}_{slot}"
                args_named[key] = value
                label = decode_arg_value(source, event_def, slot, value, schema)
                if label is not None:
                    args_decoded[key] = label
        else:
            arg_definitions[slot] = {"name": slot, "raw_name": slot, "detail": slot, "enum": {}}

    event_name = event_def["name"] if event_def else f"unknown_evt_{evt_value}"
    event_macro = event_def["macro"] if event_def else None

    return {
        "index": event_index,
        "line_number": line_number,
        "boot_segment_index": 0,
        "t_ms": raw.get("t"),
        "source": {
            "raw": raw.get("src"),
            "id": source.get("id"),
            "name": source.get("name"),
            "macro": source.get("macro"),
        },
        "event": {
            "raw": evt_value,
            "name": event_name,
            "macro": event_macro,
            "comment": event_def.get("comment") if event_def else None,
        },
        "severity": {
            "raw": raw.get("sev"),
            "level": severity.get("level"),
            "name": severity.get("name"),
            "macro": severity.get("macro"),
        },
        "args_raw": raw_args,
        "args_named": args_named,
        "args_decoded": args_decoded,
        "arg_definitions": arg_definitions,
        "raw_event": raw,
    }


def assign_boot_segments(events: list[dict[str, Any]]) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    segments: list[dict[str, Any]] = []
    current_index = 0
    last_t: int | None = None
    reset_refs: list[dict[str, Any]] = []

    def ensure_segment(index: int, event: dict[str, Any]) -> None:
        while len(segments) <= index:
            segments.append(
                {
                    "index": len(segments),
                    "start_event_index": event["index"],
                    "end_event_index": event["index"],
                    "event_count": 0,
                    "start_t_ms": event.get("t_ms"),
                    "end_t_ms": event.get("t_ms"),
                    "duration_ms": 0,
                    "boot_reason": None,
                }
            )

    for event in events:
        t_ms = event.get("t_ms")
        if isinstance(t_ms, int) and last_t is not None and t_ms < last_t:
            current_index += 1
            reset_refs.append(
                {
                    "event_index": event["index"],
                    "previous_t_ms": last_t,
                    "current_t_ms": t_ms,
                    "new_boot_segment_index": current_index,
                }
            )
        if isinstance(t_ms, int):
            last_t = t_ms

        ensure_segment(current_index, event)
        event["boot_segment_index"] = current_index
        segment = segments[current_index]
        segment["end_event_index"] = event["index"]
        segment["event_count"] += 1
        if segment["start_t_ms"] is None:
            segment["start_t_ms"] = t_ms
        segment["end_t_ms"] = t_ms
        if isinstance(segment["start_t_ms"], int) and isinstance(segment["end_t_ms"], int):
            segment["duration_ms"] = max(0, segment["end_t_ms"] - segment["start_t_ms"])

        if event["source"]["name"] == "system" and event["event"]["name"] == "sys_boot":
            segment["boot_reason"] = {
                "value": event["args_raw"].get("a1"),
                "label": event["args_decoded"].get("boot_reason"),
                "event_index": event["index"],
            }

    return segments, reset_refs


def sorted_counter(counter: Counter[str]) -> dict[str, int]:
    return {key: counter[key] for key in sorted(counter)}


def build_input_debug_summary(events: list[dict[str, Any]]) -> dict[str, Any]:
    key_summary = {
        f"KEY{i}": {"press": 0, "release": 0, "single": 0, "double": 0, "long": 0}
        for i in range(1, 5)
    }
    ec11_summary: dict[str, Any] = {
        "clockwise": 0,
        "counterclockwise": 0,
        "last_detent_count": None,
        "press": 0,
        "double_click_recovery": 0,
        "long_press_ignored": 0,
    }

    key_phase_names = {
        1: "press",
        2: "release",
        3: "single",
        4: "double",
        5: "long",
    }
    ec11_direction_names = {
        1: "clockwise",
        2: "counterclockwise",
    }
    ec11_press_names = {
        1: "press",
        2: "double_click_recovery",
        3: "long_press_ignored",
    }

    for event in events:
        source_name = event["source"].get("name")
        event_name = event["event"].get("name")
        args = event.get("args_named", {})

        if source_name == "keyboard" and event_name == "kbd_custom_key":
            logical_key = args.get("logical_key")
            phase = args.get("phase")
            key_name = f"KEY{logical_key}" if isinstance(logical_key, int) else None
            phase_name = key_phase_names.get(phase) if isinstance(phase, int) else None
            if key_name in key_summary and phase_name:
                key_summary[key_name][phase_name] += 1
            continue

        if source_name == "keyboard" and event_name == "kbd_ec11_detent":
            direction = args.get("direction")
            direction_name = ec11_direction_names.get(direction) if isinstance(direction, int) else None
            if direction_name:
                ec11_summary[direction_name] += 1
            if isinstance(args.get("detent_count"), int):
                ec11_summary["last_detent_count"] = args["detent_count"]
            continue

        if source_name == "voice_key" and event_name == "vkey_press":
            press_type = args.get("type")
            press_name = ec11_press_names.get(press_type) if isinstance(press_type, int) else None
            if press_name:
                ec11_summary[press_name] += 1

    return {
        "custom_keys": key_summary,
        "ec11": ec11_summary,
        "notes": "Counts are populated when keyboard.kbd_custom_key, keyboard.kbd_ec11_detent, or voice_key.vkey_press events exist in the input log.",
    }


def build_summary(events: list[dict[str, Any]], segments: list[dict[str, Any]]) -> dict[str, Any]:
    source_counts: Counter[str] = Counter()
    severity_counts: Counter[str] = Counter()
    event_counts: Counter[str] = Counter()
    warning_error_refs: list[dict[str, Any]] = []
    session_refs: list[dict[str, Any]] = []

    for event in events:
        source_name = event["source"]["name"] or "unknown"
        severity_name = event["severity"]["name"] or "UNKNOWN"
        event_name = event["event"]["name"] or "unknown"
        source_counts[source_name] += 1
        severity_counts[severity_name] += 1
        event_counts[f"{source_name}.{event_name}"] += 1

        if severity_name in ("WARN", "ERROR"):
            warning_error_refs.append(
                {
                    "event_index": event["index"],
                    "boot_segment_index": event["boot_segment_index"],
                    "t_ms": event["t_ms"],
                    "severity": severity_name,
                    "source": source_name,
                    "event": event_name,
                    "args_named": event["args_named"],
                    "args_decoded": event["args_decoded"],
                    "raw_args": event["args_raw"],
                }
            )

        for key, value in event["args_named"].items():
            if key in ("session_id", "session_count") or key.endswith("_session_id"):
                session_refs.append(
                    {
                        "event_index": event["index"],
                        "boot_segment_index": event["boot_segment_index"],
                        "t_ms": event["t_ms"],
                        "source": source_name,
                        "event": event_name,
                        "field": key,
                        "value": value,
                    }
                )

    return {
        "event_count": len(events),
        "boot_segment_count": len(segments),
        "counts_by_source": sorted_counter(source_counts),
        "counts_by_severity": sorted_counter(severity_counts),
        "counts_by_event": sorted_counter(event_counts),
        "input_debug_summary": build_input_debug_summary(events),
        "recent_warning_error_refs": warning_error_refs[-20:],
        "session_refs": session_refs,
    }


def build_definitions(events: list[dict[str, Any]], schema: dict[str, Any]) -> dict[str, Any]:
    seen_sources = sorted({event["source"]["name"] for event in events if event["source"]["name"]})
    seen_events = sorted(
        {
            (event["source"]["name"], event["event"]["name"])
            for event in events
            if event["source"]["name"] and event["event"]["name"]
        }
    )
    source_defs = {
        name: {
            "id": schema["sources_by_name"][name]["id"],
            "macro": schema["sources_by_name"][name]["macro"],
        }
        for name in seen_sources
        if name in schema["sources_by_name"]
    }
    event_defs: dict[str, Any] = {}
    for source_name, event_name in seen_events:
        source_info = schema["sources_by_name"].get(source_name)
        if not source_info:
            continue
        for (source_macro, _value), event_def in schema["events_by_source_value"].items():
            if source_macro == source_info["macro"] and event_def["name"] == event_name:
                event_defs[f"{source_name}.{event_name}"] = {
                    "value": event_def["value"],
                    "macro": event_def["macro"],
                    "comment": event_def["comment"],
                    "args": event_def["args"],
                }
                break
    return {
        "sources_seen": source_defs,
        "events_seen": event_defs,
        "constant_groups": schema["constants_by_group"],
    }


def decode_file(input_path: pathlib.Path, header_path: pathlib.Path) -> dict[str, Any]:
    schema = load_event_schema(header_path)
    events: list[dict[str, Any]] = []
    parse_errors: list[dict[str, Any]] = []
    skipped_lines = 0

    lines = input_path.read_text(encoding="utf-8", errors="replace").splitlines()
    for line_number, line in enumerate(lines, 1):
        raw = extract_json_object(line)
        if raw is None:
            skipped_lines += 1
            if line.strip():
                parse_errors.append(
                    {
                        "line_number": line_number,
                        "reason": "no JSON object could be parsed",
                        "line_preview": line.strip()[:160],
                    }
                )
            continue
        events.append(decode_event(raw, line_number, len(events), schema))

    segments, reset_refs = assign_boot_segments(events)

    return {
        "schema_version": BUNDLE_SCHEMA_VERSION,
        "schema_id": BUNDLE_SCHEMA_ID,
        "source_metadata": {
            "input_path": str(input_path),
            "input_sha256": sha256_file(input_path),
            "input_bytes": input_path.stat().st_size,
            "input_line_count": len(lines),
            "event_schema_header_path": str(header_path),
            "event_schema_header_sha256": schema["header_sha256"],
            "event_schema_macro_count": schema["macro_count"],
            "decoder": "tools/decode_diag_log.py",
        },
        "parse": {
            "json_event_count": len(events),
            "skipped_line_count": skipped_lines,
            "parse_errors": parse_errors[:50],
            "parse_error_count": len(parse_errors),
        },
        "timebase": {
            "field": "t",
            "unit": "milliseconds since boot segment",
            "boot_segment_rule": "boot_segment_index increments when t_ms decreases between ordered events",
            "t_decrease_events": reset_refs,
            "segments": segments,
        },
        "summary": build_summary(events, segments),
        "definitions": build_definitions(events, schema),
        "events": events,
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Decode firmware ~DIAGLOG JSONL into a stable JSON bundle for AI agents."
    )
    parser.add_argument("--input", required=True, type=pathlib.Path, help="Saved ~DIAGLOG JSONL file.")
    parser.add_argument("--output", type=pathlib.Path, help="Output JSON bundle path. Defaults to stdout.")
    parser.add_argument(
        "--header",
        type=pathlib.Path,
        default=pathlib.Path("components/diag_log/include/diag_log_events.h"),
        help="diag_log_events.h path used to derive event names and argument names.",
    )
    parser.add_argument("--compact", action="store_true", help="Emit compact JSON instead of indented JSON.")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    input_path = args.input.resolve()
    header_path = args.header.resolve()
    if not input_path.exists():
        raise FileNotFoundError(f"input JSONL not found: {input_path}")
    if not header_path.exists():
        raise FileNotFoundError(f"diag_log header not found: {header_path}")

    bundle = decode_file(input_path, header_path)
    json_text = json.dumps(
        bundle,
        indent=None if args.compact else 2,
        sort_keys=False,
        ensure_ascii=False,
    )
    if args.output:
        output_path = args.output.resolve()
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json_text + "\n", encoding="utf-8")
    else:
        sys.stdout.write(json_text + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
