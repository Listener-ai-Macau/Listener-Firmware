import argparse
import asyncio
import hashlib
import math
import pathlib
import random
import struct
import subprocess
import wave
from types import SimpleNamespace

import serial
import winsound

from capture_audio_ble_wav import (
    READY_MARKERS,
    SerialLogMonitor,
    get_paired_device_address_hex,
    open_serial_with_retry,
    reset_target_before_capture,
    run_ble_capture,
    send_cancel,
    send_toggle,
)

PCM_SAMPLE_RATE = 16000
PCM_WIDTH_BYTES = 2
PCM_CHANNELS = 1
ARTIFACT_DIR = pathlib.Path("tests")
TRANSIENT_SOURCE_DIR = ARTIFACT_DIR / "artifacts" / "audio_regression_sources"
DEFAULT_SERIAL_LOG_PATH = ARTIFACT_DIR / "capture_ble_latest.log"
RESTART_WINDOWS_BLUETOOTH_SCRIPT = pathlib.Path(__file__).with_name("restart_windows_bluetooth.ps1")
RECOVER_BLE_HID_HOST_SCRIPT = pathlib.Path(__file__).with_name("recover_ble_hid_host.ps1")
PASS_PACKET_LOSS_RATIO = 0.02
FAIL_PACKET_LOSS_RATIO = 0.12
ANALYSIS_MIN_CORR = 0.20
ANALYSIS_MIN_PEAK = 450
ANALYSIS_MIN_ACTIVE_FRAMES = 5


def add_common_capture_args(parser: argparse.ArgumentParser, capture_seconds_default: int) -> None:
    parser.add_argument("--port", required=True)
    parser.add_argument("--device-name", default="listener")
    parser.add_argument("--capture-seconds", type=int, default=capture_seconds_default)
    parser.add_argument("--timeout-seconds", type=int, default=None)
    parser.add_argument("--no-reset-before-capture", action="store_false", dest="reset_before_capture")
    parser.set_defaults(reset_before_capture=True)


def source_wav_path(scenario: str, suffix: str = "latest") -> pathlib.Path:
    normalized = scenario.lower()
    return TRANSIENT_SOURCE_DIR / f"source_ble_pattern_{normalized}_{suffix}_16k_mono.wav"


def resolve_source_seed(path: pathlib.Path, seconds: int) -> int:
    digest = hashlib.sha256(f"{path.stem}:{seconds}".encode("utf-8")).digest()
    return int.from_bytes(digest[:8], "little")


def build_speech_like_cycle(seed: int, cycle_samples: int) -> list[int]:
    rng = random.Random(seed)
    frames = [0] * cycle_samples
    cursor = 0

    while cursor < cycle_samples:
        short_pause = rng.randint(int(0.03 * PCM_SAMPLE_RATE), int(0.12 * PCM_SAMPLE_RATE))
        cursor += short_pause
        if cursor >= cycle_samples:
            break

        syllable_samples = rng.randint(int(0.09 * PCM_SAMPLE_RATE), int(0.26 * PCM_SAMPLE_RATE))
        syllable_samples = min(syllable_samples, cycle_samples - cursor)
        if syllable_samples <= 0:
            break

        base_hz = rng.uniform(135.0, 245.0)
        harmonic2_hz = base_hz * rng.uniform(1.9, 2.2)
        harmonic3_hz = base_hz * rng.uniform(2.8, 3.3)
        amplitude = rng.uniform(0.35, 0.82)
        vibrato_hz = rng.uniform(3.5, 6.5)
        tremolo_hz = rng.uniform(2.0, 4.5)
        phase1 = rng.uniform(0.0, 2.0 * math.pi)
        phase2 = rng.uniform(0.0, 2.0 * math.pi)
        phase3 = rng.uniform(0.0, 2.0 * math.pi)
        phase4 = rng.uniform(0.0, 2.0 * math.pi)

        attack_samples = max(1, int(syllable_samples * 0.18))
        release_samples = max(1, int(syllable_samples * 0.20))
        sustain_start = attack_samples
        sustain_end = max(sustain_start, syllable_samples - release_samples)

        for sample_offset in range(syllable_samples):
            target_index = cursor + sample_offset
            if target_index >= cycle_samples:
                break

            if sample_offset < sustain_start:
                envelope = float(sample_offset + 1) / float(attack_samples)
            elif sample_offset >= sustain_end:
                remaining = max(1, syllable_samples - sample_offset)
                envelope = float(remaining) / float(release_samples)
            else:
                envelope = 1.0

            time_seconds = float(sample_offset) / float(PCM_SAMPLE_RATE)
            vibrato = 1.0 + 0.018 * math.sin((2.0 * math.pi * vibrato_hz * time_seconds) + phase4)
            tremolo = 0.84 + 0.16 * math.sin((2.0 * math.pi * tremolo_hz * time_seconds) + phase3)
            carrier = (
                0.68 * math.sin((2.0 * math.pi * base_hz * vibrato * time_seconds) + phase1)
                + 0.22 * math.sin((2.0 * math.pi * harmonic2_hz * time_seconds) + phase2)
                + 0.10 * math.sin((2.0 * math.pi * harmonic3_hz * time_seconds) + phase3)
            )
            sample = amplitude * envelope * tremolo * carrier
            frames[target_index] = int(max(-32767.0, min(32767.0, sample * 32767.0)))

        cursor += syllable_samples

        if rng.random() < 0.35 and cursor < cycle_samples:
            fricative_samples = min(
                rng.randint(int(0.02 * PCM_SAMPLE_RATE), int(0.06 * PCM_SAMPLE_RATE)),
                cycle_samples - cursor,
            )
            fricative_hz = rng.uniform(1800.0, 3200.0)
            fricative_amp = rng.uniform(0.08, 0.18)
            phase = rng.uniform(0.0, 2.0 * math.pi)
            for sample_offset in range(fricative_samples):
                target_index = cursor + sample_offset
                time_seconds = float(sample_offset) / float(PCM_SAMPLE_RATE)
                envelope = 1.0 - (float(sample_offset) / float(max(1, fricative_samples)))
                sample = fricative_amp * envelope * math.sin((2.0 * math.pi * fricative_hz * time_seconds) + phase)
                mixed = frames[target_index] + int(sample * 32767.0)
                frames[target_index] = max(-32767, min(32767, mixed))
            cursor += fricative_samples

    return frames


def generate_source_wav(path: pathlib.Path, seconds: int, seed: int | None = None) -> int:
    path.parent.mkdir(parents=True, exist_ok=True)
    total_samples = PCM_SAMPLE_RATE * seconds
    source_seed = int(seed) if seed is not None else resolve_source_seed(path, seconds)
    cycle_samples = PCM_SAMPLE_RATE * 4
    cycle_frames = build_speech_like_cycle(source_seed, cycle_samples)
    frames = [cycle_frames[index % cycle_samples] for index in range(total_samples)]
    with wave.open(str(path), "wb") as wav_file:
        wav_file.setnchannels(PCM_CHANNELS)
        wav_file.setsampwidth(PCM_WIDTH_BYTES)
        wav_file.setframerate(PCM_SAMPLE_RATE)
        wav_file.writeframes(struct.pack("<" + "h" * len(frames), *frames))
    return source_seed


def rms_for_frames(frames: list[int]) -> float:
    if not frames:
        return 0.0
    accumulator = 0.0
    for sample in frames:
        accumulator += float(sample) * float(sample)
    return math.sqrt(accumulator / float(len(frames)))


def read_wav_frames(path: pathlib.Path) -> list[int]:
    with wave.open(str(path), "rb") as wav_file:
        raw = wav_file.readframes(wav_file.getnframes())
    frame_count = len(raw) // 2
    return list(struct.unpack("<" + "h" * frame_count, raw))


def compute_envelope(frames: list[int], window_samples: int = 320) -> list[float]:
    envelope = []
    for start in range(0, len(frames), window_samples):
        envelope.append(rms_for_frames(frames[start : start + window_samples]))
    return envelope


def normalize(envelope: list[float]) -> list[float]:
    if not envelope:
        return []
    mean = sum(envelope) / len(envelope)
    centered = [value - mean for value in envelope]
    max_abs = max(abs(value) for value in centered) or 1.0
    return [value / max_abs for value in centered]


def detect_active_runs(rms_values: list[float]) -> list[tuple[int, int]]:
    if not rms_values:
        return []

    peak = max(rms_values)
    threshold = max(peak * 0.12, 80.0)
    min_run_frames = 4

    runs = []
    run_start = None
    for index, value in enumerate(rms_values):
        if value >= threshold and run_start is None:
            run_start = index
        elif value < threshold and run_start is not None:
            if index - run_start >= min_run_frames:
                runs.append((run_start, index))
            run_start = None

    if run_start is not None and len(rms_values) - run_start >= min_run_frames:
        runs.append((run_start, len(rms_values)))
    return runs


def best_envelope_correlation(source_env: list[float], recorded_env: list[float]) -> tuple[float, int]:
    if not source_env or not recorded_env:
        return 0.0, 0

    source_norm = normalize(source_env)
    recorded_norm = normalize(recorded_env)

    best_corr = -1.0
    best_offset = 0
    min_overlap = 20

    for offset in range(-len(source_norm) + min_overlap, len(recorded_norm) - min_overlap):
        start_src = max(0, -offset)
        start_rec = max(0, offset)
        overlap = min(len(source_norm) - start_src, len(recorded_norm) - start_rec)
        if overlap < min_overlap:
            continue

        src_slice = source_norm[start_src : start_src + overlap]
        rec_slice = recorded_norm[start_rec : start_rec + overlap]
        numerator = sum(src * rec for src, rec in zip(src_slice, rec_slice))
        denom_src = math.sqrt(sum(src * src for src in src_slice))
        denom_rec = math.sqrt(sum(rec * rec for rec in rec_slice))
        if denom_src == 0.0 or denom_rec == 0.0:
            continue
        corr = numerator / (denom_src * denom_rec)
        if corr > best_corr:
            best_corr = corr
            best_offset = offset

    return best_corr, best_offset


def analyze_recording(source_wav: pathlib.Path, recorded_wav: pathlib.Path) -> dict[str, object]:
    source_frames = read_wav_frames(source_wav)
    recorded_frames = read_wav_frames(recorded_wav)
    source_env = compute_envelope(source_frames)
    recorded_env = compute_envelope(recorded_frames)
    corr, offset = best_envelope_correlation(source_env, recorded_env)
    peak = max(abs(value) for value in recorded_frames) if recorded_frames else 0
    recorded_runs = detect_active_runs(recorded_env)
    active_frame_count = sum(end - start for start, end in recorded_runs)
    analysis_pass = (
        corr >= ANALYSIS_MIN_CORR
        and peak >= ANALYSIS_MIN_PEAK
        and active_frame_count >= ANALYSIS_MIN_ACTIVE_FRAMES
    )
    analysis_failure_reason = ""
    if not analysis_pass:
        if corr < ANALYSIS_MIN_CORR:
            analysis_failure_reason = "correlation_below_threshold"
        elif peak < ANALYSIS_MIN_PEAK:
            analysis_failure_reason = "recorded_peak_below_threshold"
        else:
            analysis_failure_reason = "active_frame_count_below_threshold"
    return {
        "best_corr": corr,
        "best_offset_frames": offset,
        "recorded_peak": peak,
        "recorded_runs": recorded_runs,
        "active_frame_count": active_frame_count,
        "analysis_pass": analysis_pass,
        "analysis_result": "pass" if analysis_pass else "fail",
        "analysis_failure_reason": analysis_failure_reason,
    }


def default_post_stop_timeout_seconds(capture_seconds: int) -> int:
    return max(60, int(capture_seconds * 3 + 30))


def summary_int(summary: dict[str, object], *keys: str, default: int = 0) -> int:
    for key in keys:
        value = summary.get(key)
        if value is None:
            continue
        return int(value)
    return default


def make_capture_args(
    *,
    port: str,
    device_name: str,
    capture_seconds: int,
    capture_seconds_per_session: list[int] | None = None,
    session_pre_start_delay_seconds: list[float] | None = None,
    trigger_mode: str = "serial-toggle",
    max_sessions: int = 1,
    timeout_seconds: int | None = None,
    output_dir: pathlib.Path = ARTIFACT_DIR,
    serial_log_path: pathlib.Path = DEFAULT_SERIAL_LOG_PATH,
    boot_timeout_seconds: int = 15,
    ble_connect_timeout_seconds: int = 15,
    notify_ready_timeout_seconds: int = 45,
    reset_before_capture: bool = True,
):
    return SimpleNamespace(
        port=port,
        device_name=device_name,
        capture_seconds=capture_seconds,
        output_dir=str(output_dir),
        serial_log_path=str(serial_log_path),
        timeout_seconds=timeout_seconds if timeout_seconds is not None else default_post_stop_timeout_seconds(capture_seconds),
        boot_timeout_seconds=boot_timeout_seconds,
        ble_connect_timeout_seconds=ble_connect_timeout_seconds,
        notify_ready_timeout_seconds=notify_ready_timeout_seconds,
        trigger_mode=trigger_mode,
        max_sessions=max_sessions,
        capture_seconds_per_session=capture_seconds_per_session,
        session_pre_start_delay_seconds=session_pre_start_delay_seconds,
        reset_before_capture=reset_before_capture,
    )


async def capture_sessions(capture_args) -> list[dict[str, object]]:
    with open_serial_with_retry(capture_args.port, 115200, timeout=0.05) as ser:
        ser.setDTR(False)
        ser.setRTS(False)
        if getattr(capture_args, "reset_before_capture", True):
            reset_target_before_capture(ser)
        ser.reset_input_buffer()
        serial_monitor = SerialLogMonitor(ser)
        if getattr(capture_args, "reset_before_capture", True):
            await serial_monitor.wait_for_markers(READY_MARKERS, timeout_seconds=capture_args.boot_timeout_seconds)
        ser.reset_input_buffer()
        return await run_ble_capture(capture_args, ser, serial_monitor)


async def run_serial_cancel_probe(
    *,
    port: str,
    hold_seconds: float,
    reset_before_probe: bool = False,
    boot_timeout_seconds: int = 15,
    post_cancel_wait_seconds: float = 2.0,
) -> dict[str, object]:
    with open_serial_with_retry(port, 115200, timeout=0.05) as ser:
        ser.setDTR(False)
        ser.setRTS(False)
        if reset_before_probe:
            reset_target_before_capture(ser)
        ser.reset_input_buffer()
        serial_monitor = SerialLogMonitor(ser)
        if reset_before_probe:
            await serial_monitor.wait_for_markers(READY_MARKERS, timeout_seconds=boot_timeout_seconds)
        ser.reset_input_buffer()

        send_toggle(ser)
        deadline = asyncio.get_running_loop().time() + hold_seconds
        while asyncio.get_running_loop().time() < deadline:
            serial_monitor.poll_lines()
            await asyncio.sleep(0.05)

        send_cancel(ser)
        deadline = asyncio.get_running_loop().time() + post_cancel_wait_seconds
        while asyncio.get_running_loop().time() < deadline:
            serial_monitor.poll_lines()
            await asyncio.sleep(0.05)

        full_text = serial_monitor.full_text()
        cancel_requested = "record session cancel requested" in full_text
        cancel_completed = (
            "record session canceled" in full_text
            or "recording cancel source=" in full_text
            or "record session canceled before activation" in full_text
        )
        return {
            "result": "pass" if cancel_requested and cancel_completed else "fail",
            "cancel_requested": cancel_requested,
            "cancel_completed": cancel_completed,
            "serial_log": full_text,
        }


def validate_transport_summary(
    summary: dict[str, object],
    *,
    capture_seconds: int,
    min_ratio: float = 0.90,
    max_extra_seconds: float = 1.00,
) -> dict[str, object]:
    expected_packet_count = summary_int(summary, "expected_packet_count", "expected_chunk_count")
    received_packet_count = summary_int(summary, "received_packet_count", "chunk_count")
    missing_packet_count = summary_int(
        summary,
        "missing_packet_count",
        "missing_chunk_count",
        default=max(expected_packet_count - received_packet_count, 0),
    )
    received_pcm_bytes = summary_int(summary, "received_pcm_bytes", "pcm_bytes")
    duration_seconds = float(summary["duration_seconds"])
    min_duration_seconds = float(capture_seconds) * min_ratio
    max_duration_seconds = float(capture_seconds) + max_extra_seconds
    duration_ok = min_duration_seconds <= duration_seconds <= max_duration_seconds
    packet_loss_ratio = (
        float(missing_packet_count) / float(expected_packet_count)
        if expected_packet_count > 0
        else 1.0
    )
    transport_result = "pass"
    transport_failure_reason = ""
    transport_warning_reason = ""

    if expected_packet_count <= 0:
        transport_result = "fail"
        transport_failure_reason = "missing_expected_packet_count"
    elif received_packet_count <= 0 or received_pcm_bytes <= 0:
        transport_result = "fail"
        transport_failure_reason = "no_audio_packets_received"
    elif received_packet_count > expected_packet_count:
        transport_result = "fail"
        transport_failure_reason = "received_packet_count_exceeds_expected"
    elif not duration_ok:
        transport_result = "fail"
        transport_failure_reason = "duration_out_of_range"
    elif packet_loss_ratio > FAIL_PACKET_LOSS_RATIO:
        transport_result = "fail"
        transport_failure_reason = "packet_loss_above_fail_threshold"
    elif packet_loss_ratio > PASS_PACKET_LOSS_RATIO:
        transport_result = "warning"
        transport_warning_reason = "packet_loss_above_pass_threshold"
    return {
        "expected_packet_count": expected_packet_count,
        "received_packet_count": received_packet_count,
        "missing_packet_count": missing_packet_count,
        "received_pcm_bytes": received_pcm_bytes,
        "packet_loss_ratio": packet_loss_ratio,
        "duration_min_seconds": min_duration_seconds,
        "duration_max_seconds": max_duration_seconds,
        "duration_ok": duration_ok,
        "transport_result": transport_result,
        "transport_pass": transport_result == "pass",
        "transport_ok": transport_result != "fail",
        "transport_failure_reason": transport_failure_reason,
        "transport_warning_reason": transport_warning_reason,
    }


async def play_and_capture_serial_toggle(
    *,
    port: str,
    device_name: str,
    scenario: str,
    capture_seconds: int,
    source_label: str = "latest",
    timeout_seconds: int | None = None,
    reset_before_capture: bool = True,
    pre_start_delay_seconds: float = 0.0,
    output_dir: pathlib.Path = ARTIFACT_DIR,
    serial_log_path: pathlib.Path = DEFAULT_SERIAL_LOG_PATH,
) -> dict[str, object]:
    playback_source_wav = source_wav_path(scenario, source_label)
    analysis_source_wav = source_wav_path(
        scenario,
        f"{source_label}_reference_{capture_seconds}s",
    )
    playback_length_seconds = max(
        capture_seconds,
        int(math.ceil(float(capture_seconds) + max(2.0, float(pre_start_delay_seconds) + 2.0))),
    )
    source_seed = resolve_source_seed(analysis_source_wav, capture_seconds)
    generate_source_wav(playback_source_wav, playback_length_seconds, seed=source_seed)
    generate_source_wav(analysis_source_wav, capture_seconds, seed=source_seed)
    winsound.PlaySound(str(playback_source_wav), winsound.SND_FILENAME | winsound.SND_ASYNC)
    try:
        capture_args = make_capture_args(
            port=port,
            device_name=device_name,
            capture_seconds=capture_seconds,
            capture_seconds_per_session=[capture_seconds],
            session_pre_start_delay_seconds=[pre_start_delay_seconds],
            timeout_seconds=timeout_seconds,
            reset_before_capture=reset_before_capture,
            output_dir=output_dir,
            serial_log_path=serial_log_path,
        )
        session_summaries = await capture_sessions(capture_args)
    finally:
        winsound.PlaySound(None, winsound.SND_PURGE)

    if not session_summaries:
        raise RuntimeError(f"{scenario}: capture returned no session summaries")

    summary = dict(session_summaries[-1])
    recorded_wav = pathlib.Path(summary["wav_path"])
    if not recorded_wav.exists():
        raise RuntimeError(f"{scenario}: expected output wav missing: {recorded_wav}")

    analysis = analyze_recording(analysis_source_wav, recorded_wav)
    validation = validate_transport_summary(summary, capture_seconds=capture_seconds)
    summary.update(validation)
    summary.update(analysis)
    summary["source_wav"] = str(analysis_source_wav)
    summary["source_profile"] = "speech_like_loop"
    summary["source_seed"] = source_seed
    summary["pre_start_delay_seconds"] = float(pre_start_delay_seconds)
    summary["result"] = "pass"
    summary["failure_reason"] = ""
    summary["warning_reason"] = ""

    if validation["transport_result"] == "fail":
        summary["result"] = "fail"
        summary["failure_reason"] = validation["transport_failure_reason"] or "transport_validation_failed"
    elif analysis["analysis_result"] == "fail":
        summary["result"] = "fail"
        summary["failure_reason"] = analysis["analysis_failure_reason"] or "recording_analysis_failed"
    elif validation["transport_result"] == "warning":
        summary["result"] = "warning"
        summary["warning_reason"] = validation["transport_warning_reason"] or "transport_warning"

    return summary


async def play_and_capture_serial_toggle_after_cancel_probe(
    *,
    port: str,
    device_name: str,
    scenario: str,
    capture_seconds: int,
    cancel_hold_seconds: float,
    cancel_post_wait_seconds: float = 2.0,
    source_label: str = "latest",
    timeout_seconds: int | None = None,
    reset_before_capture: bool = True,
    pre_start_delay_seconds: float = 0.0,
    output_dir: pathlib.Path = ARTIFACT_DIR,
    serial_log_path: pathlib.Path = DEFAULT_SERIAL_LOG_PATH,
) -> dict[str, object]:
    playback_source_wav = source_wav_path(scenario, source_label)
    analysis_source_wav = source_wav_path(
        scenario,
        f"{source_label}_reference_{capture_seconds}s",
    )
    playback_length_seconds = max(
        capture_seconds,
        int(math.ceil(float(capture_seconds) + max(2.0, float(pre_start_delay_seconds) + 2.0))),
    )
    source_seed = resolve_source_seed(analysis_source_wav, capture_seconds)
    generate_source_wav(playback_source_wav, playback_length_seconds, seed=source_seed)
    generate_source_wav(analysis_source_wav, capture_seconds, seed=source_seed)

    with open_serial_with_retry(port, 115200, timeout=0.05) as ser:
        ser.setDTR(False)
        ser.setRTS(False)
        if reset_before_capture:
            reset_target_before_capture(ser)
        ser.reset_input_buffer()
        serial_monitor = SerialLogMonitor(ser)
        if reset_before_capture:
            await serial_monitor.wait_for_markers(READY_MARKERS, timeout_seconds=15)
        ser.reset_input_buffer()

        cancel_args = make_capture_args(
            port=port,
            device_name=device_name,
            capture_seconds=max(1, int(math.ceil(cancel_hold_seconds))),
            capture_seconds_per_session=[max(1, int(math.ceil(cancel_hold_seconds)))],
            session_pre_start_delay_seconds=[0.0],
            max_sessions=1,
            timeout_seconds=max(30, int(cancel_hold_seconds + cancel_post_wait_seconds + 20)),
            reset_before_capture=False,
            output_dir=output_dir,
            serial_log_path=serial_log_path,
        )
        cancel_args.session_cancel_after_start_seconds = cancel_hold_seconds
        cancel_args.session_cancel_post_wait_seconds = cancel_post_wait_seconds
        cancel_summaries = await run_ble_capture(cancel_args, ser, serial_monitor)
        cancel_probe_summary = cancel_summaries[-1] if cancel_summaries else {}
        cancel_requested = bool(cancel_probe_summary.get("cancel_requested"))
        cancel_completed = bool(cancel_probe_summary.get("cancel_completed"))
        if not cancel_requested or not cancel_completed:
            return {
                "result": "fail",
                "failure_reason": "short_cancel_probe_failed",
                "cancel_requested": cancel_requested,
                "cancel_completed": cancel_completed,
                "serial_log": serial_monitor.full_text(),
            }

        winsound.PlaySound(str(playback_source_wav), winsound.SND_FILENAME | winsound.SND_ASYNC)
        try:
            capture_args = make_capture_args(
                port=port,
                device_name=device_name,
                capture_seconds=capture_seconds,
                capture_seconds_per_session=[capture_seconds],
                session_pre_start_delay_seconds=[pre_start_delay_seconds],
                timeout_seconds=timeout_seconds,
                reset_before_capture=False,
                output_dir=output_dir,
                serial_log_path=serial_log_path,
            )
            session_summaries = await run_ble_capture(capture_args, ser, serial_monitor)
        finally:
            winsound.PlaySound(None, winsound.SND_PURGE)

    if not session_summaries:
        raise RuntimeError(f"{scenario}: capture returned no session summaries")

    summary = dict(session_summaries[-1])
    recorded_wav = pathlib.Path(summary["wav_path"])
    if not recorded_wav.exists():
        raise RuntimeError(f"{scenario}: expected output wav missing: {recorded_wav}")

    analysis = analyze_recording(analysis_source_wav, recorded_wav)
    validation = validate_transport_summary(summary, capture_seconds=capture_seconds)
    summary.update(validation)
    summary.update(analysis)
    summary["source_wav"] = str(analysis_source_wav)
    summary["source_profile"] = "speech_like_loop"
    summary["source_seed"] = source_seed
    summary["pre_start_delay_seconds"] = float(pre_start_delay_seconds)
    summary["cancel_hold_seconds"] = float(cancel_hold_seconds)
    summary["cancel_requested"] = cancel_requested
    summary["cancel_completed"] = cancel_completed
    summary["result"] = "pass"
    summary["failure_reason"] = ""
    summary["warning_reason"] = ""

    if validation["transport_result"] == "fail":
        summary["result"] = "fail"
        summary["failure_reason"] = validation["transport_failure_reason"] or "transport_validation_failed"
    elif analysis["analysis_result"] == "fail":
        summary["result"] = "fail"
        summary["failure_reason"] = analysis["analysis_failure_reason"] or "recording_analysis_failed"
    elif validation["transport_result"] == "warning":
        summary["result"] = "warning"
        summary["warning_reason"] = validation["transport_warning_reason"] or "transport_warning"

    return summary


async def play_and_capture_serial_toggle_multi_session(
    *,
    port: str,
    device_name: str,
    scenario: str,
    capture_seconds: int,
    session_count: int,
    capture_seconds_per_session: list[int] | None = None,
    source_label: str = "multi",
    timeout_seconds: int | None = None,
    reset_before_capture: bool = True,
    require_analysis: bool = True,
    pre_start_delay_seconds_per_session: list[float] | None = None,
    output_dir: pathlib.Path = ARTIFACT_DIR,
    serial_log_path: pathlib.Path = DEFAULT_SERIAL_LOG_PATH,
) -> list[dict[str, object]]:
    effective_capture_seconds_per_session = (
        [int(value) for value in capture_seconds_per_session]
        if capture_seconds_per_session is not None
        else [int(capture_seconds)] * session_count
    )
    if len(effective_capture_seconds_per_session) != session_count:
        raise RuntimeError(
            f"{scenario}: expected {session_count} capture durations, got {len(effective_capture_seconds_per_session)}"
        )
    effective_pre_start_delay_seconds = (
        [float(value) for value in pre_start_delay_seconds_per_session]
        if pre_start_delay_seconds_per_session is not None
        else [0.0] * session_count
    )
    if len(effective_pre_start_delay_seconds) != session_count:
        raise RuntimeError(
            f"{scenario}: expected {session_count} pre-start delays, got {len(effective_pre_start_delay_seconds)}"
        )

    source_wav = source_wav_path(scenario, source_label)
    total_capture_seconds = sum(effective_capture_seconds_per_session)
    total_pre_start_delay_seconds = sum(effective_pre_start_delay_seconds)
    source_seed = resolve_source_seed(source_wav, int(capture_seconds))
    generate_source_wav(
        source_wav,
        max(
            max(effective_capture_seconds_per_session),
            int(math.ceil(float(total_capture_seconds) + float(total_pre_start_delay_seconds) + 2.0)),
        ),
        seed=source_seed,
    )
    winsound.PlaySound(str(source_wav), winsound.SND_FILENAME | winsound.SND_ASYNC)
    try:
        capture_args = make_capture_args(
            port=port,
            device_name=device_name,
            capture_seconds=capture_seconds,
            capture_seconds_per_session=effective_capture_seconds_per_session,
            session_pre_start_delay_seconds=effective_pre_start_delay_seconds,
            max_sessions=session_count,
            timeout_seconds=timeout_seconds,
            reset_before_capture=reset_before_capture,
            output_dir=output_dir,
            serial_log_path=serial_log_path,
        )
        session_summaries = await capture_sessions(capture_args)
    finally:
        winsound.PlaySound(None, winsound.SND_PURGE)

    if len(session_summaries) != session_count:
        raise RuntimeError(
            f"{scenario}: expected {session_count} session summaries, got {len(session_summaries)}"
        )

    enriched = []
    reference_wav_cache: dict[int, pathlib.Path] = {}
    for index, raw_summary in enumerate(session_summaries, start=1):
        summary = dict(raw_summary)
        recorded_wav = pathlib.Path(summary["wav_path"])
        if not recorded_wav.exists():
            raise RuntimeError(f"{scenario}: expected output wav missing: {recorded_wav}")

        target_capture_seconds = int(summary.get("capture_seconds_target", capture_seconds))
        reference_wav = reference_wav_cache.get(target_capture_seconds)
        if reference_wav is None:
            reference_wav = source_wav_path(
                scenario,
                f"{source_label}_reference_{target_capture_seconds}s",
            )
            generate_source_wav(reference_wav, target_capture_seconds, seed=source_seed)
            reference_wav_cache[target_capture_seconds] = reference_wav

        analysis = analyze_recording(reference_wav, recorded_wav)
        validation = validate_transport_summary(summary, capture_seconds=target_capture_seconds)
        summary.update(validation)
        summary.update(analysis)
        summary["source_wav"] = str(reference_wav)
        summary["source_profile"] = "speech_like_loop"
        summary["source_seed"] = source_seed
        summary["result"] = "pass"
        summary["failure_reason"] = ""
        summary["warning_reason"] = ""
        summary["session_index"] = index
        summary["analysis_required"] = require_analysis
        summary["pre_start_delay_seconds"] = effective_pre_start_delay_seconds[index - 1]

        if validation["transport_result"] == "fail":
            summary["result"] = "fail"
            summary["failure_reason"] = validation["transport_failure_reason"] or "transport_validation_failed"
        elif require_analysis and analysis["analysis_result"] == "fail":
            summary["result"] = "fail"
            summary["failure_reason"] = analysis["analysis_failure_reason"] or "recording_analysis_failed"
        elif validation["transport_result"] == "warning":
            summary["result"] = "warning"
            summary["warning_reason"] = validation["transport_warning_reason"] or "transport_warning"
        enriched.append(summary)

    return enriched


def run_powershell_script(
    script_path: pathlib.Path,
    arguments: list[str],
    *,
    timeout_seconds: int = 60,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["powershell", "-ExecutionPolicy", "Bypass", "-File", str(script_path), *arguments],
        capture_output=True,
        text=True,
        check=True,
        timeout=timeout_seconds,
    )


def restart_windows_bluetooth(*, restart_pan_adapter: bool = False) -> subprocess.CompletedProcess[str]:
    arguments: list[str] = []
    if restart_pan_adapter:
        arguments.append("-RestartPanAdapter")
    return run_powershell_script(RESTART_WINDOWS_BLUETOOTH_SCRIPT, arguments)


def recover_ble_hid_host(device_name: str) -> subprocess.CompletedProcess[str]:
    address_hex = get_paired_device_address_hex(device_name)
    if address_hex is None:
        raise RuntimeError(f"unable to resolve paired BLE device address for '{device_name}'")
    return run_powershell_script(
        RECOVER_BLE_HID_HOST_SCRIPT,
        ["-DeviceName", device_name, "-BluetoothAddress", address_hex],
    )
