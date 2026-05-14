import argparse
import asyncio
import math
import pathlib
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
    run_ble_capture,
)

PCM_SAMPLE_RATE = 16000
PCM_WIDTH_BYTES = 2
PCM_CHANNELS = 1
ARTIFACT_DIR = pathlib.Path("tests")
TRANSIENT_SOURCE_DIR = ARTIFACT_DIR / "artifacts" / "audio_regression_sources"
DEFAULT_SERIAL_LOG_PATH = ARTIFACT_DIR / "capture_ble_latest.log"
RESTART_WINDOWS_BLUETOOTH_SCRIPT = pathlib.Path(__file__).with_name("restart_windows_bluetooth.ps1")
RECOVER_BLE_HID_HOST_SCRIPT = pathlib.Path(__file__).with_name("recover_ble_hid_host.ps1")


def add_common_capture_args(parser: argparse.ArgumentParser, capture_seconds_default: int) -> None:
    parser.add_argument("--port", required=True)
    parser.add_argument("--device-name", default="Listener Keyboard")
    parser.add_argument("--capture-seconds", type=int, default=capture_seconds_default)


def source_wav_path(scenario: str, suffix: str = "latest") -> pathlib.Path:
    normalized = scenario.lower()
    return TRANSIENT_SOURCE_DIR / f"source_ble_pattern_{normalized}_{suffix}_16k_mono.wav"


def generate_source_wav(path: pathlib.Path, seconds: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    frames = []
    total_samples = PCM_SAMPLE_RATE * seconds
    burst_period = PCM_SAMPLE_RATE // 2
    tone_hz = 880.0
    for index in range(total_samples):
        in_burst = (index % burst_period) < (burst_period // 3)
        amplitude = 0.75 if in_burst else 0.0
        sample = int(math.sin((2.0 * math.pi * tone_hz * index) / PCM_SAMPLE_RATE) * amplitude * 32767)
        frames.append(sample)
    with wave.open(str(path), "wb") as wav_file:
        wav_file.setnchannels(PCM_CHANNELS)
        wav_file.setsampwidth(PCM_WIDTH_BYTES)
        wav_file.setframerate(PCM_SAMPLE_RATE)
        wav_file.writeframes(struct.pack("<" + "h" * len(frames), *frames))


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
    return {
        "best_corr": corr,
        "best_offset_frames": offset,
        "recorded_peak": peak,
        "recorded_runs": recorded_runs,
        "active_frame_count": active_frame_count,
        "analysis_pass": corr >= 0.20 and peak >= 500 and active_frame_count >= 40,
    }


def default_post_stop_timeout_seconds(capture_seconds: int) -> int:
    return max(60, int(capture_seconds * 3 + 30))


def make_capture_args(
    *,
    port: str,
    device_name: str,
    capture_seconds: int,
    trigger_mode: str = "serial-toggle",
    max_sessions: int = 1,
    timeout_seconds: int | None = None,
    output_dir: pathlib.Path = ARTIFACT_DIR,
    serial_log_path: pathlib.Path = DEFAULT_SERIAL_LOG_PATH,
    boot_timeout_seconds: int = 15,
    notify_ready_timeout_seconds: int = 20,
):
    return SimpleNamespace(
        port=port,
        device_name=device_name,
        capture_seconds=capture_seconds,
        output_dir=str(output_dir),
        serial_log_path=str(serial_log_path),
        timeout_seconds=timeout_seconds if timeout_seconds is not None else default_post_stop_timeout_seconds(capture_seconds),
        boot_timeout_seconds=boot_timeout_seconds,
        notify_ready_timeout_seconds=notify_ready_timeout_seconds,
        trigger_mode=trigger_mode,
        max_sessions=max_sessions,
    )


async def capture_sessions(capture_args) -> list[dict[str, object]]:
    with serial.Serial(capture_args.port, 115200, timeout=0.05) as ser:
        ser.setDTR(False)
        ser.setRTS(False)
        ser.reset_input_buffer()
        serial_monitor = SerialLogMonitor(ser)
        await serial_monitor.wait_for_markers(READY_MARKERS, timeout_seconds=capture_args.boot_timeout_seconds)
        ser.reset_input_buffer()
        return await run_ble_capture(capture_args, ser, serial_monitor)


def validate_transport_summary(
    summary: dict[str, object],
    *,
    capture_seconds: int,
    min_ratio: float = 0.90,
    max_extra_seconds: float = 1.00,
) -> dict[str, object]:
    duration_seconds = float(summary["duration_seconds"])
    min_duration_seconds = float(capture_seconds) * min_ratio
    max_duration_seconds = float(capture_seconds) + max_extra_seconds
    duration_ok = min_duration_seconds <= duration_seconds <= max_duration_seconds
    transport_ok = (
        int(summary["missing_chunk_count"]) == 0
        and int(summary["expected_chunk_count"]) == int(summary["chunk_count"])
        and duration_ok
    )
    return {
        "duration_min_seconds": min_duration_seconds,
        "duration_max_seconds": max_duration_seconds,
        "duration_ok": duration_ok,
        "transport_ok": transport_ok,
    }


async def play_and_capture_serial_toggle(
    *,
    port: str,
    device_name: str,
    scenario: str,
    capture_seconds: int,
    source_label: str = "latest",
    timeout_seconds: int | None = None,
) -> dict[str, object]:
    source_wav = source_wav_path(scenario, source_label)
    generate_source_wav(source_wav, capture_seconds)
    winsound.PlaySound(str(source_wav), winsound.SND_FILENAME | winsound.SND_ASYNC)
    try:
        capture_args = make_capture_args(
            port=port,
            device_name=device_name,
            capture_seconds=capture_seconds,
            timeout_seconds=timeout_seconds,
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

    analysis = analyze_recording(source_wav, recorded_wav)
    validation = validate_transport_summary(summary, capture_seconds=capture_seconds)
    summary.update(validation)
    summary.update(analysis)
    summary["source_wav"] = str(source_wav)
    summary["result"] = "pass" if validation["transport_ok"] else "fail"
    summary["failure_reason"] = "" if validation["transport_ok"] else "transport_validation_failed"
    return summary


def run_powershell_script(script_path: pathlib.Path, arguments: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["powershell", "-ExecutionPolicy", "Bypass", "-File", str(script_path), *arguments],
        capture_output=True,
        text=True,
        check=True,
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
