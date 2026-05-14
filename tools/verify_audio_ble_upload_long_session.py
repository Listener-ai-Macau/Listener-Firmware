import argparse
import asyncio

from ble_audio_regression_common import (
    add_common_capture_args,
    play_and_capture_serial_toggle,
)
from capture_audio_ble_wav import configure_utf8_stdio


def parse_args():
    parser = argparse.ArgumentParser()
    add_common_capture_args(parser, capture_seconds_default=60)
    return parser.parse_args()


async def main_async(args):
    summary = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="R3",
        capture_seconds=args.capture_seconds,
        timeout_seconds=max(240, int(args.capture_seconds * 3 + 30)),
        reset_before_capture=args.reset_before_capture,
    )

    print("scenario=R3", flush=True)
    print(f"result={summary['result']}", flush=True)
    print(f"transport_result={summary['transport_result']}", flush=True)
    print(f"analysis_result={summary['analysis_result']}", flush=True)
    print(f"session_id={summary['session_id']}", flush=True)
    print(f"received_packet_count={summary['received_packet_count']}", flush=True)
    print(f"expected_packet_count={summary['expected_packet_count']}", flush=True)
    print(f"missing_packet_count={summary['missing_packet_count']}", flush=True)
    print(f"packet_loss_ratio={summary['packet_loss_ratio']:.4f}", flush=True)
    print(f"pcm_bytes={summary['pcm_bytes']}", flush=True)
    print(f"received_pcm_bytes={summary['received_pcm_bytes']}", flush=True)
    print(f"duration_seconds={summary['duration_seconds']:.3f}", flush=True)
    print(f"duration_ok={1 if summary['duration_ok'] else 0}", flush=True)
    print(f"wav_path={summary['wav_path']}", flush=True)
    print(f"serial_log_path={summary['serial_log_path']}", flush=True)
    print(f"best_corr={summary['best_corr']:.4f}", flush=True)
    print(f"recorded_peak={summary['recorded_peak']}", flush=True)
    print(f"active_frame_count={summary['active_frame_count']}", flush=True)
    print(f"warning_reason={summary['warning_reason']}", flush=True)
    print(f"failure_reason={summary['failure_reason']}", flush=True)

    if summary["result"] == "fail":
        raise RuntimeError("verify_audio_ble_upload_long_session: long session transport validation failed")


def main():
    configure_utf8_stdio()
    args = parse_args()
    asyncio.run(main_async(args))


if __name__ == "__main__":
    main()
