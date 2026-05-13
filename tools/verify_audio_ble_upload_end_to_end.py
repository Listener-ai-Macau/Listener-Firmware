import argparse
import asyncio
import math
import pathlib
import struct
import time
import wave

import serial
import winsound

from capture_audio_ble_wav import (
    READY_MARKERS,
    SerialLogMonitor,
    run_ble_capture,
)

PCM_SAMPLE_RATE = 16000
PCM_WIDTH_BYTES = 2
PCM_CHANNELS = 1
ARTIFACT_DIR = pathlib.Path("tests/artifacts/audio")


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--device-name", default="Listener Keyboard")
    parser.add_argument("--capture-seconds", type=int, default=4)
    return parser.parse_args()


def generate_source_wav(path: pathlib.Path, seconds: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    frames = []
    total_samples = PCM_SAMPLE_RATE * seconds
    burst_period = PCM_SAMPLE_RATE // 2
    tone_hz = 880.0
    for i in range(total_samples):
        in_burst = (i % burst_period) < (burst_period // 3)
        amp = 0.75 if in_burst else 0.0
        sample = int(math.sin((2.0 * math.pi * tone_hz * i) / PCM_SAMPLE_RATE) * amp * 32767)
        frames.append(sample)
    with wave.open(str(path), "wb") as wav_file:
        wav_file.setnchannels(PCM_CHANNELS)
        wav_file.setsampwidth(PCM_WIDTH_BYTES)
        wav_file.setframerate(PCM_SAMPLE_RATE)
        wav_file.writeframes(struct.pack("<" + "h" * len(frames), *frames))


def rms_for_frames(frames):
    if not frames:
        return 0.0
    acc = 0.0
    for sample in frames:
        acc += float(sample) * float(sample)
    return math.sqrt(acc / float(len(frames)))


def read_wav_frames(path: pathlib.Path):
    with wave.open(str(path), "rb") as wav_file:
        raw = wav_file.readframes(wav_file.getnframes())
    frame_count = len(raw) // 2
    return list(struct.unpack("<" + "h" * frame_count, raw))


def compute_envelope(frames, window_samples=320):
    env = []
    for start in range(0, len(frames), window_samples):
        env.append(rms_for_frames(frames[start:start + window_samples]))
    return env


def normalize(env):
    if not env:
        return []
    mean = sum(env) / len(env)
    centered = [v - mean for v in env]
    max_abs = max(abs(v) for v in centered) or 1.0
    return [v / max_abs for v in centered]


def detect_active_runs(rms_values):
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


def best_envelope_correlation(source_env, recorded_env):
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


async def main_async(args):
    timestamp = time.strftime("%Y%m%d_%H%M%S")
    source_wav = ARTIFACT_DIR / f"source_ble_pattern_{timestamp}_16k_mono.wav"
    generate_source_wav(source_wav, args.capture_seconds)
    winsound.PlaySound(str(source_wav), winsound.SND_FILENAME | winsound.SND_ASYNC)
    try:
        class CaptureArgs:
            port = args.port
            device_name = args.device_name
            capture_seconds = args.capture_seconds
            output_dir = str(ARTIFACT_DIR)
            timeout_seconds = 60
            boot_timeout_seconds = 15
            notify_ready_timeout_seconds = 20

        with serial.Serial(args.port, 115200, timeout=0.05) as ser:
            ser.setDTR(False)
            ser.setRTS(False)
            ser.reset_input_buffer()
            serial_monitor = SerialLogMonitor(ser)
            await serial_monitor.wait_for_markers(READY_MARKERS, timeout_seconds=CaptureArgs.boot_timeout_seconds)
            ser.reset_input_buffer()
            await run_ble_capture(CaptureArgs(), ser, serial_monitor)
    finally:
        winsound.PlaySound(None, winsound.SND_PURGE)

    latest = sorted(ARTIFACT_DIR.glob("capture_ble_*_16k_mono.wav"), key=lambda p: p.stat().st_mtime, reverse=True)[0]
    source_frames = read_wav_frames(source_wav)
    recorded_frames = read_wav_frames(latest)
    source_env = compute_envelope(source_frames)
    recorded_env = compute_envelope(recorded_frames)
    corr, offset = best_envelope_correlation(source_env, recorded_env)
    peak = max(abs(v) for v in recorded_frames) if recorded_frames else 0
    recorded_runs = detect_active_runs(recorded_env)
    active_frame_count = sum(end - start for start, end in recorded_runs)
    if corr < 0.20 or peak < 500 or active_frame_count < 40:
        raise RuntimeError(
            f"verify_audio_ble_upload_end_to_end: failed corr={corr:.4f} peak={peak} runs={recorded_runs} active_frames={active_frame_count}"
        )
    print(f"source_wav={source_wav}")
    print(f"recorded_wav={latest}")
    print(f"best_corr={corr:.4f}")
    print(f"best_offset_frames={offset}")
    print(f"recorded_peak={peak}")
    print(f"recorded_runs={recorded_runs}")
    print(f"active_frame_count={active_frame_count}")


def main():
    args = parse_args()
    asyncio.run(main_async(args))


if __name__ == "__main__":
    main()
