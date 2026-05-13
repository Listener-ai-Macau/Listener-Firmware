import argparse
import math
import pathlib
import struct
import threading
import time
import wave
import winsound

import serial


SAMPLE_RATE = 16000
BITS_PER_SAMPLE = 16
CHANNELS = 1
FRAME_MS = 20
FRAME_SAMPLES = SAMPLE_RATE * FRAME_MS // 1000
FRAME_BYTES = FRAME_SAMPLES * 2
EXPECTED_FREQS = (440, 660, 880)
TEST_TONE_FREQ = 880


def make_wav_header(pcm_bytes: int) -> bytes:
    byte_rate = SAMPLE_RATE * CHANNELS * (BITS_PER_SAMPLE // 8)
    block_align = CHANNELS * (BITS_PER_SAMPLE // 8)
    return (
        b"RIFF"
        + struct.pack("<I", 36 + pcm_bytes)
        + b"WAVE"
        + b"fmt "
        + struct.pack("<IHHIIHH", 16, 1, CHANNELS, SAMPLE_RATE, byte_rate, block_align, BITS_PER_SAMPLE)
        + b"data"
        + struct.pack("<I", pcm_bytes)
    )


def write_wav(path: pathlib.Path, pcm_data: bytes) -> None:
    path.write_bytes(make_wav_header(len(pcm_data)) + pcm_data)


def generate_test_pattern(path: pathlib.Path) -> bytes:
    segments = [
        ("silence", 0.20),
        ("tone", 0.45),
        ("silence", 0.18),
        ("tone", 0.70),
        ("silence", 0.18),
        ("tone", 0.35),
        ("silence", 0.18),
        ("tone", 0.85),
        ("silence", 0.18),
        ("tone", 0.55),
        ("silence", 0.18),
    ]

    pcm = bytearray()
    amplitude = 0.75 * 32767.0
    fade_samples = int(0.01 * SAMPLE_RATE)

    for segment_type, duration_s in segments:
        total_samples = int(duration_s * SAMPLE_RATE)
        for sample_index in range(total_samples):
            if segment_type == "silence":
                value = 0
            else:
                phase = 2.0 * math.pi * TEST_TONE_FREQ * sample_index / SAMPLE_RATE
                envelope = 1.0
                if sample_index < fade_samples:
                    envelope = sample_index / max(1, fade_samples)
                elif total_samples - sample_index < fade_samples:
                    envelope = (total_samples - sample_index) / max(1, fade_samples)
                value = int(amplitude * math.sin(phase) * envelope)
            pcm.extend(struct.pack("<h", value))

    write_wav(path, bytes(pcm))
    return bytes(pcm)


def play_wav_after_delay(wav_path: pathlib.Path, delay_s: float) -> None:
    def worker() -> None:
        time.sleep(delay_s)
        winsound.PlaySound(str(wav_path), winsound.SND_FILENAME)

    thread = threading.Thread(target=worker, daemon=True)
    thread.start()


def read_ready(ser: serial.Serial, timeout_s: int) -> str:
    deadline = time.time() + timeout_s
    boot_buffer = bytearray()
    while time.time() < deadline:
        data = ser.read(4096)
        if data:
            boot_buffer.extend(data)
            text = boot_buffer.decode("utf-8", errors="replace")
            if "audio_capture: codec init ok" in text and "ble_hid: USB SERIAL INPUT READY" in text:
                return text
    raise RuntimeError("verify_audio_capture_session_end_to_end: device did not become ready before timeout")


def capture_toggle_session(
    ser: serial.Serial,
    capture_seconds: float,
    export_timeout_s: int,
) -> bytes:
    ser.write(b"~VREC:TOGGLE\n")
    ser.flush()
    time.sleep(capture_seconds)
    ser.write(b"~VREC:TOGGLE\n")
    ser.flush()

    export_deadline = time.time() + export_timeout_s
    line_buffer = bytearray()
    pcm_data = bytearray()
    saw_begin = False
    saw_end = False
    while time.time() < export_deadline:
        data = ser.read(4096)
        if not data:
            continue

        line_buffer.extend(data)
        while b"\n" in line_buffer:
            raw_line, _, line_buffer = line_buffer.partition(b"\n")
            line = raw_line.decode("utf-8", errors="replace").strip()
            if not line:
                continue
            if "AUDIO_CAPTURE_EXPORT_BEGIN" in line:
                saw_begin = True
                continue
            marker = line.find("PCM64:")
            if marker >= 0:
                import base64

                payload = line[marker + len("PCM64:") :]
                pcm_data.extend(base64.b64decode(payload.encode("ascii")))
                continue
            if "AUDIO_CAPTURE_EXPORT_END" in line:
                saw_end = True
                break
        if saw_end:
            break

    if not saw_begin:
        raise RuntimeError("verify_audio_capture_session_end_to_end: missing AUDIO_CAPTURE_EXPORT_BEGIN marker")
    if not saw_end:
        raise RuntimeError("verify_audio_capture_session_end_to_end: missing AUDIO_CAPTURE_EXPORT_END marker")
    return bytes(pcm_data)


def pcm_bytes_to_samples(pcm_data: bytes) -> list[int]:
    if len(pcm_data) % 2 != 0:
        raise RuntimeError("verify_audio_capture_session_end_to_end: pcm length must be even")
    return list(struct.unpack("<" + "h" * (len(pcm_data) // 2), pcm_data))


def frame_rms_values(samples: list[int]) -> list[float]:
    values: list[float] = []
    for offset in range(0, len(samples), FRAME_SAMPLES):
        frame = samples[offset : offset + FRAME_SAMPLES]
        if not frame:
            continue
        energy = sum(sample * sample for sample in frame) / len(frame)
        values.append(math.sqrt(energy))
    return values


def detect_active_runs(rms_values: list[float]) -> list[tuple[int, int]]:
    if not rms_values:
        return []

    peak = max(rms_values)
    threshold = max(peak * 0.12, 120.0)
    min_run_frames = 4

    runs: list[tuple[int, int]] = []
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


def normalize_series(values: list[float]) -> list[float]:
    if not values:
        return []
    mean = sum(values) / len(values)
    centered = [value - mean for value in values]
    max_abs = max(abs(value) for value in centered) or 1.0
    return [value / max_abs for value in centered]


def best_envelope_correlation(source_rms: list[float], recorded_rms: list[float]) -> tuple[float, int]:
    if not source_rms or not recorded_rms:
        return 0.0, 0

    source_norm = normalize_series(source_rms)
    recorded_norm = normalize_series(recorded_rms)

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


def analyze_recording(source_pcm: bytes, recorded_pcm: bytes) -> dict:
    source_samples = pcm_bytes_to_samples(source_pcm)
    recorded_samples = pcm_bytes_to_samples(recorded_pcm)
    source_rms = frame_rms_values(source_samples)
    recorded_rms = frame_rms_values(recorded_samples)
    recorded_peak = max(abs(sample) for sample in recorded_samples) if recorded_samples else 0
    recorded_runs = detect_active_runs(recorded_rms)

    best_corr, best_offset = best_envelope_correlation(source_rms, recorded_rms)

    return {
        "source_frames": len(source_rms),
        "recorded_frames": len(recorded_rms),
        "recorded_peak": recorded_peak,
        "recorded_runs": recorded_runs,
        "best_corr": best_corr,
        "best_offset": best_offset,
        "pass": len(recorded_runs) >= 3 and best_corr >= 0.70 and recorded_peak >= 1000,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", default=115200, type=int)
    parser.add_argument("--boot-timeout-seconds", default=12, type=int)
    parser.add_argument("--export-timeout-seconds", default=45, type=int)
    parser.add_argument("--capture-seconds", default=4.0, type=float)
    parser.add_argument("--playback-delay-seconds", default=0.25, type=float)
    parser.add_argument("--artifacts-dir", required=True)
    args = parser.parse_args()

    artifacts_dir = pathlib.Path(args.artifacts_dir)
    artifacts_dir.mkdir(parents=True, exist_ok=True)
    timestamp = time.strftime("%Y%m%d_%H%M%S")
    source_wav_path = artifacts_dir / f"source_session_pattern_{timestamp}_16k_mono.wav"
    recorded_wav_path = artifacts_dir / f"capture_session_verify_{timestamp}_16k_mono.wav"

    source_pcm = generate_test_pattern(source_wav_path)

    ser = serial.Serial(args.port, args.baud, timeout=0.2)
    try:
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        ser.dtr = False
        ser.rts = True
        time.sleep(0.1)
        ser.rts = False
        time.sleep(0.2)
        ser.reset_input_buffer()

        read_ready(ser, args.boot_timeout_seconds)
        play_wav_after_delay(source_wav_path, args.playback_delay_seconds)
        recorded_pcm = capture_toggle_session(ser, args.capture_seconds, args.export_timeout_seconds)
    finally:
        ser.close()

    write_wav(recorded_wav_path, recorded_pcm)
    analysis = analyze_recording(source_pcm, recorded_pcm)

    print(f"source_wav={source_wav_path}")
    print(f"recorded_wav={recorded_wav_path}")
    print(f"recorded_pcm_bytes={len(recorded_pcm)}")
    print(f"recorded_peak={analysis['recorded_peak']}")
    print(f"recorded_runs={analysis['recorded_runs']}")
    print(f"best_corr={analysis['best_corr']:.4f}")
    print(f"best_offset_frames={analysis['best_offset']}")

    if not analysis["pass"]:
        raise RuntimeError("verify_audio_capture_session_end_to_end: envelope correlation check failed")


if __name__ == "__main__":
    main()
