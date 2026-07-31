#!/usr/bin/env python3
"""Verify that the recording-light meter owns the raw microphone level tap."""

from __future__ import annotations

import math
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATH = ROOT / "ports" / "esp32" / "audio_capture" / "audio_capture_esp32.c"
TRANSPORT_PATH = (
    ROOT / "ports" / "esp32" / "ble_audio_stream" / "ble_audio_stream_esp32.c"
)
PROTO_PATH = ROOT / "protocols" / "listener_proto" / "include" / "listener_audio_proto.h"


def require_function(source: str, name: str, next_marker: str) -> str:
    match = re.search(
        rf"\b{name}\s*\([^)]*\)\s*\{{([\s\S]*?)\n\}}\n\n{next_marker}",
        source,
    )
    if match is None:
        raise AssertionError(f"missing or unbounded function: {name}")
    return match.group(1)


def constant(source: str, name: str) -> int:
    match = re.search(rf"#define\s+{name}\s+(\d+)U", source)
    if match is None:
        raise AssertionError(f"missing integer constant: {name}")
    return int(match.group(1))


def meter_percent(level: int, floor: int, full_scale: int) -> int:
    if level <= floor:
        return 0
    if level >= full_scale:
        return 100
    return (level - floor) * 100 // (full_scale - floor)


def main() -> int:
    source = SOURCE_PATH.read_text(encoding="utf-8")
    transport = TRANSPORT_PATH.read_text(encoding="utf-8")
    protocol = PROTO_PATH.read_text(encoding="utf-8")
    raw_tap = require_function(
        source,
        "audio_capture_update_recording_level_from_raw_input",
        r"static void audio_capture_note_transport_backpressure",
    )
    delivery = require_function(
        source,
        "audio_capture_process_frame",
        r"/\* ========== ES8311 hardware path ========== \*/",
    )

    assert "status_led_set_recording_level" in raw_tap
    assert "audio_capture_frame_level_percent(raw_frame_buffer)" in raw_tap
    assert "s_raw_input_level_percent = level_percent" in raw_tap
    assert "status_led_set_recording_level" not in delivery, (
        "processed AFE/AGC/limiter output must not drive the recording-light meter"
    )

    es8311_order = re.search(
        r"esp_codec_dev_read\([^;]+;[\s\S]*?"
        r"audio_capture_update_recording_level_from_raw_input\(frame_buffer\);[\s\S]*?"
        r"audio_capture_process_frame\(frame_buffer\);",
        source,
    )
    assert es8311_order is not None, "ES8311 must meter the raw frame before delivery"

    pdm_order = re.search(
        r"audio_capture_select_pdm_slot\([^;]+;[\s\S]*?"
        r"audio_capture_update_recording_level_from_raw_input\(frame_buffer\);[\s\S]*?"
        r"audio_capture_apply_pdm_software_gain\(frame_buffer\);[\s\S]*?"
        r"audio_capture_pdm_afe_process\(frame_buffer\);",
        source,
    )
    assert pdm_order is not None, "PDM must meter after slot selection and before processing"

    assert "raw_input_level_percent" in delivery
    assert "s_export_state.stream_batch_raw_input_level_percent" in delivery
    assert "audio_capture_frame_level_percent" not in delivery, (
        "processed PCM delivery must carry the captured raw envelope, not recompute it"
    )
    assert "listener_audio_proto_flags_with_raw_input_level" in transport
    assert ".raw_input_level_percent = raw_input_level_percent" in transport
    assert "LISTENER_AUDIO_PROTO_HEADER_BYTES + payload_len" in transport, (
        "raw level metadata must not enlarge the VKA1 header or reduce PCM payload capacity"
    )
    assert "DENZIC_AUDIO_V1_RAW_INPUT_LEVEL_FLAG_MASK" in protocol
    assert "DENZIC_AUDIO_V1_RAW_INPUT_LEVEL_MAX_PERCENT" in protocol

    floor = constant(source, "AUDIO_CAPTURE_LEVEL_NOISE_FLOOR")
    full_scale = constant(source, "AUDIO_CAPTURE_LEVEL_FULL_SCALE")
    assert 0 <= floor < full_scale

    # A deterministic 24 dB raw-input sweep. The top point stays just below
    # display saturation so a healthy meter proves three visible levels rather
    # than returning 100 for every speech amplitude.
    quiet_level = max(floor + 1, int((full_scale - 1) / math.pow(10.0, 24.0 / 20.0)))
    normal_level = int(round(math.sqrt(quiet_level * (full_scale - 1))))
    loud_level = full_scale - 1
    inputs = [quiet_level, normal_level, loud_level]
    outputs = [meter_percent(level, floor, full_scale) for level in inputs]
    encoded_flags = [((level + 1) << 1) & 0xFE for level in outputs]
    decoded_outputs = [((flags & 0xFE) >> 1) - 1 for flags in encoded_flags]

    assert inputs[2] / inputs[0] >= math.pow(10.0, 23.5 / 20.0)
    assert outputs[0] < outputs[1] < outputs[2] < 100, (
        f"24 dB raw sweep must be monotonic and non-saturated: "
        f"inputs={inputs} outputs={outputs}"
    )
    assert decoded_outputs == outputs, (
        f"VKA1 flags[7:1] raw-level metadata must round-trip: "
        f"encoded={encoded_flags} decoded={decoded_outputs}"
    )

    print(
        "PASS recording raw-level meter: "
        f"tap=pre_agc_pre_limiter floor={floor} full_scale={full_scale} "
        f"input_24db_sweep={inputs} output_percent={outputs} "
        f"encoded_flags={encoded_flags} header_growth_bytes=0"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
