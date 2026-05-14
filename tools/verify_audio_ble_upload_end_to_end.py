import argparse
import asyncio
import pathlib
from ble_audio_regression_common import (
    add_common_capture_args,
    play_and_capture_serial_toggle,
)


def parse_args():
    parser = argparse.ArgumentParser()
    add_common_capture_args(parser, capture_seconds_default=12)
    return parser.parse_args()


async def main_async(args):
    summary = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="R1",
        capture_seconds=args.capture_seconds,
    )
    if not summary["analysis_pass"]:
        raise RuntimeError(
            "verify_audio_ble_upload_end_to_end: failed "
            f"corr={summary['best_corr']:.4f} "
            f"peak={summary['recorded_peak']} "
            f"runs={summary['recorded_runs']} "
            f"active_frames={summary['active_frame_count']}"
        )
    print(f"source_wav={summary['source_wav']}")
    print(f"recorded_wav={summary['wav_path']}")
    print(f"best_corr={summary['best_corr']:.4f}")
    print(f"best_offset_frames={summary['best_offset_frames']}")
    print(f"recorded_peak={summary['recorded_peak']}")
    print(f"recorded_runs={summary['recorded_runs']}")
    print(f"active_frame_count={summary['active_frame_count']}")


def main():
    args = parse_args()
    asyncio.run(main_async(args))


if __name__ == "__main__":
    main()
