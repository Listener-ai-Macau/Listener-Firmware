from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


RGB = tuple[int, int, int]
StatusFrame = dict[str, RGB]


def parse_status_frames(text: str) -> list[StatusFrame]:
    frames: list[StatusFrame] = []
    for line in text.splitlines():
        if "~LED:STATUS detail=rgb status_rgb=" not in line:
            continue
        match = re.search(r"status_rgb=([^\r\n]+)", line)
        if not match:
            continue
        frame: StatusFrame = {}
        for token in match.group(1).split(";"):
            if ":" not in token:
                continue
            name, value = token.split(":", 1)
            channels = value.split(",")
            if len(channels) != 3:
                continue
            try:
                frame[name] = tuple(int(channel) for channel in channels)  # type: ignore[assignment]
            except ValueError:
                continue
        if frame:
            frames.append(frame)
    return frames


def section(text: str, start_marker: str, next_markers: list[str]) -> str:
    start = text.find(start_marker)
    if start < 0:
        raise ValueError(f"missing section marker: {start_marker}")
    ends = [text.find(marker, start + len(start_marker)) for marker in next_markers]
    ends = [pos for pos in ends if pos >= 0]
    end = min(ends) if ends else len(text)
    return text[start:end]


def rgb_sum(rgb: RGB) -> int:
    return rgb[0] + rgb[1] + rgb[2]


def is_single_channel(rgb: RGB) -> bool:
    return sum(1 for channel in rgb if channel > 0) == 1


def is_whiteish(rgb: RGB) -> bool:
    r, g, b = rgb
    return min(rgb) > 0 and max(rgb) - min(rgb) <= 2


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify status-strip RGB energy balance evidence from a serial transcript."
    )
    parser.add_argument("transcript", type=Path)
    args = parser.parse_args()

    text = args.transcript.read_text(encoding="utf-8", errors="replace")
    failures: list[str] = []

    for token, message in (
        (
            "status_zone_brightness_percent=80 key_zone_brightness_percent=80",
            "brightness log must show restored Type status/key caps at 80/80",
        ),
        (
            "status_rgb_energy_balance=status_rgb_channel_sum_matches_single_channel_peak",
            "brightness log must expose the status RGB energy-balance contract",
        ),
        ("zone_brightness_is_hard_cap=1", "brightness log must show Type brightness is a hard cap"),
    ):
        if token not in text:
            failures.append(message)

    try:
        charging = section(text, "> ~LED:PREVIEW charging", ["> ~LED:PREVIEW ready"])
        ready = section(text, "> ~LED:PREVIEW ready", ["> ~LED:PREVIEW ok", "> ~LED:PREVIEW clear"])
    except ValueError as exc:
        failures.append(str(exc))
        charging = ""
        ready = ""
    try:
        connected = section(
            text,
            "> ~LED:PREVIEW connected",
            ["> ~LED:PREVIEW reconnect", "> ~LED:PREVIEW reconnecting", "> ~LED:PREVIEW clear", "> ~LED:PREVIEW sleep"],
        )
    except ValueError:
        connected = ""
    try:
        pairing = section(
            text,
            "> ~LED:PREVIEW pairing",
            ["> ~LED:PREVIEW connected", "> ~LED:PREVIEW reconnect", "> ~LED:PREVIEW reconnecting", "> ~LED:PREVIEW clear", "> ~LED:PREVIEW sleep"],
        )
    except ValueError:
        pairing = ""
    try:
        reconnecting = section(
            text,
            "> ~LED:PREVIEW reconnecting",
            ["> ~LED:PREVIEW clear", "> ~LED:PREVIEW sleep"],
        )
    except ValueError:
        reconnecting = ""
    try:
        idle_sleep = section(
            text,
            "> ~LED:PREVIEW sleep",
            ["> ~LED:PREVIEW disconnected", "> ~LED:PREVIEW clear"],
        )
    except ValueError:
        idle_sleep = ""
    try:
        disconnected = section(text, "> ~LED:PREVIEW disconnected", ["> ~LED:PREVIEW clear"])
    except ValueError:
        disconnected = ""

    ready_frames = parse_status_frames(ready)
    ready_pair: tuple[RGB, RGB] | None = None
    for frame in ready_frames:
        pwr = frame.get("PWR", (0, 0, 0))
        ble = frame.get("BLE", (0, 0, 0))
        if is_single_channel(pwr) and is_single_channel(ble) and rgb_sum(pwr) == rgb_sum(ble) and rgb_sum(ble) > 0:
            ready_pair = (pwr, ble)
            break
    if ready_pair is None:
        failures.append(f"ready scene must show single-channel PWR and BLE with equal energy; saw {ready_frames}")
        blue_cap_sum = 0
    else:
        _, ble = ready_pair
        blue_cap_sum = rgb_sum(ble)

    charging_frames = parse_status_frames(charging)
    white_samples: list[RGB] = []
    for frame in charging_frames:
        pwr = frame.get("PWR", (0, 0, 0))
        if is_whiteish(pwr):
            white_samples.append(pwr)
    if not white_samples:
        failures.append(f"charging scene must include a balanced white-ish PWR sample; saw {charging_frames}")
    elif blue_cap_sum > 0:
        too_bright = [rgb for rgb in white_samples if rgb_sum(rgb) > blue_cap_sum]
        if too_bright:
            failures.append(
                f"white PWR channel-sum must not exceed the Type-capped blue peak {blue_cap_sum}; too bright={too_bright}"
            )

    over_cap_whites: list[RGB] = []
    if blue_cap_sum > 0:
        for frame in parse_status_frames(text):
            for rgb in frame.values():
                if is_whiteish(rgb) and rgb_sum(rgb) > blue_cap_sum:
                    over_cap_whites.append(rgb)
        if over_cap_whites:
            failures.append(f"found white-ish status samples above blue cap sum {blue_cap_sum}: {over_cap_whites}")

    connected_ble_samples: list[RGB] = []
    if connected:
        connected_ble_all: list[RGB] = []
        for frame in parse_status_frames(connected):
            ble = frame.get("BLE", (0, 0, 0))
            connected_ble_all.append(ble)
            if rgb_sum(ble) > 0:
                connected_ble_samples.append(ble)
        if not connected_ble_samples:
            failures.append("active connected find-Type section must keep BLE visible until Type-ready or idle")
        elif blue_cap_sum > 0:
            dark_connected = [rgb for rgb in connected_ble_all if rgb_sum(rgb) == 0]
            peak_floor = max(1, (blue_cap_sum * 80 + 99) // 100)
            low_floor = max(1, (blue_cap_sum * 18 + 99) // 100)
            low_ceiling = max(low_floor, (blue_cap_sum * 45 + 99) // 100)
            peak_samples = [rgb for rgb in connected_ble_samples if rgb_sum(rgb) >= peak_floor]
            low_samples = [
                rgb for rgb in connected_ble_samples
                if low_floor <= rgb_sum(rgb) <= low_ceiling
            ]
            if dark_connected:
                failures.append(
                    f"active connected find-Type must not go dark before idle; dark samples={dark_connected}"
                )
            if not peak_samples:
                failures.append(
                    "connected find-Type double flash must keep its peak mapped near Type-ready blue "
                    f"(floor {peak_floor}, ready {blue_cap_sum}); saw={connected_ble_samples}"
                )
            if not low_samples:
                failures.append(
                    "connected find-Type low floor must be visible but clearly below the peak "
                    f"(expected {low_floor}..{low_ceiling}, ready {blue_cap_sum}); saw={connected_ble_samples}"
                )

    reconnecting_ble_samples: list[RGB] = []
    if reconnecting:
        for frame in parse_status_frames(reconnecting):
            ble = frame.get("BLE", (0, 0, 0))
            reconnecting_ble_samples.append(ble)
        dark_reconnecting = [rgb for rgb in reconnecting_ble_samples if rgb_sum(rgb) == 0]
        if not dark_reconnecting:
            failures.append(
                "active reconnecting must include a dark off phase; low blue floor is reserved for HID-only connected "
                f"find-Type. saw={reconnecting_ble_samples}"
            )

    pairing_ble_samples: list[RGB] = []
    if pairing:
        for frame in parse_status_frames(pairing):
            ble = frame.get("BLE", (0, 0, 0))
            pairing_ble_samples.append(ble)
        dark_pairing = [rgb for rgb in pairing_ble_samples if rgb_sum(rgb) == 0]
        if not dark_pairing:
            failures.append(
                "active pairing/repairing must include a dark off phase; low blue floor is reserved for HID-only "
                f"connected find-Type. saw={pairing_ble_samples}"
            )

    idle_ble_samples: list[RGB] = []
    if not idle_sleep:
        failures.append("log must include an idle/sleep section so BLE-off is proven as the idle marker")
    else:
        if "output_disabled=1 low_power_disabled=1" not in idle_sleep:
            failures.append("idle/sleep section must report output_disabled=1 low_power_disabled=1 before BLE is allowed to be dark")
        for frame in parse_status_frames(idle_sleep):
            idle_ble_samples.append(frame.get("BLE", (0, 0, 0)))
        if not idle_ble_samples:
            failures.append("idle/sleep section must include an RGB frame proving BLE is dark only after idle")
        lit_idle = [rgb for rgb in idle_ble_samples if rgb_sum(rgb) > 0]
        if lit_idle:
            failures.append(f"idle/sleep section must keep BLE dark as the idle marker; lit samples={lit_idle}")

    disconnected_ble_samples: list[RGB] = []
    if not disconnected:
        failures.append("log must include a preview-disconnected/no-host section proving no-host stays dark while active")
    else:
        if "ble=disconnected" not in disconnected:
            failures.append("preview-disconnected/no-host section must report ble=disconnected")
        active_disconnected = "output_disabled=0 low_power_disabled=0" in disconnected
        if not active_disconnected:
            failures.append("preview-disconnected/no-host section must remain active while proving BLE is dark")
        for frame in parse_status_frames(disconnected):
            disconnected_ble_samples.append(frame.get("BLE", (0, 0, 0)))
        if not disconnected_ble_samples:
            failures.append("preview-disconnected/no-host section must include an RGB frame")
        lit_disconnected = [rgb for rgb in disconnected_ble_samples if rgb_sum(rgb) > 0]
        if active_disconnected and lit_disconnected:
            failures.append(
                "active preview-disconnected/no-host must stay dark; low blue floor is reserved for HID-only connected "
                f"find-Type. lit samples={lit_disconnected}"
            )

    if failures:
        print("FAIL: status RGB energy-balance log verification failed")
        for failure in failures:
            print(f" - {failure}")
        return 1

    print(
        "PASS: status RGB log keeps white-ish PWR energy at or below the Type-capped single-channel "
        f"blue/green peak ({blue_cap_sum}); ready pair={ready_pair}; white_samples={white_samples}; "
        f"connected_ble_samples={connected_ble_samples}; reconnecting_ble_samples={reconnecting_ble_samples}; "
        f"pairing_ble_samples={pairing_ble_samples}; idle_ble_samples={idle_ble_samples}; "
        f"disconnected_ble_samples={disconnected_ble_samples}."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
