#!/usr/bin/env python3
"""Validate low-power status LED brightness from a serial transcript."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
from typing import Dict, Iterable, Tuple


Rgb = Tuple[int, int, int]


def latest_line(lines: Iterable[str], needle: str) -> str:
    found = ""
    for line in lines:
        if needle in line:
            found = line.strip()
    return found


def parse_fields(line: str) -> Dict[str, str]:
    return {match.group(1): match.group(2) for match in re.finditer(r"([A-Za-z0-9_]+)=([^ \r\n]+)", line)}


def parse_status_rgb(line: str) -> Dict[str, Rgb]:
    values: Dict[str, Rgb] = {}
    for name, red, green, blue in re.findall(r"\b(PWR|BLE|REC|AI|OK|WARN):(\d+),(\d+),(\d+)", line):
        values[name] = (int(red), int(green), int(blue))
    return values


def parse_active_flags(summary_line: str) -> Dict[str, int]:
    match = re.search(r"active_flags=([^ \r\n]+)", summary_line)
    if not match:
        return {}
    flags: Dict[str, int] = {}
    for part in match.group(1).split(","):
        if ":" not in part:
            continue
        name, value = part.split(":", 1)
        try:
            flags[name] = int(value)
        except ValueError:
            pass
    return flags


def linear_percent_to_255(percent: int) -> int:
    percent = max(0, min(100, percent))
    return min(255, ((percent * 255) + 50) // 100)


def channel_sum(rgb: Rgb) -> int:
    return rgb[0] + rgb[1] + rgb[2]


def fail(message: str, details: Dict[str, object]) -> int:
    print(f"FAIL: {message}")
    print(json.dumps(details, ensure_ascii=False, indent=2))
    return 1


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("transcript", type=pathlib.Path, help="Serial transcript containing ~LED:STATUS detail lines")
    parser.add_argument("--tolerance", type=int, default=2, help="Allowed RGB channel-sum tolerance")
    parser.add_argument("--require-low-power", action="store_true", default=True)
    args = parser.parse_args(argv)

    text = args.transcript.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    brightness_line = latest_line(lines, "~LED:STATUS detail=brightness")
    state_line = latest_line(lines, "~LED:STATUS detail=state")
    power_line = latest_line(lines, "~LED:STATUS detail=power")
    rgb_line = latest_line(lines, "~LED:STATUS detail=rgb status_rgb=")
    summary_line = latest_line(lines, "~LED:STATUS profile=")

    details: Dict[str, object] = {
        "transcript": str(args.transcript),
        "brightness_line": brightness_line,
        "state_line": state_line,
        "power_line": power_line,
        "rgb_line": rgb_line,
        "summary_line": summary_line,
    }
    missing = [
        name
        for name, line in (
            ("brightness", brightness_line),
            ("state", state_line),
            ("power", power_line),
            ("rgb", rgb_line),
            ("summary", summary_line),
        )
        if not line
    ]
    if missing:
        details["missing"] = missing
        return fail("missing required ~LED:STATUS detail lines", details)

    brightness = parse_fields(brightness_line)
    state = parse_fields(state_line)
    power = parse_fields(power_line)
    rgb = parse_status_rgb(rgb_line)
    flags = parse_active_flags(summary_line)
    details.update(
        {
            "brightness": brightness,
            "state": state,
            "power": power,
            "rgb": rgb,
            "active_flags": flags,
        }
    )

    if args.require_low_power and state.get("low_power_disabled") != "1":
        return fail("device is not in low-power final-latch state", details)
    if brightness.get("budget_limited_by_current") not in ("0", None):
        return fail("current budget limited this frame; brightness contract cannot be judged", details)

    try:
        status_percent = int(brightness["status_zone_brightness_percent"])
    except (KeyError, ValueError):
        return fail("missing status_zone_brightness_percent", details)
    expected_peak = linear_percent_to_255(status_percent)
    details["expected_type_peak"] = expected_peak

    pwr_rgb = rgb.get("PWR")
    pwr_active = bool(flags.get("PWR", 0)) or (pwr_rgb is not None and channel_sum(pwr_rgb) > 0)
    if not pwr_active or pwr_rgb is None:
        return fail("low-power status frame did not expose a visible PWR cue", details)

    pwr_sum = channel_sum(pwr_rgb)
    details["pwr_channel_sum"] = pwr_sum
    if abs(pwr_sum - expected_peak) > args.tolerance:
        return fail(
            "low-power PWR brightness is not Type-capped; hidden low-power dimming likely returned",
            details,
        )

    if power.get("external_power") == "1":
        spread = max(pwr_rgb) - min(pwr_rgb)
        details["pwr_white_channel_spread"] = spread
        if spread > args.tolerance:
            return fail("external-power low-power PWR white is not energy-balanced", details)

    ble_rgb = rgb.get("BLE")
    ble_state = state.get("ble")
    if ble_state != "type_ready":
        return fail("low-power capture did not retain a Type-ready BLE connection", details)
    if ble_rgb is None or channel_sum(ble_rgb) == 0 or not flags.get("BLE", 0):
        return fail("low-power TYPE_READY BLE indicator is not visibly active", details)
    ble_sum = channel_sum(ble_rgb)
    details["ble_channel_sum"] = ble_sum
    if (
        abs(ble_sum - expected_peak) > args.tolerance
        or ble_rgb[0] > args.tolerance
        or ble_rgb[1] > args.tolerance
        or ble_rgb[2] < expected_peak - args.tolerance
    ):
        return fail("low-power TYPE_READY BLE indicator is not Type-capped dim blue", details)

    print(
        "PASS: low-power Type-ready status LEDs retain Type-capped PWR and dim-blue BLE "
        f"(status={status_percent}%, expected_peak={expected_peak}, PWR={pwr_rgb}, BLE={ble_rgb})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
