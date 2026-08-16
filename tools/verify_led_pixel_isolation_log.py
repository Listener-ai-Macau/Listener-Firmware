from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


COMMAND_RE = re.compile(
    r"^>\s*~LED:TEST:PIXEL\s+"
    r"(?P<strip>status|ec11|knob|ring|key|edge)\s+"
    r"(?P<led>LED\d+|\d+)\s+"
    r"(?P<color>white|off|black)\s+"
    r"(?P<percent>\d+)\s*$",
    re.IGNORECASE,
)
CALIBRATION_RE = re.compile(
    r"LED pixel calibration strip=(?P<strip>status|ec11|key|edge)\s+"
    r"led=LED(?P<led>\d+)\s+"
    r"index=(?P<index>\d+)\s+"
    r"color=(?P<color>white|off|black)\s+"
    r"percent=(?P<percent>\d+)",
    re.IGNORECASE,
)
STATUS_RE = re.compile(
    r"(?P<name>PWR|BLE|REC|AI|OK|WARN):"
    r"(?P<r>\d+),(?P<g>\d+),(?P<b>\d+)"
)
PX_RE = re.compile(r"px(?P<index>\d+):(?P<r>\d+),(?P<g>\d+),(?P<b>\d+)")

STATUS_NAMES = ("PWR", "BLE", "REC", "AI", "OK", "WARN")
STRIP_COUNTS = {"status": 6, "ec11": 12, "key": 4, "edge": 6}
FIRST_LEDS = {"status": 1, "ec11": 7, "key": 11, "edge": 17}


def canonical_strip(strip: str) -> str:
    lower = strip.lower()
    if lower in ("knob", "ring"):
        return "ec11"
    return lower


def parse_rgb(rgb: tuple[str, str, str]) -> tuple[int, int, int]:
    return int(rgb[0]), int(rgb[1]), int(rgb[2])


def parse_status_pixels(line: str) -> dict[str, tuple[int, int, int]]:
    if "~LED:STATUS detail=rgb" not in line or "status_rgb=" not in line:
        return {}
    return {
        match.group("name"): parse_rgb((match.group("r"), match.group("g"), match.group("b")))
        for match in STATUS_RE.finditer(line)
    }


def parse_px_pixels(line: str, strip: str) -> dict[int, tuple[int, int, int]]:
    if f"~LED:STATUS detail=rgb_{strip}" not in line or f"{strip}_rgb=" not in line:
        return {}
    return {
        int(match.group("index")): parse_rgb((match.group("r"), match.group("g"), match.group("b")))
        for match in PX_RE.finditer(line)
    }


def led_to_index(strip: str, led: str) -> int | None:
    token = led.upper()
    if token.startswith("LED"):
        value = token[3:]
        if not value.isdecimal():
            return None
        physical = int(value)
        index = physical - FIRST_LEDS[strip] + 1
    else:
        if not token.isdecimal():
            return None
        index = int(token)
    if 1 <= index <= STRIP_COUNTS[strip]:
        return index
    return None


def is_white(rgb: tuple[int, int, int]) -> bool:
    r, g, b = rgb
    return r > 0 and r == g == b


def is_dark(rgb: tuple[int, int, int]) -> bool:
    return rgb == (0, 0, 0)


def validate_target_frame(
    strip: str,
    target: int,
    frames: dict[str, dict],
    failures: list[str],
    context: str,
) -> bool:
    if strip == "status":
        status = frames.get("status", {})
        if set(status) != set(STATUS_NAMES):
            failures.append(f"{context}: missing complete status_rgb readback")
            return False
        target_name = STATUS_NAMES[target - 1]
        for name, rgb in status.items():
            if name == target_name:
                if not is_white(rgb):
                    failures.append(f"{context}: target status {target_name} is not isolated white: {rgb}")
                    return False
            elif not is_dark(rgb):
                failures.append(f"{context}: status {name} carried light while {target_name} was targeted: {rgb}")
                return False
    else:
        pixels = frames.get(strip, {})
        expected = set(range(1, STRIP_COUNTS[strip] + 1))
        if set(pixels) != expected:
            failures.append(f"{context}: missing complete {strip}_rgb readback")
            return False
        for index, rgb in pixels.items():
            if index == target:
                if not is_white(rgb):
                    failures.append(f"{context}: target {strip} px{target} is not isolated white: {rgb}")
                    return False
            elif not is_dark(rgb):
                failures.append(f"{context}: {strip} px{index} carried light while px{target} was targeted: {rgb}")
                return False

    for other, data in frames.items():
        if other == strip:
            continue
        for name, rgb in data.items():
            if not is_dark(rgb):
                failures.append(
                    f"{context}: cross-strip leak into {other} {name} while {strip} px{target} was targeted: {rgb}"
                )
                return False
    return True


def collect_frames(lines: list[str], start: int, end: int) -> dict[str, dict]:
    frames: dict[str, dict] = {}
    for line in lines[start:end]:
        status = parse_status_pixels(line)
        if status:
            frames["status"] = status
        for strip in ("ec11", "key", "edge"):
            pixels = parse_px_pixels(line, strip)
            if pixels:
                frames[strip] = pixels
    return frames


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify all-zone LED TEST:PIXEL isolation readback.")
    parser.add_argument("log", type=Path)
    args = parser.parse_args()

    text = args.log.read_text(encoding="utf-8", errors="replace")
    failures: list[str] = []

    for token in (
        "strip_transport_actual=main:rmt,ec11_alias:rmt,key_alias:rmt,edge:spi2",
        "spi_dma_actual=status:0,ec11:0,key:0,edge:1",
        "spi_dma_fallback=status:0,ec11:0,key:0,edge:0",
        "rmt_tx_dma_actual=status:1,ec11:1,key:1,edge:0",
        "rmt_tx_dma_all_strips=0",
        "dma_all_physical_routes=1",
        "low_power_spi_latch=dma_prelatch_then_non_dma_final_gpio_low",
        "rmt_idle_drive=active_dma_low_power_all_zone_non_dma_final_frame_then_release_gpio_low",
    ):
        if token not in text:
            failures.append(f"missing transport/latch contract token: {token}")

    if "LED TEST:PIXEL invalid" in text or "WRITE_ERROR" in text or "READ_ERROR" in text:
        failures.append("serial transcript contains TEST:PIXEL/serial error")

    lines = text.splitlines()
    command_targets: dict[str, set[int]] = {strip: set() for strip in STRIP_COUNTS}
    for line_no, line in enumerate(lines):
        match = COMMAND_RE.match(line.strip())
        if not match:
            continue
        strip = canonical_strip(match.group("strip"))
        color = match.group("color").lower()
        percent = int(match.group("percent"))
        target = led_to_index(strip, match.group("led"))
        if target is None:
            failures.append(f"invalid TEST:PIXEL target at line {line_no + 1}: {line}")
            continue
        if color != "white" or percent != 100:
            failures.append(f"pixel isolation sweep must use white 100, got line {line_no + 1}: {line}")
            continue
        command_targets[strip].add(target)

    calibration_targets: list[tuple[int, str, int, str]] = []
    for line_no, line in enumerate(lines):
        match = CALIBRATION_RE.search(line)
        if not match:
            continue
        strip = canonical_strip(match.group("strip"))
        color = match.group("color").lower()
        percent = int(match.group("percent"))
        target = int(match.group("index")) + 1
        if color != "white" or percent != 100:
            failures.append(f"pixel isolation calibration must use white 100, got line {line_no + 1}: {line}")
            continue
        if not 1 <= target <= STRIP_COUNTS[strip]:
            failures.append(f"invalid calibration target at line {line_no + 1}: {line}")
            continue
        calibration_targets.append((line_no, strip, target, line.strip()))

    expected_counts = {"status": 6, "ec11": 12, "key": 4, "edge": 6}
    for strip, count in expected_counts.items():
        missing_commands = sorted(set(range(1, count + 1)) - command_targets[strip])
        if missing_commands:
            failures.append(f"missing TEST:PIXEL commands for {strip} pixels: {missing_commands}")

    observed: dict[str, set[int]] = {strip: set() for strip in expected_counts}
    off_markers = [
        i
        for i, line in enumerate(lines)
        if "LED output manually forced off until the next status/key event" in line
    ]
    for index, (line_no, strip, target, context) in enumerate(calibration_targets):
        next_boundaries = []
        if index + 1 < len(calibration_targets):
            next_boundaries.append(calibration_targets[index + 1][0])
        next_boundaries.extend(marker for marker in off_markers if marker > line_no)
        next_line = min(next_boundaries) if next_boundaries else len(lines)
        frames = collect_frames(lines, line_no, next_line)
        if validate_target_frame(strip, target, frames, failures, context):
            observed[strip].add(target)

    for strip, count in expected_counts.items():
        missing = sorted(set(range(1, count + 1)) - observed[strip])
        if missing:
            failures.append(f"missing isolated {strip} pixel readbacks: {missing}")

    off_index = next(
        (i for i, line in enumerate(lines) if "LED output manually forced off until the next status/key event" in line),
        None,
    )
    if off_index is None:
        failures.append("missing final ~LED:OFF command")
    else:
        frames = collect_frames(lines, off_index, len(lines))
        missing_off_frames = sorted(set(STRIP_COUNTS) - set(frames))
        if missing_off_frames:
            failures.append(f"final OFF readback missing strip rgb lines: {missing_off_frames}")
        for strip, data in frames.items():
            expected_keys = set(STATUS_NAMES) if strip == "status" else set(range(1, STRIP_COUNTS[strip] + 1))
            if set(data) != expected_keys:
                failures.append(f"final OFF readback for {strip} is incomplete")
                continue
            for name, rgb in data.items():
                if not is_dark(rgb):
                    failures.append(f"final OFF readback left {strip} {name} lit: {rgb}")
                    break

    if failures:
        print("FAIL: LED pixel isolation log verification failed")
        for failure in failures:
            print(f" - {failure}")
        return 1

    print(
        "PASS: LED pixel isolation log proves status/key/EC11/edge TEST:PIXEL readback "
        "lights exactly one target pixel, keeps other strips dark, preserves the "
        "V2.2 main-chain RMT DMA + edge SPI2 DMA transport contract, and "
        "returns dark after OFF."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
