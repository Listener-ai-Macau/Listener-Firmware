#!/usr/bin/env python3
"""Validate flash diag evidence for unplugged/no-serial LED behavior."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from typing import Any


REQUIRED_HIGHLIGHTS = (
    ("status_led", "power_input"),
    ("status_led", "visual_state"),
    ("status_led", "output_state"),
    ("power", "external_power"),
    ("power", "sleep_wake"),
)

PWR_CLASS_ALIASES = {
    "yellow": "amber",
}


def load_bundle(path: pathlib.Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError("decoded bundle root must be a JSON object")
    return data


def highlight_list(bundle: dict[str, Any], section: str, name: str) -> list[dict[str, Any]]:
    value = (
        bundle.get("summary", {})
        .get("param_highlights", {})
        .get(section, {})
        .get(name, [])
    )
    if not isinstance(value, list):
        return []
    return [entry for entry in value if isinstance(entry, dict)]


def pwr_class_label(entry: dict[str, Any]) -> str | None:
    flags = entry.get("led_visual_state_flags")
    if not isinstance(flags, dict):
        return None
    label = flags.get("pwr_class_label")
    return label if isinstance(label, str) else None


def pwr_rgb_hex(entry: dict[str, Any]) -> str | None:
    rgb = entry.get("pwr_rgb")
    if not isinstance(rgb, dict):
        return None
    value = rgb.get("hex")
    return value if isinstance(value, str) else None


def visual_entry_is_off(entry: dict[str, Any]) -> bool:
    active = entry.get("led_active_flags")
    flags = entry.get("led_visual_state_flags")
    label = pwr_class_label(entry)
    rgb_hex = pwr_rgb_hex(entry)
    pwr_active = isinstance(active, dict) and bool(active.get("pwr"))
    output_disabled = isinstance(flags, dict) and bool(flags.get("output_disabled"))
    return output_disabled or label == "off" or rgb_hex == "#000000" or not pwr_active


def visual_entry_is_on(entry: dict[str, Any]) -> bool:
    active = entry.get("led_active_flags")
    flags = entry.get("led_visual_state_flags")
    label = pwr_class_label(entry)
    rgb_hex = pwr_rgb_hex(entry)
    output_disabled = isinstance(flags, dict) and bool(flags.get("output_disabled"))
    pwr_active = isinstance(active, dict) and bool(active.get("pwr"))
    visible_rgb = isinstance(rgb_hex, str) and rgb_hex != "#000000"
    visible_class = isinstance(label, str) and label not in ("", "off")
    return not output_disabled and (pwr_active or visible_rgb or visible_class)


def has_output_disable(entries: list[dict[str, Any]]) -> bool:
    for entry in sorted(entries, key=lambda item: int(item.get("event_index", -1))):
        state = entry.get("led_output_state")
        if not isinstance(state, dict):
            continue
        is_disabled = bool(state.get("output_disabled")) or bool(state.get("low_power_disabled"))
        if is_disabled:
            return True
    return False


def normalize_expected_classes(values: list[str]) -> set[str]:
    normalized: set[str] = set()
    for value in values:
        key = value.strip().lower()
        if key:
            normalized.add(PWR_CLASS_ALIASES.get(key, key))
    return normalized


def validate_bundle(
    bundle: dict[str, Any],
    expected_classes: set[str],
    forbidden_classes: set[str],
    require_display_latch: bool,
) -> list[str]:
    errors: list[str] = []
    highlights = bundle.get("summary", {}).get("param_highlights", {})
    if not isinstance(highlights, dict):
        return ["summary.param_highlights is missing"]

    counts: dict[tuple[str, str], int] = {}
    for section, name in REQUIRED_HIGHLIGHTS:
        entries = highlight_list(bundle, section, name)
        counts[(section, name)] = len(entries)
        if not entries:
            errors.append(f"missing summary.param_highlights.{section}.{name} timeline")
        elif not all(entry.get("event_ref") for entry in entries[:10]):
            errors.append(f"{section}.{name} entries must keep event_ref back to raw events")

    visual_entries = highlight_list(bundle, "status_led", "visual_state")
    output_entries = highlight_list(bundle, "status_led", "output_state")
    power_entries = highlight_list(bundle, "status_led", "power_input")
    external_power_entries = highlight_list(bundle, "power", "external_power")
    sleep_wake_entries = highlight_list(bundle, "power", "sleep_wake")

    classes = {
        label
        for label in (pwr_class_label(entry) for entry in visual_entries)
        if isinstance(label, str)
    }
    forbidden_observed = classes.intersection(forbidden_classes)
    if forbidden_observed:
        errors.append(
            "forbidden PWR classes observed: " + ", ".join(sorted(forbidden_observed))
        )
    if expected_classes and not expected_classes.issubset(classes):
        errors.append(
            "missing expected PWR classes: "
            + ", ".join(sorted(expected_classes - classes))
            + f"; observed={sorted(classes)}"
        )
    elif not expected_classes and len(classes) < 2:
        errors.append(f"PWR visual class did not change; observed={sorted(classes)}")

    sorted_visual = sorted(visual_entries, key=lambda item: int(item.get("event_index", -1)))
    off_then_on = False
    saw_off = False
    for entry in sorted_visual:
        if visual_entry_is_off(entry):
            saw_off = True
        if saw_off and visual_entry_is_on(entry):
            off_then_on = True
            break
    if not off_then_on:
        errors.append("status_led.visual_state does not show LED/PWR off followed by visible-on recovery")

    if output_entries and not has_output_disable(output_entries):
        errors.append("status_led.output_state does not show output disable before sleep/shutdown")

    if power_entries and not any(
        isinstance(entry.get("led_power_flags"), dict)
        and not bool(entry["led_power_flags"].get("external_power"))
        for entry in power_entries
    ):
        errors.append("status_led.power_input never records battery-only external_power=false")

    if external_power_entries and not any(
        isinstance(entry.get("power_source_flags"), dict)
        and not bool(entry["power_source_flags"].get("external_power_present"))
        for entry in external_power_entries
    ):
        errors.append("power.external_power never records external_power_present=false")

    if external_power_entries and not any(
        isinstance(entry.get("power_source_flags"), dict)
        and isinstance(entry.get("power_source_levels"), dict)
        for entry in external_power_entries
    ):
        errors.append("power.external_power entries are not decoded into source flags and GPIO levels")

    if sleep_wake_entries and not any(
        isinstance(entry.get("sleep_blocked"), dict)
        and isinstance(entry["sleep_blocked"].get("blocker_names"), list)
        for entry in sleep_wake_entries
    ):
        errors.append("power.sleep_wake entries are not decoded into blocker names")

    if require_display_latch and power_entries:
        display_entries = [
            entry for entry in power_entries
            if isinstance(entry.get("led_power_flags"), dict)
            and bool(entry["led_power_flags"].get("display_valid"))
            and isinstance(entry.get("battery_display_level"), int)
        ]
        if not display_entries:
            errors.append("status_led.power_input never records display-latched battery level")
        elif not any(
            isinstance(entry.get("fields"), dict)
            and isinstance(entry["fields"].get("battery_mv"), int)
            and isinstance(entry["fields"].get("battery_level"), int)
            and entry["fields"].get("battery_level") != 0xFF
            for entry in display_entries
        ):
            errors.append("status_led.power_input display-latch entries do not preserve raw battery_mv/battery_level")

    return errors


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Validate a decoded diag_log_ai_bundle.json from an unplugged/no-serial "
            "reproduction. Use after reconnecting and dumping flash diagnostics."
        )
    )
    parser.add_argument("--bundle", required=True, type=pathlib.Path, help="Decoded diag_log_ai_bundle.json")
    parser.add_argument(
        "--expect-pwr-class",
        action="append",
        default=[],
        help="Required PWR class label, for example amber and green for yellow/green cycling.",
    )
    parser.add_argument(
        "--forbid-pwr-class",
        action="append",
        default=[],
        help="PWR class label that must not appear, for example green after battery display latch.",
    )
    parser.add_argument(
        "--require-display-latch",
        action="store_true",
        help="Require status_led.power_input to include decoded display-latched battery level.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    bundle_path = args.bundle.resolve()
    if not bundle_path.exists():
        raise FileNotFoundError(f"decoded bundle not found: {bundle_path}")

    bundle = load_bundle(bundle_path)
    expected_classes = normalize_expected_classes(args.expect_pwr_class)
    forbidden_classes = normalize_expected_classes(args.forbid_pwr_class)
    errors = validate_bundle(bundle, expected_classes, forbidden_classes, args.require_display_latch)
    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1

    visual_entries = highlight_list(bundle, "status_led", "visual_state")
    classes = sorted(
        label
        for label in {pwr_class_label(entry) for entry in visual_entries}
        if isinstance(label, str)
    )
    print(
        "PASS: unplugged flash diag bundle captures status_led power_input, "
        "visual_state, output_state, power external_power, and sleep_wake timelines; "
        f"pwr_classes={classes}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
