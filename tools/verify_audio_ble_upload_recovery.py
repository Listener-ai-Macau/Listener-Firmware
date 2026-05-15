import argparse
import asyncio

from ble_audio_regression_common import (
    add_common_capture_args,
    play_and_capture_serial_toggle,
    recover_ble_hid_host,
    restart_windows_bluetooth,
)
from capture_audio_ble_wav import configure_utf8_stdio


def parse_args():
    parser = argparse.ArgumentParser()
    add_common_capture_args(parser, capture_seconds_default=10)
    parser.add_argument("--restart-pan-adapter", action="store_true")
    return parser.parse_args()


async def main_async(args):
    recover_attempts = 0

    restart_result = restart_windows_bluetooth(restart_pan_adapter=args.restart_pan_adapter)
    recover_attempts += 1
    recover_result = recover_ble_hid_host(args.device_name)
    recover_attempts += 1

    summary = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="P4",
        capture_seconds=args.capture_seconds,
        timeout_seconds=args.timeout_seconds if args.timeout_seconds is not None else max(90, int(args.capture_seconds * 3 + 30)),
        reset_before_capture=args.reset_before_capture,
    )

    print("product_case_id=P4", flush=True)
    print("legacy_scenario=R4", flush=True)
    print("scenario=P4", flush=True)
    print(f"result={summary['result']}", flush=True)
    print(f"transport_result={summary['transport_result']}", flush=True)
    print(f"analysis_result={summary['analysis_result']}", flush=True)
    print(f"recover_attempts={recover_attempts}", flush=True)
    print(f"session_id={summary['session_id']}", flush=True)
    print(f"received_packet_count={summary['received_packet_count']}", flush=True)
    print(f"expected_packet_count={summary['expected_packet_count']}", flush=True)
    print(f"missing_packet_count={summary['missing_packet_count']}", flush=True)
    print(f"packet_loss_ratio={summary['packet_loss_ratio']:.4f}", flush=True)
    print(f"received_pcm_bytes={summary['received_pcm_bytes']}", flush=True)
    print(f"duration_seconds={summary['duration_seconds']:.3f}", flush=True)
    print(f"wav_path={summary['wav_path']}", flush=True)
    print(f"serial_log_path={summary['serial_log_path']}", flush=True)
    print(f"best_corr={summary['best_corr']:.4f}", flush=True)
    print(f"recorded_peak={summary['recorded_peak']}", flush=True)
    print(f"active_frame_count={summary['active_frame_count']}", flush=True)
    print(f"restart_stdout={restart_result.stdout.strip()}", flush=True)
    print(f"recover_stdout={recover_result.stdout.strip()}", flush=True)
    print(f"warning_reason={summary['warning_reason']}", flush=True)
    print(f"failure_reason={summary['failure_reason']}", flush=True)

    if summary["result"] == "fail":
        raise RuntimeError("verify_audio_ble_upload_recovery: recovery scenario failed")


def main():
    configure_utf8_stdio()
    args = parse_args()
    asyncio.run(main_async(args))


if __name__ == "__main__":
    main()
