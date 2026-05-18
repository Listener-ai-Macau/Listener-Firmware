import argparse
import asyncio
import base64
import json
import math
import pathlib
import struct
import subprocess
import wave
import winsound

from ble_audio_regression_common import (
    PCM_CHANNELS,
    PCM_SAMPLE_RATE,
    PCM_WIDTH_BYTES,
    analyze_recording,
    capture_sessions,
    make_capture_args,
    read_wav_frames,
    validate_transport_summary,
)
from capture_audio_ble_wav import configure_utf8_stdio


DEFAULT_TEXT = "明天下午两点提醒我检查蓝牙音频丢包率。"


def parse_args():
    parser = argparse.ArgumentParser(
        description="Play a generated TTS sentence through the PC speaker, capture it through BLE audio, and emit a WAV summary."
    )
    parser.add_argument("--port", required=True)
    parser.add_argument("--device-name", default="listener")
    parser.add_argument("--text", default=DEFAULT_TEXT)
    parser.add_argument("--capture-seconds", type=int, default=6)
    parser.add_argument("--timeout-seconds", type=int, default=90)
    parser.add_argument("--playback-loop-seconds", type=int, default=45)
    parser.add_argument("--artifacts-dir", required=True)
    parser.add_argument("--boot-timeout-seconds", type=int, default=15)
    parser.add_argument("--ble-connect-timeout-seconds", type=int, default=15)
    parser.add_argument("--notify-ready-timeout-seconds", type=int, default=45)
    parser.add_argument("--no-reset-before-capture", action="store_false", dest="reset_before_capture")
    parser.set_defaults(reset_before_capture=True)
    return parser.parse_args()


def powershell_json_literal(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def generate_tts_wav(path: pathlib.Path, text: str) -> str:
    path.parent.mkdir(parents=True, exist_ok=True)
    script = f"""
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Speech
$path = {powershell_json_literal(str(path))}
$text = {powershell_json_literal(text)}
$synth = [System.Speech.Synthesis.SpeechSynthesizer]::new()
try {{
    $zhVoice = $synth.GetInstalledVoices() |
        Where-Object {{ $_.VoiceInfo.Culture.Name -like "zh-*" }} |
        Select-Object -First 1
    if ($null -ne $zhVoice) {{
        $synth.SelectVoice($zhVoice.VoiceInfo.Name)
    }}
    $format = [System.Speech.AudioFormat.SpeechAudioFormatInfo]::new(
        16000,
        [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,
        [System.Speech.AudioFormat.AudioChannel]::Mono
    )
    $synth.SetOutputToWaveFile($path, $format)
    $synth.Speak($text) | Out-Null
    Write-Output $synth.Voice.Name
}} finally {{
    $synth.Dispose()
}}
"""
    completed = subprocess.run(
        [
            "powershell",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-EncodedCommand",
            base64.b64encode(script.encode("utf-16le")).decode("ascii"),
        ],
        check=True,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
        text=True,
    )
    voice_lines = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
    return voice_lines[-1] if voice_lines else ""


def make_loop_wav(source_wav: pathlib.Path, loop_wav: pathlib.Path, loop_seconds: int) -> None:
    frames = read_wav_frames(source_wav)
    silence = [0] * int(PCM_SAMPLE_RATE * 0.25)
    cycle = frames + silence
    target_frames = max(len(cycle), int(PCM_SAMPLE_RATE * loop_seconds))
    repeat_count = int(math.ceil(target_frames / len(cycle)))
    loop_frames = (cycle * repeat_count)[:target_frames]
    loop_wav.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(loop_wav), "wb") as wav_file:
        wav_file.setnchannels(PCM_CHANNELS)
        wav_file.setsampwidth(PCM_WIDTH_BYTES)
        wav_file.setframerate(PCM_SAMPLE_RATE)
        wav_file.writeframes(struct.pack("<" + "h" * len(loop_frames), *loop_frames))


async def main_async(args) -> None:
    artifacts_dir = pathlib.Path(args.artifacts_dir)
    artifacts_dir.mkdir(parents=True, exist_ok=True)

    source_wav = artifacts_dir / "source_tts_16k_mono.wav"
    playback_wav = artifacts_dir / "source_tts_loop_16k_mono.wav"
    serial_log_path = artifacts_dir / "ble_tts_capture_latest.log"
    voice_name = generate_tts_wav(source_wav, args.text)
    make_loop_wav(source_wav, playback_wav, args.playback_loop_seconds)

    capture_args = make_capture_args(
        port=args.port,
        device_name=args.device_name,
        capture_seconds=args.capture_seconds,
        capture_seconds_per_session=[args.capture_seconds],
        timeout_seconds=args.timeout_seconds,
        reset_before_capture=args.reset_before_capture,
        output_dir=artifacts_dir,
        serial_log_path=serial_log_path,
        boot_timeout_seconds=args.boot_timeout_seconds,
        ble_connect_timeout_seconds=args.ble_connect_timeout_seconds,
        notify_ready_timeout_seconds=args.notify_ready_timeout_seconds,
    )

    winsound.PlaySound(str(playback_wav), winsound.SND_FILENAME | winsound.SND_ASYNC)
    try:
        summaries = await capture_sessions(capture_args)
    finally:
        winsound.PlaySound(None, winsound.SND_PURGE)

    if not summaries:
        raise RuntimeError("verify_ble_tts_asr_smoke: no BLE session captured")

    summary = dict(summaries[-1])
    recorded_wav = pathlib.Path(summary["wav_path"])
    transport = validate_transport_summary(summary, capture_seconds=args.capture_seconds)
    analysis = analyze_recording(playback_wav, recorded_wav)
    status = "PASS"
    reason = ""
    if transport["transport_result"] == "fail":
        status = "FAIL"
        reason = transport["transport_failure_reason"] or "transport_validation_failed"
    elif analysis["analysis_result"] == "fail":
        status = "FAIL"
        reason = analysis["analysis_failure_reason"] or "recording_analysis_failed"
    elif transport["transport_result"] == "warning":
        status = "WARNING"
        reason = transport["transport_warning_reason"] or "transport_warning"

    report = {
        "status": status,
        "reason": reason,
        "text": args.text,
        "voice": voice_name,
        "source_wav": str(source_wav),
        "playback_wav": str(playback_wav),
        "recorded_wav": str(recorded_wav),
        "serial_log_path": str(serial_log_path),
        "capture_seconds": args.capture_seconds,
        "reset_before_capture": args.reset_before_capture,
        "received_packet_count": transport["received_packet_count"],
        "expected_packet_count": transport["expected_packet_count"],
        "missing_packet_count": transport["missing_packet_count"],
        "packet_loss_ratio": transport["packet_loss_ratio"],
        "received_pcm_bytes": transport["received_pcm_bytes"],
        "duration_seconds": float(summary["duration_seconds"]),
        "best_corr": analysis["best_corr"],
        "recorded_peak": analysis["recorded_peak"],
        "active_frame_count": analysis["active_frame_count"],
    }

    report_path = artifacts_dir / "ble_tts_capture_result.json"
    report_path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    print(json.dumps(report, ensure_ascii=False, indent=2), flush=True)
    print(f"ble_tts_capture_result_json={report_path}", flush=True)
    if status == "FAIL":
        raise RuntimeError(f"verify_ble_tts_asr_smoke: failed reason={reason}")


def main() -> None:
    configure_utf8_stdio()
    args = parse_args()
    asyncio.run(main_async(args))


if __name__ == "__main__":
    main()
