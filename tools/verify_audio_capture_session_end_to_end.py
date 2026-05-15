import argparse
import asyncio
import pathlib

from ble_audio_regression_common import (
    analyze_recording,
    detect_active_runs,
    generate_source_wav,
    read_wav_frames,
    validate_transport_summary,
    compute_envelope,
)
from capture_audio_ble_wav import configure_utf8_stdio, run_capture_with_args


def parse_args():
    parser = argparse.ArgumentParser(
        description=(
            "Verify KEY1/toggle session WAV capture using the current BLE session path. "
            "This replaces the retired serial ACAP/PCM64 export verification."
        )
    )
    parser.add_argument("--port", required=True)
    parser.add_argument("--device-name", default="Listener Keyboard")
    parser.add_argument("--boot-timeout-seconds", default=15, type=int)
    parser.add_argument("--ble-connect-timeout-seconds", default=12, type=int)
    parser.add_argument("--notify-ready-timeout-seconds", default=20, type=int)
    parser.add_argument("--capture-seconds", default=4, type=int)
    parser.add_argument("--timeout-seconds", default=60, type=int)
    parser.add_argument("--artifacts-dir", required=True)
    parser.add_argument("--trigger-mode", choices=["serial-toggle", "physical-key"], default="serial-toggle")
    parser.add_argument("--no-reset-before-capture", action="store_false", dest="reset_before_capture")
    parser.set_defaults(reset_before_capture=True)
    return parser.parse_args()


def build_capture_args(args, output_dir: pathlib.Path):
    serial_log_path = output_dir / "verify_audio_capture_session_latest.log"
    return argparse.Namespace(
        port=args.port,
        device_name=args.device_name,
        capture_seconds=args.capture_seconds,
        output_dir=str(output_dir),
        serial_log_path=str(serial_log_path),
        timeout_seconds=args.timeout_seconds,
        boot_timeout_seconds=args.boot_timeout_seconds,
        ble_connect_timeout_seconds=args.ble_connect_timeout_seconds,
        notify_ready_timeout_seconds=args.notify_ready_timeout_seconds,
        trigger_mode=args.trigger_mode,
        max_sessions=1,
        reset_before_capture=args.reset_before_capture,
    )


async def main_async(args) -> None:
    artifacts_dir = pathlib.Path(args.artifacts_dir)
    artifacts_dir.mkdir(parents=True, exist_ok=True)

    capture_args = build_capture_args(args, artifacts_dir)
    summaries = await run_capture_with_args(capture_args)
    summary = summaries[0]
    recorded_wav_path = pathlib.Path(summary["wav_path"])
    transport = validate_transport_summary(summary, capture_seconds=args.capture_seconds)

    source_wav_path = None
    analysis = None
    physical_key_analysis = None

    if args.trigger_mode == "serial-toggle":
        source_wav_path = artifacts_dir / "source_ble_session_reference_latest_16k_mono.wav"
        generate_source_wav(source_wav_path, args.capture_seconds)
        analysis = analyze_recording(source_wav_path, recorded_wav_path)
    else:
        recorded_frames = read_wav_frames(recorded_wav_path)
        recorded_env = compute_envelope(recorded_frames)
        recorded_peak = max(abs(value) for value in recorded_frames) if recorded_frames else 0
        recorded_runs = detect_active_runs(recorded_env)
        active_frame_count = sum(end - start for start, end in recorded_runs)
        physical_key_analysis = {
            "recorded_peak": recorded_peak,
            "recorded_runs": recorded_runs,
            "active_frame_count": active_frame_count,
            "analysis_pass": recorded_peak >= 500 and active_frame_count >= 5,
            "analysis_failure_reason": (
                "recorded_peak_below_threshold"
                if recorded_peak < 500
                else "active_frame_count_below_threshold"
                if active_frame_count < 5
                else ""
            ),
        }

    if source_wav_path is not None:
        print(f"source_wav={source_wav_path}")
    print(f"recorded_wav={recorded_wav_path}")
    print(f"serial_log_path={summary['serial_log_path']}")
    print(f"trigger_mode={args.trigger_mode}")
    print(f"received_packet_count={transport['received_packet_count']}")
    print(f"expected_packet_count={transport['expected_packet_count']}")
    print(f"missing_packet_count={transport['missing_packet_count']}")
    print(f"received_pcm_bytes={transport['received_pcm_bytes']}")
    print(f"duration_seconds={summary['duration_seconds']:.3f}")
    print(f"packet_loss_ratio={transport['packet_loss_ratio']:.4f}")

    if transport["transport_result"] == "fail":
        raise RuntimeError(
            "verify_audio_capture_session_end_to_end: BLE session transport validation failed "
            f"reason={transport['transport_failure_reason']} "
            f"duration_seconds={summary['duration_seconds']:.3f} "
            f"packet_loss_ratio={transport['packet_loss_ratio']:.4f}"
        )

    if args.trigger_mode == "serial-toggle":
        print(f"best_corr={analysis['best_corr']:.4f}")
        print(f"best_offset_frames={analysis['best_offset_frames']}")
        print(f"recorded_peak={analysis['recorded_peak']}")
        print(f"recorded_runs={analysis['recorded_runs']}")
        print(f"active_frame_count={analysis['active_frame_count']}")

        if not analysis["analysis_pass"]:
            raise RuntimeError(
                "verify_audio_capture_session_end_to_end: BLE session WAV analysis failed "
                f"reason={analysis['analysis_failure_reason']} "
                f"corr={analysis['best_corr']:.4f} "
                f"peak={analysis['recorded_peak']} "
                f"active_frames={analysis['active_frame_count']}"
            )
    else:
        print(f"recorded_peak={physical_key_analysis['recorded_peak']}")
        print(f"recorded_runs={physical_key_analysis['recorded_runs']}")
        print(f"active_frame_count={physical_key_analysis['active_frame_count']}")

        if not physical_key_analysis["analysis_pass"]:
            raise RuntimeError(
                "verify_audio_capture_session_end_to_end: physical-key WAV activity check failed "
                f"reason={physical_key_analysis['analysis_failure_reason']} "
                f"peak={physical_key_analysis['recorded_peak']} "
                f"active_frames={physical_key_analysis['active_frame_count']}"
            )


def main() -> None:
    configure_utf8_stdio()
    args = parse_args()
    asyncio.run(main_async(args))


if __name__ == "__main__":
    main()
