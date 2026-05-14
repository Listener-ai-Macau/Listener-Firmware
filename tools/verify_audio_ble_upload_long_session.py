import argparse
import asyncio

from ble_audio_regression_common import (
    add_common_capture_args,
    play_and_capture_serial_toggle,
)


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
    )

    print("scenario=R3", flush=True)
    print(f"result={summary['result']}", flush=True)
    print(f"session_id={summary['session_id']}", flush=True)
    print(f"chunk_count={summary['chunk_count']}", flush=True)
    print(f"expected_chunk_count={summary['expected_chunk_count']}", flush=True)
    print(f"missing_chunk_count={summary['missing_chunk_count']}", flush=True)
    print(f"pcm_bytes={summary['pcm_bytes']}", flush=True)
    print(f"duration_seconds={summary['duration_seconds']:.3f}", flush=True)
    print(f"duration_ok={1 if summary['duration_ok'] else 0}", flush=True)
    print(f"wav_path={summary['wav_path']}", flush=True)
    print(f"serial_log_path={summary['serial_log_path']}", flush=True)
    print(f"best_corr={summary['best_corr']:.4f}", flush=True)
    print(f"recorded_peak={summary['recorded_peak']}", flush=True)
    print(f"active_frame_count={summary['active_frame_count']}", flush=True)
    print(f"failure_reason={summary['failure_reason']}", flush=True)

    if summary["result"] != "pass":
        raise RuntimeError("verify_audio_ble_upload_long_session: long session transport validation failed")


def main():
    args = parse_args()
    asyncio.run(main_async(args))


if __name__ == "__main__":
    main()
