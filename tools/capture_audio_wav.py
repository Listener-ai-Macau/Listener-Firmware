import argparse
import base64
import pathlib
import struct
import time

import serial


SAMPLE_RATE = 16000
BITS_PER_SAMPLE = 16
CHANNELS = 1
FRAME_MS = 20
FRAME_BYTES = (SAMPLE_RATE * FRAME_MS // 1000) * (BITS_PER_SAMPLE // 8) * CHANNELS


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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--duration-seconds", type=int)
    parser.add_argument("--capture-seconds", type=int)
    parser.add_argument("--mode", choices=["fixed", "toggle-session"], default="fixed")
    parser.add_argument("--baud", default=115200, type=int)
    parser.add_argument("--boot-timeout-seconds", default=12, type=int)
    parser.add_argument("--export-timeout-seconds", default=45, type=int)
    parser.add_argument("--wav-path", required=True)
    args = parser.parse_args()

    wav_path = pathlib.Path(args.wav_path)
    if args.mode == "fixed":
        if args.duration_seconds is None:
            raise RuntimeError("capture_audio_wav: --duration-seconds is required in fixed mode")
        expected_pcm_bytes = args.duration_seconds * SAMPLE_RATE * (BITS_PER_SAMPLE // 8) * CHANNELS
        start_command = f"~ACAP:{args.duration_seconds}\n".encode("ascii")
        stop_command = None
        active_capture_seconds = args.duration_seconds
    else:
        if args.capture_seconds is None:
            raise RuntimeError("capture_audio_wav: --capture-seconds is required in toggle-session mode")
        expected_pcm_bytes = args.capture_seconds * SAMPLE_RATE * (BITS_PER_SAMPLE // 8) * CHANNELS
        max_pcm_bytes = expected_pcm_bytes + FRAME_BYTES * 8
        start_command = b"~VREC:TOGGLE\n"
        stop_command = b"~VREC:TOGGLE\n"
        active_capture_seconds = args.capture_seconds

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

        boot_deadline = time.time() + args.boot_timeout_seconds
        boot_buffer = bytearray()
        while time.time() < boot_deadline:
            data = ser.read(4096)
            if data:
                boot_buffer.extend(data)
                boot_text = boot_buffer.decode("utf-8", errors="replace")
                if "audio_capture: codec init ok" in boot_text and "ble_hid: USB SERIAL INPUT READY" in boot_text:
                    break
        else:
            raise RuntimeError("capture_audio_wav: device did not become ready before timeout")

        ser.write(start_command)
        ser.flush()

        if stop_command is not None:
            time.sleep(active_capture_seconds)
            ser.write(stop_command)
            ser.flush()

        export_deadline = time.time() + args.export_timeout_seconds
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
                pcm_marker = line.find("PCM64:")
                if pcm_marker >= 0:
                    payload = line[pcm_marker + len("PCM64:") :]
                    pcm_data.extend(base64.b64decode(payload.encode("ascii")))
                    continue
                if "AUDIO_CAPTURE_EXPORT_END" in line:
                    saw_end = True
                    break
            if saw_end:
                break

        if not saw_begin:
            raise RuntimeError("capture_audio_wav: missing AUDIO_CAPTURE_EXPORT_BEGIN marker")
        if not saw_end:
            raise RuntimeError("capture_audio_wav: missing AUDIO_CAPTURE_EXPORT_END marker")
        if args.mode == "fixed":
            if len(pcm_data) != expected_pcm_bytes:
                raise RuntimeError(
                    f"capture_audio_wav: pcm size mismatch expected={expected_pcm_bytes} actual={len(pcm_data)}"
                )
        else:
            if len(pcm_data) < expected_pcm_bytes:
                raise RuntimeError(
                    f"capture_audio_wav: pcm size too short expected_at_least={expected_pcm_bytes} actual={len(pcm_data)}"
                )
            if len(pcm_data) > max_pcm_bytes:
                raise RuntimeError(
                    f"capture_audio_wav: pcm size too long max_expected={max_pcm_bytes} actual={len(pcm_data)}"
                )

        wav_path.write_bytes(make_wav_header(len(pcm_data)) + pcm_data)
        print(f"wav_path={wav_path}")
        print(f"pcm_bytes={len(pcm_data)}")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
