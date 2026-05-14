import argparse
import asyncio
from ble_audio_regression_common import (
    add_common_capture_args,
    play_and_capture_serial_toggle,
)
from capture_audio_ble_wav import configure_utf8_stdio


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
        reset_before_capture=args.reset_before_capture,
    )
    summary.update(
        {
            "round_index": round_index,
            "scenario": "R2",
        }
    )
    return summary


async def main_async(args):
    pass_count = 0
    warning_count = 0
    fail_count = 0
    failed_rounds = []
    warning_rounds = []
    round_summaries = []

    for round_index in range(1, args.round_count + 1):
        summary = await run_round(args, round_index)
        round_summaries.append(summary)
        if summary["result"] == "pass":
            pass_count += 1
        elif summary["result"] == "warning":
            warning_count += 1
            warning_rounds.append(round_index)
        else:
            fail_count += 1
            failed_rounds.append(round_index)

        print(f"scenario=R2", flush=True)
        print(f"round_index={round_index}", flush=True)
        print(f"round_count={args.round_count}", flush=True)
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
        print(f"best_corr={summary['best_corr']:.4f}", flush=True)
        print(f"recorded_peak={summary['recorded_peak']}", flush=True)
        print(f"active_frame_count={summary['active_frame_count']}", flush=True)
        print(f"pass_count={pass_count}", flush=True)
        print(f"warning_count={warning_count}", flush=True)
        print(f"fail_count={fail_count}", flush=True)
        print(f"warning_reason={summary['warning_reason']}", flush=True)
        print(f"failure_reason={summary['failure_reason']}", flush=True)

    print("scenario=R2", flush=True)
    overall_result = "fail" if fail_count > 0 else ("warning" if warning_count > 0 else "pass")
    print(f"result={overall_result}", flush=True)
    print(f"round_count={args.round_count}", flush=True)
    print(f"pass_count={pass_count}", flush=True)
    print(f"warning_count={warning_count}", flush=True)
    print(f"fail_count={fail_count}", flush=True)
    print(
        "warning_rounds=" + (",".join(str(v) for v in warning_rounds) if warning_rounds else "<none>"),
        flush=True,
    )
    print(
        "failed_rounds=" + (",".join(str(v) for v in failed_rounds) if failed_rounds else "<none>"),
        flush=True,
    )

    if fail_count != 0:
        raise RuntimeError(
            f"verify_audio_ble_upload_multi_round: {fail_count} round(s) failed out of {args.round_count}"
        )


def main():
    configure_utf8_stdio()
    args = parse_args()
    asyncio.run(main_async(args))


if __name__ == "__main__":
    main()
