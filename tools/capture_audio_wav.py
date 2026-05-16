import argparse
import asyncio
import pathlib

from ble_audio_regression_common import DEFAULT_SERIAL_LOG_PATH, make_capture_args
from capture_audio_ble_wav import configure_utf8_stdio, run_capture_with_args


DEFAULT_OUTPUT_ROOT = pathlib.Path("tests") / "artifacts" / "audio"


def parse_args():
    parser = argparse.ArgumentParser(
        description=(
            "Capture audio to WAV using the current BLE session path. "
            "The legacy fixed ACAP/PCM64 export path has been retired."
        )
    )
    parser.add_argument("--port", required=True)
    parser.add_argument("--device-name", default="listener")
    parser.add_argument("--duration-seconds", type=int)
    parser.add_argument("--capture-seconds", type=int)
    parser.add_argument("--mode", choices=["fixed", "toggle-session"], default="fixed")
    parser.add_argument("--baud", default=115200, type=int)
    parser.add_argument("--boot-timeout-seconds", default=15, type=int)
    parser.add_argument("--ble-connect-timeout-seconds", default=12, type=int)
    parser.add_argument("--notify-ready-timeout-seconds", default=20, type=int)
    parser.add_argument("--timeout-seconds", default=60, type=int)
    parser.add_argument("--wav-path", required=True)
    parser.add_argument("--serial-log-path", default=None)
    parser.add_argument("--trigger-mode", choices=["serial-toggle", "physical-key"], default=None)
    parser.add_argument("--no-reset-before-capture", action="store_false", dest="reset_before_capture")
    parser.set_defaults(reset_before_capture=True)
    return parser.parse_args()


def resolve_capture_args(args) -> argparse.Namespace:
    wav_path = pathlib.Path(args.wav_path)
    wav_path.parent.mkdir(parents=True, exist_ok=True)

    if args.mode == "fixed":
        if args.duration_seconds is None:
            raise RuntimeError("capture_audio_wav: --duration-seconds is required in fixed mode")
        capture_seconds = args.duration_seconds
        trigger_mode = args.trigger_mode or "serial-toggle"
    else:
        if args.capture_seconds is None:
            raise RuntimeError("capture_audio_wav: --capture-seconds is required in toggle-session mode")
        capture_seconds = args.capture_seconds
        trigger_mode = args.trigger_mode or "serial-toggle"

    serial_log_path = args.serial_log_path
    if serial_log_path is None:
        serial_log_path = str(DEFAULT_SERIAL_LOG_PATH)

    return make_capture_args(
        port=args.port,
        device_name=args.device_name,
        capture_seconds=capture_seconds,
        trigger_mode=trigger_mode,
        max_sessions=1,
        timeout_seconds=args.timeout_seconds,
        output_dir=wav_path.parent,
        serial_log_path=pathlib.Path(serial_log_path),
        boot_timeout_seconds=args.boot_timeout_seconds,
        ble_connect_timeout_seconds=args.ble_connect_timeout_seconds,
        notify_ready_timeout_seconds=args.notify_ready_timeout_seconds,
        reset_before_capture=args.reset_before_capture,
    )


async def main_async(args) -> None:
    capture_args = resolve_capture_args(args)
    summaries = await run_capture_with_args(capture_args)
    summary = summaries[0]

    output_dir = pathlib.Path(capture_args.output_dir)
    latest_wav_path = output_dir / "capture_ble_latest_16k_mono.wav"
    requested_wav_path = pathlib.Path(args.wav_path)
    if latest_wav_path.resolve() != requested_wav_path.resolve():
        requested_wav_path.write_bytes(latest_wav_path.read_bytes())

    print(f"wav_path={requested_wav_path}")
    print(f"session_id={summary['session_id']}")
    print(f"received_packet_count={summary['received_packet_count']}")
    print(f"expected_packet_count={summary['expected_packet_count']}")
    print(f"missing_packet_count={summary['missing_packet_count']}")
    print(f"received_pcm_bytes={summary['received_pcm_bytes']}")
    print(f"duration_seconds={summary['duration_seconds']:.3f}")
    if summary.get("serial_log_path"):
        print(f"serial_log_path={summary['serial_log_path']}")


def main() -> None:
    configure_utf8_stdio()
    args = parse_args()
    asyncio.run(main_async(args))


if __name__ == "__main__":
    main()
