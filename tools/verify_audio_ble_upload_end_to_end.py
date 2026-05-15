import argparse
import asyncio
import pathlib
from ble_audio_regression_common import (
    add_common_capture_args,
    play_and_capture_serial_toggle,
)
from capture_audio_ble_wav import configure_utf8_stdio


def parse_args():
    parser = argparse.ArgumentParser()
    add_common_capture_args(parser, capture_seconds_default=12)
    return parser.parse_args()


async def main_async(args):
    summary = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="P1",
        capture_seconds=args.capture_seconds,
        timeout_seconds=args.timeout_seconds,
        reset_before_capture=args.reset_before_capture,
    )
    print("product_case_id=P1", flush=True)
    print("legacy_scenario=R1", flush=True)
    print("scenario=P1", flush=True)
    print(f"result={summary['result']}", flush=True)
    print(f"transport_result={summary['transport_result']}", flush=True)
    print(f"analysis_result={summary['analysis_result']}", flush=True)
    print(f"session_id={summary['session_id']}", flush=True)
    print(f"received_packet_count={summary['received_packet_count']}", flush=True)
    print(f"expected_packet_count={summary['expected_packet_count']}", flush=True)
    print(f"missing_packet_count={summary['missing_packet_count']}", flush=True)
    print(f"packet_loss_ratio={summary['packet_loss_ratio']:.4f}", flush=True)
    print(f"received_pcm_bytes={summary['received_pcm_bytes']}", flush=True)
    print(f"duration_seconds={summary['duration_seconds']:.3f}", flush=True)
    print(f"source_wav={summary['source_wav']}")
    print(f"recorded_wav={summary['wav_path']}")
    print(f"best_corr={summary['best_corr']:.4f}")
    print(f"best_offset_frames={summary['best_offset_frames']}")
    print(f"recorded_peak={summary['recorded_peak']}")
    print(f"recorded_runs={summary['recorded_runs']}")
    print(f"active_frame_count={summary['active_frame_count']}")
    print(f"warning_reason={summary['warning_reason']}", flush=True)
    print(f"failure_reason={summary['failure_reason']}", flush=True)

    if summary["result"] == "fail":
        raise RuntimeError(
            "verify_audio_ble_upload_end_to_end: failed "
            f"result={summary['result']} "
            f"transport={summary['transport_result']} "
            f"analysis={summary['analysis_result']} "
            f"reason={summary['failure_reason']} "
            f"corr={summary['best_corr']:.4f} "
            f"peak={summary['recorded_peak']} "
            f"active_frames={summary['active_frame_count']}"
        )


def main():
    configure_utf8_stdio()
    args = parse_args()
    asyncio.run(main_async(args))


if __name__ == "__main__":
    main()
