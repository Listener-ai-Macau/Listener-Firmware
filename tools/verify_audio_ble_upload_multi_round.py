import argparse
import asyncio
from ble_audio_regression_common import (
    add_common_capture_args,
    play_and_capture_serial_toggle,
)


def parse_args():
    parser = argparse.ArgumentParser()
    add_common_capture_args(parser, capture_seconds_default=10)
    parser.add_argument("--round-count", type=int, default=10)
    return parser.parse_args()


async def run_round(args, round_index: int):
    summary = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="R2",
        capture_seconds=args.capture_seconds,
        source_label=f"round{round_index}",
    )
    summary.update(
        {
            "round_index": round_index,
            "scenario": "R2",
            "failure_reason": "" if summary["result"] == "pass" else "round_transport_validation_failed",
        }
    )
    return summary


async def main_async(args):
    pass_count = 0
    fail_count = 0
    failed_rounds = []
    round_summaries = []

    for round_index in range(1, args.round_count + 1):
        summary = await run_round(args, round_index)
        round_summaries.append(summary)
        if summary["result"] == "pass":
            pass_count += 1
        else:
            fail_count += 1
            failed_rounds.append(round_index)

        print(f"scenario=R2", flush=True)
        print(f"round_index={round_index}", flush=True)
        print(f"round_count={args.round_count}", flush=True)
        print(f"result={summary['result']}", flush=True)
        print(f"session_id={summary['session_id']}", flush=True)
        print(f"chunk_count={summary['chunk_count']}", flush=True)
        print(f"expected_chunk_count={summary['expected_chunk_count']}", flush=True)
        print(f"missing_chunk_count={summary['missing_chunk_count']}", flush=True)
        print(f"pcm_bytes={summary['pcm_bytes']}", flush=True)
        print(f"duration_seconds={summary['duration_seconds']:.3f}", flush=True)
        print(f"duration_ok={1 if summary['duration_ok'] else 0}", flush=True)
        print(f"wav_path={summary['wav_path']}", flush=True)
        print(f"best_corr={summary['best_corr']:.4f}", flush=True)
        print(f"recorded_peak={summary['recorded_peak']}", flush=True)
        print(f"active_frame_count={summary['active_frame_count']}", flush=True)
        print(f"pass_count={pass_count}", flush=True)
        print(f"fail_count={fail_count}", flush=True)

    print("scenario=R2", flush=True)
    print(f"result={'pass' if fail_count == 0 else 'fail'}", flush=True)
    print(f"round_count={args.round_count}", flush=True)
    print(f"pass_count={pass_count}", flush=True)
    print(f"fail_count={fail_count}", flush=True)
    print(
        "failed_rounds=" + (",".join(str(v) for v in failed_rounds) if failed_rounds else "<none>"),
        flush=True,
    )

    if fail_count != 0:
        raise RuntimeError(
            f"verify_audio_ble_upload_multi_round: {fail_count} round(s) failed out of {args.round_count}"
        )


def main():
    args = parse_args()
    asyncio.run(main_async(args))


if __name__ == "__main__":
    main()
