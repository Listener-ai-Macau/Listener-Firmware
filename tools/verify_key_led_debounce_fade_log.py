from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


RGB_RE = re.compile(r"px(?P<index>[1-4]):(?P<r>\d+),(?P<g>\d+),(?P<b>\d+)")
KEY_COMMAND_RE = re.compile(r"^>\s*~KEY:KEY(?P<index>[1-4]):SINGLE\s*$")


def fail(message: str) -> int:
    print(f"FAIL: {message}")
    return 1


def require(condition: bool, failures: list[str], message: str) -> None:
    if not condition:
        failures.append(message)


def require_any(text: str, tokens: tuple[str, ...], failures: list[str], message: str) -> None:
    if not any(token in text for token in tokens):
        failures.append(message)


def parse_fields(line: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for token in line.split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        fields[key] = value.strip()
    return fields


def latest_line(text: str, needle: str) -> str:
    lines = [line for line in text.splitlines() if needle in line]
    return lines[-1] if lines else ""


def key_rgb_lines(text: str) -> list[tuple[int, str, dict[int, tuple[int, int, int]]]]:
    parsed: list[tuple[int, str, dict[int, tuple[int, int, int]]]] = []
    for line_no, line in enumerate(text.splitlines(), start=1):
        if "~LED:STATUS detail=rgb_key" not in line or "key_rgb=" not in line:
            continue
        pixels: dict[int, tuple[int, int, int]] = {}
        for match in RGB_RE.finditer(line):
            pixels[int(match.group("index"))] = (
                int(match.group("r")),
                int(match.group("g")),
                int(match.group("b")),
            )
        if len(pixels) == 4:
            parsed.append((line_no, line, pixels))
    return parsed


def text_after_first_key_command(text: str) -> str:
    marker = "> ~KEY:"
    index = text.find(marker)
    return text[index:] if index >= 0 else text


def all_zero(pixels: dict[int, tuple[int, int, int]]) -> bool:
    return all(rgb == (0, 0, 0) for rgb in pixels.values())


def only_pixel_lit(pixels: dict[int, tuple[int, int, int]], index: int) -> bool:
    return all((rgb != (0, 0, 0)) == (px == index) for px, rgb in pixels.items())


def lit_pixels(pixels: dict[int, tuple[int, int, int]]) -> list[tuple[int, tuple[int, int, int]]]:
    return [(px, rgb) for px, rgb in pixels.items() if rgb != (0, 0, 0)]


def is_white(rgb: tuple[int, int, int]) -> bool:
    r, g, b = rgb
    return r == g == b and r > 0


def is_purple(rgb: tuple[int, int, int]) -> bool:
    r, g, b = rgb
    return r > 0 and g == 0 and b > r


def verify_key_feedback_latch_tx_contract(
    text: str,
    failures: list[str],
    context: str,
    *,
    require_independent_fade_contract: bool = True,
) -> None:
    contract = latest_line(text, "~LED:STATUS detail=contract")
    require(
        "key_lit_edge_tx=spi_dma_only" in contract,
        failures,
        f"{context} must read back that KEY lit-edge frames stay on SPI DMA",
    )
    require(
        "spi_ws2812_waveform=4bit_3m2_0x8_0xE" in contract,
        failures,
        f"{context} must read back the conservative 4-bit SPI WS2812 waveform",
    )
    require(
        "key_feedback_dynamic_tx=spi_dma_until_dark_latch" in contract,
        failures,
        f"{context} must read back that dynamic KEY feedback stays on SPI DMA until the final dark latch",
    )
    require(
        "key_dark_latch_rmt_writes=2" in contract,
        failures,
        f"{context} must read back the bounded KEY all-dark RMT latch write count",
    )
    require(
        "key_dark_latch_expiry_dirty=1" in contract,
        failures,
        f"{context} must read back the KEY feedback-expiry dark-latch dirty contract",
    )
    if require_independent_fade_contract:
        require(
            "key_single_white_hold_ms=160" in contract,
            failures,
            f"{context} must read back the accepted KEY single-click white hold before purple feedback",
        )
        require(
            "key_multi_key_independent_fade=1" in contract,
            failures,
            f"{context} must read back that KEY effects fade independently instead of clearing other keys",
        )
    pre_latch_attempts = (
        text.count("strip key SPI DMA pre-latch before non-DMA RMT latch")
        + text.count("strip key SPI DMA pre-latch failed before non-DMA RMT latch")
        + text.count("strip key SPI DMA pre-latch acquire failed before non-DMA RMT latch")
    )
    one_shot_latches = text.count("strip key non-DMA one-shot via RMT for SPI DMA latch")
    require(
        pre_latch_attempts >= one_shot_latches and one_shot_latches >= 1,
        failures,
        f"{context} must prove every KEY one-shot RMT latch had a preceding SPI DMA pre-latch attempt; pre_latch_attempts={pre_latch_attempts} one_shot_latches={one_shot_latches}",
    )
    lit_latch_writes = len(
        re.findall(
            r"strip key non-DMA one-shot via RMT for SPI DMA latch: .*rmt_writes=1 dark=0",
            text,
        )
    )
    require(
        lit_latch_writes == 0,
        failures,
        f"{context} must not use lit one-shot RMT; active KEY feedback must stay SPI DMA. lit_latch_writes={lit_latch_writes}",
    )
    dark_latch_writes = len(
        re.findall(
            r"strip key non-DMA one-shot via RMT for SPI DMA latch: .*rmt_writes=2 dark=1",
            text,
        )
    )
    require(
        dark_latch_writes >= 1,
        failures,
        f"{context} must include at least one KEY all-dark one-shot latch with rmt_writes=2; dark_latch_writes={dark_latch_writes}",
    )


def verify_preview_key_contract(text: str, failures: list[str]) -> None:
    contract = latest_line(text, "~LED:STATUS detail=contract")
    require(
        "key_release_fade_ms=700" in contract,
        failures,
        "status contract must expose the continuous key release fade window",
    )
    brightness = latest_line(text, "~LED:STATUS detail=brightness")
    require(brightness != "", failures, "missing ~LED:STATUS detail=brightness readback")
    brightness_fields = parse_fields(brightness)
    for key in (
        "key_zone_brightness_percent",
        "zone_brightness_is_hard_cap",
        "zone_brightness_effect_peak_cap",
        "zone_brightness_preserves_effect_percent",
    ):
        require(
            key in brightness_fields,
            failures,
            f"brightness readback missing {key}",
        )
    key_zone = brightness_fields.get("key_zone_brightness_percent")
    if key_zone is not None:
        require(key_zone.isdecimal() and 0 <= int(key_zone) <= 100, failures, f"invalid key brightness cap: {key_zone!r}")
    for key in (
        "zone_brightness_is_hard_cap",
        "zone_brightness_effect_peak_cap",
        "zone_brightness_preserves_effect_percent",
    ):
        require(brightness_fields.get(key) == "1", failures, f"brightness contract {key}={brightness_fields.get(key)!r}")

    strips = latest_line(text, "~LED:STATUS detail=strips")
    require(
        "key:gpio13:count4:orderGRB:transportspi3" in strips
        and "spi_dma_req1:spi_dma1:spi_dma_fb0:refsLED11..LED14" in strips,
        failures,
        "key strip must read back GPIO13 SPI3 DMA with no fallback",
    )

    for preview in (
        "~LED:PREVIEW recording_processing_status_led_only",
        "~LED:PREVIEW recording_processing",
        "~LED:PREVIEW ota_led_only",
        "~LED:PREVIEW clear",
    ):
        require(preview in text, failures, f"missing preview command evidence: {preview}")

    rgb_lines = key_rgb_lines(text)
    require(len(rgb_lines) >= 4, failures, "expected multiple key_rgb samples during product previews")
    for line_no, line, pixels in rgb_lines:
        require(all_zero(pixels), failures, f"product preview leaked key strip at line {line_no}: {line}")


def verify_key3_single(text: str, failures: list[str]) -> None:
    required_tokens = (
        "~KEY:GENERATED logical=KEY3 gesture=single result=ESP_OK",
        "custom key raw debounce candidate: logical=KEY3",
        "custom key stable transition: logical=KEY3 source=key3.gpio40.f15 raw_high=0 pressed=1",
        "custom key stable transition: logical=KEY3 source=key3.gpio40.f15 raw_high=1 pressed=0",
        "custom key single pending: logical=KEY3",
        "custom key fallback queued: logical=KEY3 source=key3.gpio40.f15 usage=F15 gesture=single",
    )
    for token in required_tokens:
        require(token in text, failures, f"missing KEY3 single evidence: {token}")
    require_any(
        text,
        (
            "hid_keyboard: send_usage usage=0x6A",
            "ble_hid: key3.gpio40.f15 HID usage dispatched immediately: usage=0x6A",
            "ble_hid: key3.gpio40.f15 HID usage queued: usage=0x6A",
        ),
        failures,
        "missing KEY3 single HID dispatch evidence: usage=0x6A",
    )
    require("raw-only short transition" not in text, failures, "KEY3 generated single should not use raw-only transition path")

    samples = key_rgb_lines(text)
    white_samples = [
        (line_no, pixels)
        for line_no, _line, pixels in samples
        if only_pixel_lit(pixels, 3)
        and pixels[3][0] == pixels[3][1] == pixels[3][2]
        and pixels[3][0] > 0
    ]
    purple_samples = [
        (line_no, pixels)
        for line_no, _line, pixels in samples
        if only_pixel_lit(pixels, 3)
        and pixels[3][0] > 0
        and pixels[3][1] == 0
        and pixels[3][2] > pixels[3][0]
    ]
    zero_samples = [(line_no, pixels) for line_no, _line, pixels in samples if all_zero(pixels)]
    require(white_samples, failures, "KEY3 local white press preview sample missing")
    require(purple_samples, failures, "KEY3 local single purple sample missing")
    release_line_no = next(
        (
            line_no
            for line_no, line in enumerate(text.splitlines(), start=1)
            if "custom key stable transition: logical=KEY3 source=key3.gpio40.f15 raw_high=1 pressed=0" in line
        ),
        0,
    )
    if white_samples and purple_samples:
        require(
            white_samples[0][0] < purple_samples[0][0],
            failures,
            "KEY3 white press preview must precede local purple single feedback",
        )
        post_release_white_before_purple = [
            pixels[3][0]
            for line_no, pixels in white_samples
            if line_no > release_line_no and line_no < purple_samples[0][0]
        ]
        require(
            not post_release_white_before_purple,
            failures,
            f"KEY3 stable release must switch directly to purple, not fade white first; post-release white samples={post_release_white_before_purple}",
        )
    if purple_samples and zero_samples:
        require(
            any(line_no > purple_samples[0][0] for line_no, _pixels in zero_samples),
            failures,
            "KEY3 key strip must return to zero after purple feedback",
        )


def verify_key_gestures(text: str, failures: list[str]) -> None:
    required_tokens = (
        "~KEY:GENERATED logical=KEY4 gesture=single result=ESP_OK",
        "custom key raw debounce candidate: logical=KEY4",
        "custom key single pending: logical=KEY4",
        "hid_keyboard: send_usage usage=0x6B",
        "custom key fallback queued: logical=KEY4 source=key4.gpio41.f16 usage=F16 gesture=single",
        "~KEY:GENERATED logical=KEY3 gesture=double result=ESP_OK",
        "hid_keyboard: send_usage usage=0x6E",
        "custom key gesture queued: logical=KEY3 source=key3.gpio40.f19 usage=F19 gesture=double",
        "~KEY:GENERATED logical=KEY4 gesture=long result=ESP_OK",
        "hid_keyboard: send_usage usage=0x73",
        "custom key gesture queued: logical=KEY4 source=key4.gpio41.f24 usage=F24 gesture=long",
    )
    for token in required_tokens:
        require(token in text, failures, f"missing key gesture evidence: {token}")
    require(
        text.count("custom key stable transition: logical=KEY3 source=key3.gpio40.f15 raw_high=0 pressed=1") >= 2,
        failures,
        "KEY3 double must include two debounced press transitions",
    )
    require(
        text.count("custom key stable transition: logical=KEY3 source=key3.gpio40.f15 raw_high=1 pressed=0") >= 2,
        failures,
        "KEY3 double must include two debounced release transitions",
    )
    require(
        "custom key fallback queued: logical=KEY3 source=key3.gpio40.f15 usage=F15 gesture=single" not in text,
        failures,
        "KEY3 double must not fall through to single F15 fallback",
    )
    require("raw-only short transition" not in text, failures, "generated gesture evidence must not use raw-only transition path")


def verify_key_cycle_stress(text: str, failures: list[str]) -> None:
    keys = (
        ("KEY1", "key1.gpio38.f13", "0x68", "F13"),
        ("KEY2", "key2.gpio39.f14", "0x69", "F14"),
        ("KEY3", "key3.gpio40.f15", "0x6A", "F15"),
        ("KEY4", "key4.gpio41.f16", "0x6B", "F16"),
    )
    for logical, source, usage_hex, usage_name in keys:
        require(
            text.count(f"~KEY:GENERATED logical={logical} gesture=single result=ESP_OK") >= 2,
            failures,
            f"{logical} cycle stress must enqueue at least two generated singles",
        )
        require(
            text.count(f"custom key raw debounce candidate: logical={logical}") >= 2,
            failures,
            f"{logical} cycle stress must show local raw white preview",
        )
        require(
            text.count(
                f"custom key stable transition: logical={logical} source={source} raw_high=0 pressed=1"
            )
            >= 2,
            failures,
            f"{logical} cycle stress must show debounced press transitions",
        )
        require(
            text.count(
                f"custom key stable transition: logical={logical} source={source} raw_high=1 pressed=0"
            )
            >= 2,
            failures,
            f"{logical} cycle stress must show debounced release transitions",
        )
        require(
            text.count(f"custom key single pending: logical={logical}") >= 2,
            failures,
            f"{logical} cycle stress must preserve immediate single-click purple feedback",
        )
        require(
            text.count(
                f"custom key fallback queued: logical={logical} source={source} usage={usage_name} gesture=single"
            )
            >= 2,
            failures,
            f"{logical} cycle stress must dispatch the expected single-click HID fallback",
        )
        require_any(
            text,
            (
                f"hid_keyboard: send_usage usage={usage_hex}",
                f"ble_hid: {source} HID usage dispatched immediately: usage={usage_hex}",
                f"ble_hid: {source} HID usage queued: usage={usage_hex}",
            ),
            failures,
            f"{logical} cycle stress missing HID usage evidence: usage={usage_hex}",
        )

    forbidden = (
        "raw-only short transition",
        "drop generated custom key event",
        "ESP_ERR_TIMEOUT",
        "ESP_ERR_INVALID_STATE",
        "ESP_ERR_INVALID_ARG",
    )
    for token in forbidden:
        require(token not in text, failures, f"key cycle stress log contains forbidden token: {token}")

    samples = key_rgb_lines(text)
    require(samples, failures, "key cycle stress must include key_rgb samples")
    for line_no, line, pixels in samples:
        lit = [px for px, rgb in pixels.items() if rgb != (0, 0, 0)]
        require(
            len(lit) <= 1,
            failures,
            f"key cycle stress leaked/coupled multiple key LEDs at line {line_no}: {line}",
        )


def verify_key_cycle_visible_stress(text: str, failures: list[str]) -> None:
    scoped = text_after_first_key_command(text)
    require(scoped != text, failures, "visible cycle stress must include at least one ~KEY command marker")
    require("command_read_ms=70" in text, failures, "visible cycle stress must record short press read windows")
    require("command_read_ms=1600" in text, failures, "visible cycle stress must record long LED status read windows")

    verify_key_cycle_stress(scoped, failures)

    for token in (
        "raw-duration tap accepted",
        "button state changed: pressed=1",
        "gesture=double",
        "gesture=long",
    ):
        require(token not in scoped, failures, f"visible cycle stress log contains forbidden token: {token}")

    samples = key_rgb_lines(scoped)
    require(len(samples) >= 8, failures, "visible cycle stress must include white and purple samples for all four keys")
    keys = (
        ("KEY1", "key1.gpio38.f13", 1),
        ("KEY2", "key2.gpio39.f14", 2),
        ("KEY3", "key3.gpio40.f15", 3),
        ("KEY4", "key4.gpio41.f16", 4),
    )
    for logical, source, index in keys:
        white_samples = [
            line_no
            for line_no, _line, pixels in samples
            if only_pixel_lit(pixels, index)
            and pixels[index][0] == pixels[index][1] == pixels[index][2]
            and pixels[index][0] > 0
        ]
        purple_samples = [
            line_no
            for line_no, _line, pixels in samples
            if only_pixel_lit(pixels, index)
            and pixels[index][0] > 0
            and pixels[index][1] == 0
            and pixels[index][2] > pixels[index][0]
        ]
        require(white_samples, failures, f"{logical} visible cycle stress missing white-only press sample")
        require(purple_samples, failures, f"{logical} visible cycle stress missing purple-only single feedback sample")
        if white_samples and purple_samples:
            require(
                white_samples[0] < purple_samples[0],
                failures,
                f"{logical} visible cycle stress must show white press before purple feedback",
            )


def verify_key_cross_key_isolation(text: str, failures: list[str]) -> None:
    verify_key_feedback_latch_tx_contract(text, failures, "cross-key isolation log")
    require(
        "key_multi_key_independent_fade=1" in text,
        failures,
        "cross-key isolation log must read back key_multi_key_independent_fade=1",
    )
    require(
        "key_dark_clear_tx=spi_dma_prelatch_then_one_shot_rmt_gpio_low" in text,
        failures,
        "cross-key isolation log must read back the KEY all-dark DMA-prelatch/RMT-latch contract",
    )
    require(
        "key_dark_latch_rmt_writes=2" in text,
        failures,
        "cross-key isolation log must read back key_dark_latch_rmt_writes=2",
    )
    require(
        "custom key cross-key pending single visual canceled" not in text,
        failures,
        "fast KEY1-KEY4 cycling must not clear another key's accepted fade to hide follow-light",
    )
    lines = text.splitlines()
    command_positions: list[tuple[int, int]] = []
    for line_no, line in enumerate(lines, start=1):
        match = KEY_COMMAND_RE.match(line.strip())
        if match:
            command_positions.append((line_no, int(match.group("index"))))

    require(
        len(command_positions) >= 4,
        failures,
        "cross-key isolation log must contain a fast KEY1-KEY4 single-click sequence",
    )
    for expected_index in range(1, 5):
        require(
            any(index == expected_index for _line_no, index in command_positions),
            failures,
            f"cross-key isolation log missing KEY{expected_index} command",
        )

    for expected_index in range(1, 5):
        logical = f"KEY{expected_index}"
        require(
            f"custom key fallback queued: logical={logical}" in text,
            failures,
            f"cross-key isolation log must preserve delayed HID fallback for {logical}",
        )
        require(
            f"custom key single visual already active: logical={logical}" in text,
            failures,
            f"cross-key isolation log must not restart {logical} purple feedback when delayed HID fires",
        )

    samples = key_rgb_lines(text)
    require(samples, failures, "cross-key isolation log must include at least one key_rgb readback")
    independent_samples = [
        (line_no, line, pixels)
        for line_no, line, pixels in samples
        if len(lit_pixels(pixels)) >= 2
        and any(is_purple(rgb) for _px, rgb in lit_pixels(pixels))
        and any(is_white(rgb) for _px, rgb in lit_pixels(pixels))
    ]
    require(
        independent_samples,
        failures,
        "cross-key isolation log must prove accepted independent KEY fade: one key may still be purple while the next key shows white",
    )
    if samples:
        require(
            all_zero(samples[-1][2]),
            failures,
            f"final cross-key isolation key_rgb readback must be all dark: {samples[-1][1]}",
        )


def verify_key4_live_residual_triage(text: str, failures: list[str]) -> None:
    require("serial_opened port=COM3" in text, failures, "KEY4 live triage must be captured from COM3")
    for command in (
        "> ~LED:STATUS",
        "> ~BOARD:GPIO",
        "> ~KEY:STATUS",
        "> ~KEY:KEY4:SINGLE",
    ):
        require(command in text, failures, f"KEY4 live triage missing prefixed control command: {command}")
    require(
        "SCRIPT RX input" not in text,
        failures,
        "KEY4 live triage commands must be consumed as prefixed control commands, not typed as script input",
    )
    require(
        "~KEY:GENERATED logical=KEY4 gesture=single result=ESP_OK" in text,
        failures,
        "KEY4 live triage must prove the generated KEY4 single command was accepted",
    )
    for token in (
        "custom key raw debounce candidate: logical=KEY4",
        "custom key stable transition: logical=KEY4 source=key4.gpio41.f16 raw_high=0 pressed=1",
        "custom key stable transition: logical=KEY4 source=key4.gpio41.f16 raw_high=1 pressed=0",
        "custom key fallback queued: logical=KEY4 source=key4.gpio41.f16 usage=F16 gesture=single",
    ):
        require(token in text, failures, f"KEY4 live triage missing KEY4 path evidence: {token}")

    verify_key_feedback_latch_tx_contract(
        text,
        failures,
        "KEY4 live residual triage",
        require_independent_fade_contract=False,
    )
    lit_latch_writes = len(
        re.findall(
            r"strip key non-DMA one-shot via RMT for SPI DMA latch: .*rmt_writes=1 dark=0",
            text,
        )
    )
    require(
        lit_latch_writes == 0,
        failures,
        f"KEY4 live triage must not use lit one-shot RMT; active feedback must stay SPI DMA. lit_latch_writes={lit_latch_writes}",
    )

    board_lines = [line for line in text.splitlines() if line.startswith("~BOARD:GPIO ")]
    require(board_lines, failures, "KEY4 live triage must include BOARD:GPIO readback")
    if board_lines:
        latest_board = board_lines[-1]
        require(
            "key4_pressed=0" in latest_board and "key_pressed_mask=0x00" in latest_board,
            failures,
            f"KEY4 live triage final BOARD:GPIO must show no stuck KEY4 press: {latest_board}",
        )

    summary = latest_line(text, "~LED:STATUS profile=")
    require(summary != "", failures, "KEY4 live triage must include LED summary readback")
    if summary:
        require(
            "KEY:0" in summary and "key_mask=0x00" in summary,
            failures,
            f"KEY4 live triage final LED summary must show key inactive: {summary}",
        )

    samples = key_rgb_lines(text)
    require(samples, failures, "KEY4 live triage must include key_rgb readback")
    for line_no, line, pixels in samples:
        lit = [px for px, rgb in pixels.items() if rgb != (0, 0, 0)]
        require(
            len(lit) <= 1,
            failures,
            f"KEY4 live triage leaked/coupled multiple key LEDs at line {line_no}: {line}",
        )
    if samples:
        require(
            all_zero(samples[-1][2]),
            failures,
            f"KEY4 live triage final key_rgb readback must be all dark: {samples[-1][1]}",
        )


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify key LED debounce/fade serial transcripts.")
    parser.add_argument("--settings-product-preview", required=True, type=Path)
    parser.add_argument("--key3-single-fade", required=True, type=Path)
    parser.add_argument("--key-gestures", required=True, type=Path)
    parser.add_argument("--key-cycle-stress", required=True, type=Path)
    parser.add_argument("--key-cycle-visible-stress", required=True, type=Path)
    parser.add_argument("--key-cross-key-isolation", type=Path)
    parser.add_argument("--key-feedback-latch-transport", type=Path)
    parser.add_argument("--key4-live-residual-triage", type=Path)
    args = parser.parse_args()

    failures: list[str] = []
    settings_text = args.settings_product_preview.read_text(encoding="utf-8", errors="replace")
    key3_text = args.key3_single_fade.read_text(encoding="utf-8", errors="replace")
    gestures_text = args.key_gestures.read_text(encoding="utf-8", errors="replace")
    cycle_text = args.key_cycle_stress.read_text(encoding="utf-8", errors="replace")
    visible_cycle_text = args.key_cycle_visible_stress.read_text(encoding="utf-8", errors="replace")
    cross_key_text = (
        args.key_cross_key_isolation.read_text(encoding="utf-8", errors="replace")
        if args.key_cross_key_isolation is not None
        else ""
    )
    key_feedback_latch_transport_text = (
        args.key_feedback_latch_transport.read_text(encoding="utf-8", errors="replace")
        if args.key_feedback_latch_transport is not None
        else ""
    )
    key4_live_residual_triage_text = (
        args.key4_live_residual_triage.read_text(encoding="utf-8", errors="replace")
        if args.key4_live_residual_triage is not None
        else ""
    )

    verify_preview_key_contract(settings_text, failures)
    verify_key3_single(key3_text, failures)
    verify_key_gestures(gestures_text, failures)
    verify_key_cycle_stress(cycle_text, failures)
    verify_key_cycle_visible_stress(visible_cycle_text, failures)
    if cross_key_text:
        verify_key_cross_key_isolation(cross_key_text, failures)
    if key_feedback_latch_transport_text:
        verify_key_feedback_latch_tx_contract(
            key_feedback_latch_transport_text,
            failures,
            "KEY feedback latch transport log",
        )
    if key4_live_residual_triage_text:
        verify_key4_live_residual_triage(key4_live_residual_triage_text, failures)

    if failures:
        for failure in failures:
            print(f" - {failure}")
        return fail("key LED debounce/fade log verification failed")
    print(
        "PASS: key LED debounce/fade logs prove the key strip is DMA-backed, "
        "product previews keep KEY dark, raw edges only preview locally, and KEY3/KEY4 "
        "single/double/long gestures dispatch on the stable path, with KEY1-KEY4 cycle "
        "stress protected against missed/coupled single-click regressions, including "
        "per-key white press, purple feedback readback, and accepted independent "
        "fade under fast key cycling."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
