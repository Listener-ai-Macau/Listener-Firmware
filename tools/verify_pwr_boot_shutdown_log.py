from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


RGB = tuple[int, int, int]
POWER_CONFIRM_AMBER: RGB = (255, 96, 0)
SHUTDOWN_CONFIRM_EC11_PERCENT = 24


def fail(message: str) -> int:
    print(f"FAIL: {message}")
    return 1


def require(text: str, token: str, message: str) -> str | None:
    if token not in text:
        return message
    return None


def section(text: str, start_marker: str, next_markers: list[str]) -> str:
    start = text.find(start_marker)
    if start < 0:
        raise ValueError(f"missing section marker: {start_marker}")
    ends = [text.find(marker, start + len(start_marker)) for marker in next_markers]
    ends = [pos for pos in ends if pos >= 0]
    end = min(ends) if ends else len(text)
    return text[start:end]


def parse_status_pwrs(text: str) -> list[RGB]:
    values: list[RGB] = []
    for match in re.finditer(r"status_rgb=PWR:(\d+),(\d+),(\d+)", text):
        values.append(tuple(int(match.group(index)) for index in range(1, 4)))
    return values


def require_no_white_pwr(text: str, label: str) -> str | None:
    for rgb in parse_status_pwrs(text):
        r, g, b = rgb
        if r == g == b and r > 0:
            return f"{label}: PWR regressed to white/gray while validating amber cue: {rgb}"
    return None


def require_amber_pwr(text: str, expected: RGB, label: str) -> str | None:
    pwrs = parse_status_pwrs(text)
    if expected not in pwrs:
        return f"{label}: missing expected Type-capped amber PWR {expected}; saw {pwrs}"
    return None


def all_pixels_line(strip: str, rgb: RGB) -> str:
    body = ";".join(f"px{index}:{rgb[0]},{rgb[1]},{rgb[2]}" for index in range(1, {
        "ec11": 12,
        "key": 4,
        "edge": 6,
    }[strip] + 1))
    return f"{strip}_rgb={body}"


def parse_zone_brightness_percent(text: str, zone: str) -> int | None:
    values = [
        int(match.group(1))
        for match in re.finditer(rf"\b{re.escape(zone)}_zone_brightness_percent=(\d+)\b", text)
    ]
    return values[-1] if values else None


def scale_rgb_by_percent_and_zone(rgb: RGB, percent: int, zone_percent: int) -> RGB:
    return tuple((channel * percent * zone_percent + 5000) // 10000 for channel in rgb)  # type: ignore[return-value]


def verify_boot_diag(decoded_path: Path, failures: list[str]) -> None:
    try:
        data = json.loads(decoded_path.read_text(encoding="utf-8", errors="replace"))
    except json.JSONDecodeError as exc:
        failures.append(f"boot diag decoded JSON is invalid: {exc}")
        return

    boot_visuals: list[tuple[int, int, RGB, str, bool]] = []
    visual_entries = (
        data.get("summary", {})
        .get("param_highlights", {})
        .get("status_led", {})
        .get("visual_state", [])
    )
    if not visual_entries:
        visual_entries = [
            event
            for event in data.get("events", [])
            if event.get("event", {}).get("name") == "led_visual_state"
        ]
    for event in visual_entries:
        flags = event.get("led_visual_state_flags", {})
        if not flags.get("boot_feedback"):
            continue
        rgb_obj = event.get("pwr_rgb", {})
        try:
            rgb = (int(rgb_obj["r"]), int(rgb_obj["g"]), int(rgb_obj["b"]))
        except (KeyError, TypeError, ValueError):
            failures.append(f"boot diag event missing decoded PWR RGB: events[{event.get('event_index', event.get('index', '?'))}]")
            continue
        boot_visuals.append((
            int(event.get("event_index", event.get("index", -1))),
            int(event.get("t_ms", -1)),
            rgb,
            str(flags.get("pwr_class_label", "")),
            bool(event.get("led_active_flags", {}).get("pwr")),
        ))

    if not boot_visuals:
        failures.append("boot diag must include boot_feedback=true PWR visual events")
        return
    if len(boot_visuals) < 3:
        failures.append(f"boot diag must include repeated boot PWR samples; saw {len(boot_visuals)}")

    first_t_ms = min(t_ms for _index, t_ms, _rgb, _label, _active in boot_visuals)
    if first_t_ms < 0 or first_t_ms > 2500:
        failures.append(f"boot diag first amber PWR cue is too late for first-frame validation: {first_t_ms} ms")

    for index, t_ms, rgb, label, active in boot_visuals:
        if not active:
            failures.append(f"boot diag events[{index}] at {t_ms} ms did not keep PWR active")
        if rgb != (107, 40, 0) or label != "amber":
            failures.append(f"boot diag events[{index}] at {t_ms} ms expected capped amber PWR (107, 40, 0), saw {rgb} class={label}")
        if rgb[0] == rgb[1] == rgb[2] and rgb[0] > 0:
            failures.append(f"boot diag events[{index}] at {t_ms} ms regressed to white/gray PWR: {rgb}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify PWR boot/shutdown amber and Type brightness cap evidence from a serial transcript."
    )
    parser.add_argument("transcript", type=Path)
    parser.add_argument("--boot-diag-decoded", required=True, type=Path)
    args = parser.parse_args()

    text = args.transcript.read_text(encoding="utf-8", errors="replace")
    failures: list[str] = []
    verify_boot_diag(args.boot_diag_decoded, failures)

    for token, message in (
        (
            "status_zone_brightness_percent=80 key_zone_brightness_percent=80",
            "runtime brightness evidence must show restored Type status/key caps at 80/80",
        ),
        ("zone_brightness_is_hard_cap=1", "runtime brightness evidence must show hard Type zone caps"),
        ("zone_brightness_effect_peak_cap=1", "runtime brightness evidence must show effect peaks are capped"),
        (
            "zone_brightness_preserves_effect_percent=1",
            "runtime brightness evidence must show fade/effect percentages are preserved",
        ),
    ):
        maybe = require(text, token, message)
        if maybe:
            failures.append(maybe)

    try:
        final_section = section(
            text,
            "> ~LED:PREVIEW shutdown_final",
            ["> ~LED:PREVIEW shutdown_confirm", "> ~LED:PREVIEW clear"],
        )
        confirm_section = section(
            text,
            "> ~LED:PREVIEW shutdown_confirm",
            ["> ~LED:PREVIEW clear"],
        )
    except ValueError as exc:
        failures.append(str(exc))
    else:
        for token, message in (
            (
                "shutdown_confirm_active=1 shutdown_confirm_final=1",
                "shutdown_final must report an active final confirmation window",
            ),
            (
                "strip ec11 SPI DMA pre-latch before non-DMA RMT latch",
                "shutdown_final must keep SPI DMA pre-latch before one-shot RMT latch for EC11",
            ),
            (
                "strip key SPI DMA pre-latch before non-DMA RMT latch",
                "shutdown_final must keep SPI DMA pre-latch before one-shot RMT latch for KEY",
            ),
            (
                "strip ec11 non-DMA one-shot via RMT for SPI DMA latch",
                "shutdown_final must keep one-shot RMT latch for EC11",
            ),
            (
                "strip key non-DMA one-shot via RMT for SPI DMA latch",
                "shutdown_final must keep one-shot RMT latch for KEY",
            ),
        ):
            maybe = require(final_section, token, message)
            if maybe:
                failures.append(maybe)

        for check in (
            require_no_white_pwr(final_section, "shutdown_final"),
            require_amber_pwr(final_section, (107, 40, 0), "shutdown_final"),
            require(final_section, all_pixels_line("ec11", (0, 0, 0)), "shutdown_final must keep EC11 dark"),
            require(final_section, all_pixels_line("key", (0, 0, 0)), "shutdown_final must keep KEY dark"),
            require(final_section, all_pixels_line("edge", (0, 0, 0)), "shutdown_final must keep EDGE dark"),
        ):
            if check:
                failures.append(check)

        ec11_zone_percent = parse_zone_brightness_percent(confirm_section, "ec11")
        if ec11_zone_percent is None:
            failures.append("shutdown_confirm must report the EC11 Type brightness cap used for the amber countdown ring")
            expected_ec11_ring = None
        else:
            expected_ec11_ring = scale_rgb_by_percent_and_zone(
                POWER_CONFIRM_AMBER,
                SHUTDOWN_CONFIRM_EC11_PERCENT,
                ec11_zone_percent,
            )

        confirm_checks = [
            (
                "shutdown_confirm_active=1 shutdown_confirm_final=0",
                "shutdown_confirm must report an active non-final confirmation window",
            ),
            (all_pixels_line("key", (0, 0, 0)), "shutdown_confirm must keep KEY dark"),
            (all_pixels_line("edge", (0, 0, 0)), "shutdown_confirm must keep EDGE dark"),
        ]
        if expected_ec11_ring is not None:
            confirm_checks.append((
                all_pixels_line("ec11", expected_ec11_ring),
                f"shutdown_confirm must keep the accepted amber EC11 countdown ring at the logged Type cap ({ec11_zone_percent}%)",
            ))
        for token, message in confirm_checks:
            maybe = require(confirm_section, token, message)
            if maybe:
                failures.append(maybe)

        for check in (
            require_no_white_pwr(confirm_section, "shutdown_confirm"),
            require_amber_pwr(confirm_section, (68, 26, 0), "shutdown_confirm"),
        ):
            if check:
                failures.append(check)

    if failures:
        for failure in failures:
            print(f" - {failure}")
        return fail("PWR boot/shutdown LED log verification failed")

    print(
        "PASS: PWR boot/shutdown logs show Type-capped amber cues, no boot/shutdown "
        "white PWR regression, and the SPI-DMA-to-RMT latch path for final dark strips."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
