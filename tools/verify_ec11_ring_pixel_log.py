from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


COMMAND_RE = re.compile(
    r"^>\s*~LED:TEST:PIXEL\s+(?:ec11|knob|ring)\s+(?P<index>\d+)\s+white\s+(?P<percent>\d+)\s*$",
    re.IGNORECASE,
)
RGB_RE = re.compile(r"px(?P<index>\d+):(?P<r>\d+),(?P<g>\d+),(?P<b>\d+)")


def parse_ec11_pixels(line: str) -> dict[int, tuple[int, int, int]]:
    pixels: dict[int, tuple[int, int, int]] = {}
    if "~LED:STATUS detail=rgb_ec11" not in line or "ec11_rgb=" not in line:
        return pixels
    for match in RGB_RE.finditer(line):
        pixels[int(match.group("index"))] = (
            int(match.group("r")),
            int(match.group("g")),
            int(match.group("b")),
        )
    return pixels


def only_target_white(pixels: dict[int, tuple[int, int, int]], target: int) -> bool:
    if set(pixels) != set(range(1, 13)):
        return False
    for index, rgb in pixels.items():
        if index == target:
            r, g, b = rgb
            if not (r > 0 and r == g == b):
                return False
        elif rgb != (0, 0, 0):
            return False
    return True


def all_dark(pixels: dict[int, tuple[int, int, int]]) -> bool:
    return set(pixels) == set(range(1, 13)) and all(rgb == (0, 0, 0) for rgb in pixels.values())


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify EC11 12-pixel ring serial readback.")
    parser.add_argument("log", type=Path)
    args = parser.parse_args()

    text = args.log.read_text(encoding="utf-8", errors="replace")
    failures: list[str] = []

    for token in (
        "strip_transport_actual=status:rmt,ec11:spi2,key:spi3,edge:rmt",
        "spi_dma_actual=status:0,ec11:1,key:1,edge:0",
        "spi_dma_fallback=status:0,ec11:0,key:0,edge:0",
        "ec11:gpio5:count12:orderGRB:transportspi2:avail1",
        "spi_dma_req1:spi_dma1:spi_dma_fb0:refsLED7..LED10+LED15..LED16+LED23..LED28",
    ):
        if token not in text:
            failures.append(f"missing EC11 SPI2 DMA/readback token: {token}")

    if "LED TEST:PIXEL invalid" in text or "WRITE_ERROR" in text or "READ_ERROR" in text:
        failures.append("serial transcript contains TEST:PIXEL/serial error")

    lines = text.splitlines()
    observed: dict[int, tuple[int, str]] = {}
    pending_target: int | None = None
    for line_no, line in enumerate(lines, start=1):
        match = COMMAND_RE.match(line.strip())
        if match:
            target = int(match.group("index"))
            percent = int(match.group("percent"))
            if not 1 <= target <= 12:
                failures.append(f"invalid EC11 target index in command at line {line_no}: {target}")
            if percent != 100:
                failures.append(f"EC11 pixel sweep must use 100 percent, got {percent} at line {line_no}")
            pending_target = target
            continue

        pixels = parse_ec11_pixels(line)
        if not pixels or pending_target is None:
            continue
        if only_target_white(pixels, pending_target):
            observed[pending_target] = (line_no, line)
            pending_target = None

    missing = [index for index in range(1, 13) if index not in observed]
    if missing:
        failures.append(f"missing isolated white readback for EC11 px indexes: {missing}")

    dark_after_off = False
    seen_off = False
    for line in lines:
        if line.strip().lower() == "> ~led:off":
            seen_off = True
            continue
        if seen_off and all_dark(parse_ec11_pixels(line)):
            dark_after_off = True
    if not dark_after_off:
        failures.append("missing all-dark EC11 readback after final ~LED:OFF")

    if failures:
        print("FAIL: EC11 ring pixel log verification failed")
        for failure in failures:
            print(f" - {failure}")
        return 1

    print(
        "PASS: EC11 ring pixel log proves all 12 logical EC11 pixels can be "
        "isolated through the SPI2 DMA path and return dark after OFF."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
