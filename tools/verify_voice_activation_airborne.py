from __future__ import annotations

import argparse
import asyncio
import hashlib
import json
import os
import pathlib
import random
import re
import struct
import wave

from ble_audio_regression_common import (
    WavPlayback,
    analyze_recording,
    capture_sessions,
    generate_profile_tts_wav,
    make_capture_args,
)
from capture_audio_ble_wav import configure_utf8_stdio


VOICE_ACTIVATION_START_ORIGIN = 1
TARGET_PEAK_NEGATIVE_3_DBFS = 23197


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--device-name", default="listener")
    parser.add_argument("--bluetooth-address", default=None)
    parser.add_argument("--timeout-seconds", type=int, default=90)
    parser.add_argument(
        "--artifact-dir",
        default=".artifacts/listener-1.0.4-recording-polish/airborne",
    )
    parser.add_argument("--playback-volume-percent", type=int, default=80)
    parser.add_argument("--tts-rate", type=int, default=0)
    parser.add_argument("--tts-gain", type=float, default=4.0)
    parser.add_argument(
        "--fixture-profile",
        choices=["normal", "soft", "loud", "fast", "far"],
        default="normal",
    )
    return parser.parse_args()


def extract_int(pattern: str, text: str) -> int | None:
    match = re.search(pattern, text)
    return int(match.group(1)) if match else None


def apply_far_field_fixture(path: pathlib.Path) -> None:
    with wave.open(str(path), "rb") as wav:
        params = wav.getparams()
        raw = wav.readframes(wav.getnframes())
    samples = list(struct.unpack("<" + "h" * (len(raw) // 2), raw))
    rng = random.Random(10404)
    output: list[int] = []
    for index, sample in enumerate(samples):
        value = float(sample) * 0.55
        if index >= 83:
            value += float(samples[index - 83]) * 0.22
        if index >= 211:
            value += float(samples[index - 211]) * 0.12
        value += rng.uniform(-14.0, 14.0)
        output.append(max(-32768, min(32767, int(round(value)))))
    with wave.open(str(path), "wb") as wav:
        wav.setparams(params)
        wav.writeframes(struct.pack("<" + "h" * len(output), *output))


async def main_async(args: argparse.Namespace) -> None:
    artifact_dir = pathlib.Path(args.artifact_dir)
    artifact_dir.mkdir(parents=True, exist_ok=True)
    source_wav = artifact_dir / (
        f"wake-phrase-start-recording-{args.fixture_profile}-16k-mono.wav"
    )
    serial_log = artifact_dir / "voice-activation-serial.log"
    report_path = artifact_dir / "voice-activation-report.json"
    generate_profile_tts_wav(
        source_wav,
        "开始录音",
        tts_rate=args.tts_rate,
        tts_gain=args.tts_gain,
    )
    if args.fixture_profile == "far":
        apply_far_field_fixture(source_wav)

    os.environ["LISTENER_TEST_PLAYBACK_VOLUME_PERCENT"] = str(
        max(0, min(100, args.playback_volume_percent))
    )
    playback = WavPlayback(source_wav)
    capture_args = make_capture_args(
        port=args.port,
        device_name=args.device_name,
        bluetooth_address=args.bluetooth_address,
        capture_seconds=4,
        trigger_mode="voice-activation",
        timeout_seconds=args.timeout_seconds,
        output_dir=artifact_dir,
        serial_log_path=serial_log,
        reset_before_capture=False,
    )
    capture_args.session_start_callbacks = [lambda: playback.__enter__()]
    try:
        summaries = await capture_sessions(capture_args)
    finally:
        playback.__exit__(None, None, None)

    if len(summaries) != 1:
        raise RuntimeError(
            f"expected exactly one voice-activation session, got {len(summaries)}"
        )
    summary = dict(summaries[0])
    recorded_wav = pathlib.Path(str(summary["wav_path"]))
    serial_text = serial_log.read_text(encoding="utf-8", errors="replace")
    analysis = analyze_recording(source_wav, recorded_wav)

    input_clipped = extract_int(r"input_clipped_samples=(\d+)", serial_text)
    output_clipped = extract_int(r"output_clipped_samples=(\d+)", serial_text)
    output_peak = extract_int(r"output_peak=(\d+)", serial_text)
    actual_preroll_ms = extract_int(r"actual_preroll_ms=(\d+)", serial_text)
    ring_full_count = serial_text.count("Ringbuffer of AFE(FEED) is full")
    automatic_start_count = serial_text.count(
        "recording start source=voice_activation.auto_start"
    )
    promotion_count = serial_text.count("promotes hidden automatic candidate")
    duration_seconds = float(summary["duration_seconds"])
    missing_packet_count = int(summary["missing_packet_count"])
    duplicate_packet_count = int(summary["duplicate_packet_count"])
    start_origin = summary.get("session_start_origin")

    checks = {
        "one_session": len(summaries) == 1,
        "voice_activation_origin": start_origin == VOICE_ACTIVATION_START_ORIGIN,
        "one_automatic_start": automatic_start_count == 1,
        "no_manual_promotion": promotion_count == 0,
        "actual_preroll_1000ms": actual_preroll_ms == 1000,
        "duration_bounded": 1.5 <= duration_seconds <= 4.5,
        "zero_missing_packets": missing_packet_count == 0,
        "zero_duplicate_packets": duplicate_packet_count == 0,
        "zero_input_clips": input_clipped == 0,
        "zero_output_clips": output_clipped == 0,
        "output_peak_at_or_below_negative_3_dbfs": (
            output_peak is not None
            and output_peak <= TARGET_PEAK_NEGATIVE_3_DBFS
        ),
        "zero_afe_ring_full": ring_full_count == 0,
        "airborne_waveform_detected": analysis["analysis_result"] == "pass",
    }
    failures = [name for name, passed in checks.items() if not passed]
    report = {
        "result": "PASS" if not failures else "FAIL",
        "failures": failures,
        "checks": checks,
        "source_wav": str(source_wav.resolve()),
        "source_sha256": hashlib.sha256(source_wav.read_bytes()).hexdigest(),
        "recorded_wav": str(recorded_wav.resolve()),
        "recorded_sha256": hashlib.sha256(recorded_wav.read_bytes()).hexdigest(),
        "serial_log": str(serial_log.resolve()),
        "session_id": summary["session_id"],
        "session_start_origin": start_origin,
        "actual_preroll_ms": actual_preroll_ms,
        "duration_seconds": duration_seconds,
        "received_packet_count": int(summary["received_packet_count"]),
        "expected_packet_count": int(summary["expected_packet_count"]),
        "missing_packet_count": missing_packet_count,
        "duplicate_packet_count": duplicate_packet_count,
        "input_clipped_samples": input_clipped,
        "output_clipped_samples": output_clipped,
        "output_peak": output_peak,
        "afe_ring_full_count": ring_full_count,
        "automatic_start_count": automatic_start_count,
        "manual_promotion_count": promotion_count,
        "fixture_profile": args.fixture_profile,
        "playback_volume_percent": args.playback_volume_percent,
        "tts_rate": args.tts_rate,
        "tts_gain": args.tts_gain,
        "waveform_analysis": analysis,
    }
    report_path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(report, ensure_ascii=False, indent=2), flush=True)
    if failures:
        raise RuntimeError(
            "voice activation airborne checks failed: " + ", ".join(failures)
        )


def main() -> None:
    configure_utf8_stdio()
    asyncio.run(main_async(parse_args()))


if __name__ == "__main__":
    main()
