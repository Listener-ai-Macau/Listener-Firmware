import argparse
import asyncio
import json
import pathlib
import random
import subprocess
import sys
import time

import serial

from ble_audio_regression_common import (
    play_and_capture_serial_toggle_after_cancel_probe,
    play_and_capture_serial_toggle,
    play_and_capture_serial_toggle_multi_session,
    recover_ble_hid_host,
    restart_windows_bluetooth,
    analyze_recording,
    capture_sessions,
    make_capture_args,
    read_wav_frames,
    compute_envelope,
    detect_active_runs,
    validate_transport_summary,
    get_paired_device_address_hex,
    open_serial_with_retry,
    send_toggle,
)
from capture_audio_ble_wav import configure_utf8_stdio, run_capture_with_args


CASE_ORDER = ("A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", "A9", "A10", "H1")
MANUAL_OR_EXTERNAL_CASES = {
    "H2": "requires controlled RF/distance/interference setup",
    "H3": "requires backend ASR integration endpoint",
}
MATRIX_ARTIFACT_DIR = pathlib.Path("tests") / "artifacts" / "ble_product_matrix"
A3_RECONNECT_MIN_PRE_START_DELAY_SECONDS = 12.0
COMPACT_STDOUT_PREFIXES = (
    "execution_profile=",
    "preflight_recover_mode=",
    "random_seed=",
    "case_execution_order=",
    "case_start=",
    "case_id=",
    "case_result=",
    "case_reason=",
    "matrix_item=",
    "matrix_summary=",
    "matrix_total=",
    "matrix_failed=",
    "matrix_warning=",
    "matrix_skipped=",
    "matrix_result_json=",
    "matrix_summary_log=",
    "matrix_failed_cases=",
    "matrix_warning_cases=",
    "matrix_error=",
    "interrupted=",
)


class MatrixStdoutFilter:
    def __init__(self, original, log_file, *, verbose: bool):
        self.original = original
        self.log_file = log_file
        self.verbose = verbose
        self.buffer = ""

    def write(self, text: str) -> int:
        self.log_file.write(text)
        self.buffer += text
        while "\n" in self.buffer:
            line, self.buffer = self.buffer.split("\n", 1)
            complete = line + "\n"
            if self.verbose or self.should_forward(line.rstrip("\r")):
                self.original.write(complete)
        return len(text)

    def flush(self) -> None:
        self.log_file.flush()
        self.original.flush()

    def should_forward(self, line: str) -> bool:
        return any(line.startswith(prefix) for prefix in COMPACT_STDOUT_PREFIXES)


def parse_case_list(raw: str) -> list[str]:
    if raw.strip().lower() == "auto":
        return list(CASE_ORDER)
    cases = []
    for item in raw.split(","):
        case_id = item.strip().upper()
        if not case_id:
            continue
        cases.append(case_id)
    return cases


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run the BLE audio product-surface test matrix."
    )
    parser.add_argument("--port", required=True)
    parser.add_argument("--device-name", default="listener")
    parser.add_argument("--cases", default="auto")
    parser.add_argument("--capture-seconds", type=int, default=5)
    parser.add_argument("--long-capture-seconds", type=int, default=30)
    parser.add_argument("--round-count", type=int, default=3)
    parser.add_argument("--idle-seconds", type=int, default=30)
    parser.add_argument("--short-capture-min-seconds", type=int, default=None)
    parser.add_argument("--short-capture-max-seconds", type=int, default=None)
    parser.add_argument("--long-capture-min-seconds", type=int, default=None)
    parser.add_argument("--long-capture-max-seconds", type=int, default=None)
    parser.add_argument("--random-seed", type=int, default=None)
    parser.add_argument("--disable-random-capture-durations", action="store_true")
    parser.add_argument("--disable-random-usage-timing", action="store_true")
    parser.add_argument("--shuffle-auto-case-order", action="store_true")
    parser.add_argument(
        "--realistic-usage-profile",
        action="store_true",
        help=(
            "Enable the more realistic continuous-use profile: shuffle automated case order, "
            "do only one initial host preflight recover, and avoid intentional device reset "
            "before each capture."
        ),
    )
    parser.add_argument("--pre-start-min-delay-seconds", type=float, default=None)
    parser.add_argument("--pre-start-max-delay-seconds", type=float, default=None)
    parser.add_argument("--inter-session-gap-min-seconds", type=float, default=None)
    parser.add_argument("--inter-session-gap-max-seconds", type=float, default=None)
    parser.add_argument("--idle-min-seconds", type=float, default=None)
    parser.add_argument("--idle-max-seconds", type=float, default=None)
    parser.add_argument("--cancel-hold-min-seconds", type=float, default=None)
    parser.add_argument("--cancel-hold-max-seconds", type=float, default=None)
    parser.add_argument("--short-cancel-hold-min-seconds", type=float, default=None)
    parser.add_argument("--short-cancel-hold-max-seconds", type=float, default=None)
    parser.add_argument("--restart-pan-adapter", action="store_true")
    parser.add_argument("--skip-preflight-recover", action="store_true")
    parser.add_argument("--fail-on-warning", action="store_true")
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print full per-step progress to stdout. By default full logs go to --summary-log.",
    )
    parser.add_argument(
        "--matrix-result-json",
        default=str(MATRIX_ARTIFACT_DIR / "matrix_result.json"),
        help="Write structured matrix result JSON for CI/executor consumption.",
    )
    parser.add_argument(
        "--summary-log",
        default=str(MATRIX_ARTIFACT_DIR / "summary.log"),
        help="Write full matrix stdout to this log while keeping default stdout compact.",
    )
    parser.add_argument(
        "--preflight-recover-mode",
        choices=["per-case", "initial-only", "none"],
        default="per-case",
    )
    parser.add_argument("--no-reset-before-capture", action="store_false", dest="reset_before_capture")
    parser.set_defaults(reset_before_capture=True)
    return parser.parse_args()


def apply_execution_profile(args) -> None:
    if not args.realistic_usage_profile:
        args.execution_profile = "standard"
        return

    args.execution_profile = "realistic"
    args.shuffle_auto_case_order = True
    if not args.skip_preflight_recover:
        args.preflight_recover_mode = "initial-only"
    args.reset_before_capture = False


def resolve_duration_window(
    *,
    default_seconds: int,
    min_seconds: int | None,
    max_seconds: int | None,
    min_padding: int,
    max_padding: int,
    absolute_min: int,
    disable_random: bool,
) -> tuple[int, int]:
    if disable_random:
        return default_seconds, default_seconds

    resolved_min = (
        min_seconds
        if min_seconds is not None
        else max(absolute_min, default_seconds - min_padding)
    )
    resolved_max = (
        max_seconds
        if max_seconds is not None
        else max(resolved_min, default_seconds + max_padding)
    )
    if resolved_max < resolved_min:
        raise ValueError(
            f"invalid duration window: min_seconds={resolved_min} max_seconds={resolved_max}"
        )
    return resolved_min, resolved_max


def prepare_duration_randomizer(args) -> None:
    args.short_capture_window = resolve_duration_window(
        default_seconds=args.capture_seconds,
        min_seconds=args.short_capture_min_seconds,
        max_seconds=args.short_capture_max_seconds,
        min_padding=1,
        max_padding=2,
        absolute_min=2,
        disable_random=args.disable_random_capture_durations,
    )
    args.long_capture_window = resolve_duration_window(
        default_seconds=args.long_capture_seconds,
        min_seconds=args.long_capture_min_seconds,
        max_seconds=args.long_capture_max_seconds,
        min_padding=5,
        max_padding=5,
        absolute_min=10,
        disable_random=args.disable_random_capture_durations,
    )
    args.random_seed_resolved = (
        args.random_seed if args.random_seed is not None else int(time.time_ns() & 0xFFFFFFFF)
    )
    args.duration_rng = random.Random(args.random_seed_resolved)
    args.pre_start_delay_window = resolve_float_window(
        default_seconds=1.0,
        min_seconds=args.pre_start_min_delay_seconds,
        max_seconds=args.pre_start_max_delay_seconds,
        min_padding=0.6,
        max_padding=1.0,
        absolute_min=0.0,
        disable_random=args.disable_random_usage_timing,
    )
    args.inter_session_gap_window = resolve_float_window(
        default_seconds=1.2,
        min_seconds=args.inter_session_gap_min_seconds,
        max_seconds=args.inter_session_gap_max_seconds,
        min_padding=0.7,
        max_padding=1.3,
        absolute_min=0.0,
        disable_random=args.disable_random_usage_timing,
    )
    args.idle_wait_window = resolve_float_window(
        default_seconds=float(args.idle_seconds),
        min_seconds=args.idle_min_seconds,
        max_seconds=args.idle_max_seconds,
        min_padding=10.0,
        max_padding=15.0,
        absolute_min=5.0,
        disable_random=args.disable_random_usage_timing,
    )
    args.cancel_hold_window = resolve_float_window(
        default_seconds=1.5,
        min_seconds=args.cancel_hold_min_seconds,
        max_seconds=args.cancel_hold_max_seconds,
        min_padding=0.7,
        max_padding=0.9,
        absolute_min=0.2,
        disable_random=args.disable_random_usage_timing,
    )
    args.short_cancel_hold_window = resolve_float_window(
        default_seconds=0.2,
        min_seconds=args.short_cancel_hold_min_seconds,
        max_seconds=args.short_cancel_hold_max_seconds,
        min_padding=0.1,
        max_padding=0.25,
        absolute_min=0.05,
        disable_random=args.disable_random_usage_timing,
    )


def resolve_float_window(
    *,
    default_seconds: float,
    min_seconds: float | None,
    max_seconds: float | None,
    min_padding: float,
    max_padding: float,
    absolute_min: float,
    disable_random: bool,
) -> tuple[float, float]:
    if disable_random:
        return float(default_seconds), float(default_seconds)

    resolved_min = (
        float(min_seconds)
        if min_seconds is not None
        else max(float(absolute_min), float(default_seconds) - float(min_padding))
    )
    resolved_max = (
        float(max_seconds)
        if max_seconds is not None
        else max(resolved_min, float(default_seconds) + float(max_padding))
    )
    if resolved_max < resolved_min:
        raise ValueError(
            f"invalid float window: min_seconds={resolved_min} max_seconds={resolved_max}"
        )
    return resolved_min, resolved_max


def choose_duration(args, *, window: tuple[int, int], label: str, floor_seconds: int | None = None) -> int:
    min_seconds, max_seconds = window
    if floor_seconds is not None:
        min_seconds = max(min_seconds, floor_seconds)
        max_seconds = max(max_seconds, min_seconds)
    duration_seconds = args.duration_rng.randint(min_seconds, max_seconds)
    print(f"{label}_capture_window={min_seconds}-{max_seconds}", flush=True)
    print(f"{label}_capture_seconds={duration_seconds}", flush=True)
    return duration_seconds


def choose_duration_plan(
    args,
    *,
    window: tuple[int, int],
    count: int,
    label: str,
    floor_seconds: int | None = None,
) -> list[int]:
    durations = [
        choose_duration(args, window=window, label=f"{label}_session_{index}", floor_seconds=floor_seconds)
        for index in range(1, count + 1)
    ]
    print(f"{label}_capture_seconds_plan={','.join(str(value) for value in durations)}", flush=True)
    return durations


def choose_delay_seconds(
    args,
    *,
    window: tuple[float, float],
    label: str,
) -> float:
    min_seconds, max_seconds = window
    delay_seconds = round(args.duration_rng.uniform(min_seconds, max_seconds), 2)
    print(f"{label}_delay_window={min_seconds:.2f}-{max_seconds:.2f}", flush=True)
    print(f"{label}_delay_seconds={delay_seconds:.2f}", flush=True)
    return delay_seconds


def choose_delay_plan(
    args,
    *,
    first_window: tuple[float, float],
    followup_window: tuple[float, float],
    count: int,
    label: str,
) -> list[float]:
    delays = []
    for index in range(1, count + 1):
        window = first_window if index == 1 else followup_window
        delays.append(
            choose_delay_seconds(
                args,
                window=window,
                label=f"{label}_session_{index}_pre_start",
            )
        )
    print(
        f"{label}_pre_start_delay_plan={','.join(f'{value:.2f}' for value in delays)}",
        flush=True,
    )
    return delays


def print_case_header(case_id: str, description: str, budget_seconds: int) -> None:
    print(f"case_start={case_id}", flush=True)
    print(f"case_description={description}", flush=True)
    print(f"budget_seconds={budget_seconds}", flush=True)


def compact_case_details(summary: dict[str, object]) -> dict[str, object]:
    keys = (
        "expected_packet_count",
        "received_packet_count",
        "missing_packet_count",
        "packet_loss_ratio",
        "received_pcm_bytes",
        "duration_seconds",
        "best_corr",
        "recorded_peak",
        "active_frame_count",
        "transport_result",
        "transport_failure_reason",
        "transport_warning_reason",
        "analysis_result",
        "analysis_failure_reason",
        "wav_path",
        "serial_log_path",
        "source_wav",
        "pre_start_delay_seconds",
        "host_recovery_completed",
        "settle_window_seconds",
        "capture_phase",
        "baseline_result",
        "baseline_missing_packet_count",
        "baseline_packet_loss_ratio",
    )
    return {key: summary[key] for key in keys if key in summary}


def case_artifact_manifest(case_id: str) -> dict[str, object]:
    output_dir = case_output_dir(case_id)
    return {
        "output_dir": str(output_dir),
        "serial_logs": [str(path) for path in sorted(output_dir.glob("*.log"))],
        "wav_files": [str(path) for path in sorted(output_dir.glob("*.wav"))],
    }


def print_summary(
    case_id: str,
    result: str,
    reason: str = "",
    details: dict[str, object] | None = None,
) -> dict[str, object]:
    print(f"case_id={case_id}", flush=True)
    print(f"case_result={result}", flush=True)
    print(f"case_reason={reason}", flush=True)
    return {
        "case_id": case_id,
        "result": result,
        "reason": reason,
        "details": details or {},
        "artifacts": case_artifact_manifest(case_id),
    }


def preflight_recover_host(args, case_id: str) -> None:
    if args.skip_preflight_recover:
        print(f"preflight_recover_skipped={case_id}", flush=True)
        return
    print(f"preflight_recover_start={case_id}", flush=True)
    recover_ble_hid_host(args.device_name)
    print(f"preflight_recover_done={case_id}", flush=True)


def case_output_dir(case_id: str) -> pathlib.Path:
    path = MATRIX_ARTIFACT_DIR / case_id.lower()
    path.mkdir(parents=True, exist_ok=True)
    return path


def case_serial_log_path(case_id: str, label: str = "latest") -> pathlib.Path:
    path = case_output_dir(case_id) / f"{label}.log"
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def resolve_case_execution_order(args) -> list[str]:
    cases = parse_case_list(args.cases)
    if args.cases.strip().lower() == "auto" and args.shuffle_auto_case_order:
        shuffled_cases = list(cases)
        args.duration_rng.shuffle(shuffled_cases)
        return shuffled_cases
    return cases


def ensure_pass(case_id: str, summary: dict[str, object]) -> dict[str, object]:
    result = str(summary["result"])
    reason = str(summary.get("failure_reason") or summary.get("warning_reason") or "")
    if result != "pass":
        return print_summary(case_id, result, reason, compact_case_details(summary))
    return print_summary(case_id, "pass", "", compact_case_details(summary))


def write_matrix_result_json(
    *,
    path: str,
    summary_log: str,
    results: list[dict[str, object]],
    failed: list[dict[str, object]],
    warnings: list[dict[str, object]],
    skipped: list[dict[str, object]],
    fail_on_warning: bool,
) -> pathlib.Path:
    output_path = pathlib.Path(path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "status": "FAIL" if failed or (fail_on_warning and warnings) else "PASS",
        "fail_on_warning": bool(fail_on_warning),
        "matrix_total": len(results),
        "matrix_failed": len(failed),
        "matrix_warning": len(warnings),
        "matrix_skipped": len(skipped),
        "failed_cases": [str(item["case_id"]) for item in failed],
        "warning_cases": [str(item["case_id"]) for item in warnings],
        "skipped_cases": [str(item["case_id"]) for item in skipped],
        "summary_log": summary_log,
        "cases": results,
    }
    output_path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")
    return output_path


async def run_a1(args) -> dict[str, str]:
    use_long = args.duration_rng.random() < 0.3
    window = args.long_capture_window if use_long else args.short_capture_window
    duration_label = "long" if use_long else "short"
    capture_seconds = choose_duration(args, window=window, label=f"a1_{duration_label}")
    budget = max(240, int(capture_seconds * 3 + 60)) if use_long else 120
    print_case_header("A1", f"single recording ({duration_label})", budget)
    pre_start_delay_seconds = choose_delay_seconds(args, window=args.pre_start_delay_window, label="a1_pre_start")
    summary = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="A1",
        capture_seconds=capture_seconds,
        timeout_seconds=90,
        reset_before_capture=args.reset_before_capture,
        pre_start_delay_seconds=pre_start_delay_seconds,
        output_dir=case_output_dir("A1"),
        serial_log_path=case_serial_log_path("A1"),
    )
    return ensure_pass("A1", summary)


async def run_a2(args) -> dict[str, str]:
    budget_seconds = max(180, args.round_count * 90)
    print_case_header("A2", "multi-round continuous recording", budget_seconds)
    failures = []
    warnings = []
    capture_seconds_plan = choose_duration_plan(
        args,
        window=args.short_capture_window,
        count=args.round_count,
        label="a2",
    )
    pre_start_delay_plan = choose_delay_plan(
        args,
        first_window=args.pre_start_delay_window,
        followup_window=args.inter_session_gap_window,
        count=args.round_count,
        label="a2",
    )
    summaries = await play_and_capture_serial_toggle_multi_session(
        port=args.port,
        device_name=args.device_name,
        scenario="A2",
        capture_seconds=args.capture_seconds,
        capture_seconds_per_session=capture_seconds_plan,
        session_count=args.round_count,
        timeout_seconds=90,
        reset_before_capture=args.reset_before_capture,
        require_analysis=False,
        pre_start_delay_seconds_per_session=pre_start_delay_plan,
        output_dir=case_output_dir("A2"),
        serial_log_path=case_serial_log_path("A2"),
    )
    for round_index, summary in enumerate(summaries, start=1):
        print(f"round_index={round_index}", flush=True)
        print(f"round_result={summary['result']}", flush=True)
        print(f"round_transport_result={summary['transport_result']}", flush=True)
        print(f"round_analysis_result={summary['analysis_result']}", flush=True)
        print(f"round_capture_seconds_target={summary['capture_seconds_target']}", flush=True)
        print(f"round_missing_packet_count={summary['missing_packet_count']}", flush=True)
        print(f"round_missing_packet_indices={','.join(str(v) for v in summary['missing_packet_indices'][:16])}", flush=True)
        print(f"round_packet_loss_ratio={summary['packet_loss_ratio']:.4f}", flush=True)
        print(f"round_best_corr={summary['best_corr']:.4f}", flush=True)
        print(f"round_recorded_peak={summary['recorded_peak']}", flush=True)
        print(f"round_active_frame_count={summary['active_frame_count']}", flush=True)
        if summary["result"] == "fail":
            failures.append(round_index)
        elif summary["result"] == "warning":
            warnings.append(round_index)

    if failures:
        return print_summary("A2", "fail", "failed_rounds=" + ",".join(str(v) for v in failures))
    if warnings:
        return print_summary("A2", "warning", "warning_rounds=" + ",".join(str(v) for v in warnings))
    return print_summary("A2", "pass", "")


async def run_a3(args) -> dict[str, str]:
    print_case_header("A3", "disconnect/reconnect recovery (with BT restart)", 240)
    baseline_capture_seconds = choose_duration(args, window=args.short_capture_window, label="a3_baseline")
    baseline_pre_start_delay_seconds = choose_delay_seconds(
        args,
        window=args.pre_start_delay_window,
        label="a3_baseline_pre_start",
    )
    baseline = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="A3",
        source_label="baseline",
        capture_seconds=baseline_capture_seconds,
        timeout_seconds=90,
        reset_before_capture=args.reset_before_capture,
        pre_start_delay_seconds=baseline_pre_start_delay_seconds,
        output_dir=case_output_dir("A3"),
        serial_log_path=case_serial_log_path("A3", "baseline"),
    )
    if baseline["result"] != "pass":
        return print_summary("A3", "fail", "baseline_failed", compact_case_details(baseline))
    restart_windows_bluetooth(restart_pan_adapter=args.restart_pan_adapter)
    recover_ble_hid_host(args.device_name)
    print("a3_host_recovery_completed=1", flush=True)
    reconnect_capture_seconds = choose_duration(args, window=args.short_capture_window, label="a3_reconnect")
    reconnect_pre_start_delay_seconds = choose_delay_seconds(
        args,
        window=args.pre_start_delay_window,
        label="a3_reconnect_pre_start",
    )
    if reconnect_pre_start_delay_seconds < A3_RECONNECT_MIN_PRE_START_DELAY_SECONDS:
        print(
            "a3_reconnect_pre_start_delay_floor_seconds="
            f"{A3_RECONNECT_MIN_PRE_START_DELAY_SECONDS:.2f}",
            flush=True,
        )
        reconnect_pre_start_delay_seconds = A3_RECONNECT_MIN_PRE_START_DELAY_SECONDS
    print(f"a3_settle_window_seconds={reconnect_pre_start_delay_seconds:.2f}", flush=True)
    print("a3_capture_phase=start", flush=True)
    summary = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="A3",
        source_label="reconnect",
        capture_seconds=reconnect_capture_seconds,
        timeout_seconds=90,
        reset_before_capture=args.reset_before_capture,
        pre_start_delay_seconds=reconnect_pre_start_delay_seconds,
        output_dir=case_output_dir("A3"),
        serial_log_path=case_serial_log_path("A3", "reconnect"),
    )
    summary["host_recovery_completed"] = True
    summary["settle_window_seconds"] = float(reconnect_pre_start_delay_seconds)
    summary["capture_phase"] = "reconnect"
    summary["baseline_result"] = str(baseline.get("result", ""))
    summary["baseline_missing_packet_count"] = baseline.get("missing_packet_count")
    summary["baseline_packet_loss_ratio"] = baseline.get("packet_loss_ratio")
    return ensure_pass("A3", summary)


async def run_a6(args) -> dict[str, str]:
    a6_capture_seconds = choose_duration(
        args,
        window=args.short_capture_window,
        label="a6",
        floor_seconds=6,
    )
    idle_wait_seconds = choose_delay_seconds(args, window=args.idle_wait_window, label="a6_idle_wait")
    pre_start_delay_seconds = choose_delay_seconds(args, window=args.pre_start_delay_window, label="a6_pre_start")
    budget_seconds = int(idle_wait_seconds) + 180
    print_case_header("A6", "first recording after idle wait", budget_seconds)
    print(f"idle_wait_seconds={idle_wait_seconds:.2f}", flush=True)
    await asyncio.sleep(idle_wait_seconds)
    summary = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="A6",
        capture_seconds=a6_capture_seconds,
        timeout_seconds=90,
        reset_before_capture=False,
        pre_start_delay_seconds=pre_start_delay_seconds,
        output_dir=case_output_dir("A6"),
        serial_log_path=case_serial_log_path("A6"),
    )
    return ensure_pass("A6", summary)


async def run_a5(args) -> dict[str, str]:
    print_case_header("A5", "host-side recovery without BT restart", 180)
    baseline_capture_seconds = choose_duration(args, window=args.short_capture_window, label="a5_baseline")
    baseline_pre_start_delay_seconds = choose_delay_seconds(
        args,
        window=args.pre_start_delay_window,
        label="a5_baseline_pre_start",
    )
    baseline = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="A5",
        source_label="baseline",
        capture_seconds=baseline_capture_seconds,
        timeout_seconds=90,
        reset_before_capture=args.reset_before_capture,
        pre_start_delay_seconds=baseline_pre_start_delay_seconds,
        output_dir=case_output_dir("A5"),
        serial_log_path=case_serial_log_path("A5", "baseline"),
    )
    baseline_failure_reason = str(baseline.get("failure_reason") or baseline.get("warning_reason") or "")
    baseline_retry_allowed = (
        baseline["result"] != "pass"
        and (
            str(baseline.get("transport_result")) != "pass"
            or int(baseline.get("missing_packet_count", 0)) > 0
        )
    )
    if baseline_retry_allowed:
        print(
            "a5_baseline_retry_after_recover=1 "
            f"first_result={baseline['result']} "
            f"first_reason={baseline_failure_reason} "
            f"first_missing_packet_count={baseline['missing_packet_count']} "
            f"first_missing_packet_indices={baseline['missing_packet_indices'][:16]}",
            flush=True,
        )
        recover_ble_hid_host(args.device_name)
        baseline = await play_and_capture_serial_toggle(
            port=args.port,
            device_name=args.device_name,
            scenario="A5",
            source_label="baseline_retry",
            capture_seconds=baseline_capture_seconds,
            timeout_seconds=90,
            reset_before_capture=False,
            pre_start_delay_seconds=baseline_pre_start_delay_seconds,
            output_dir=case_output_dir("A5"),
            serial_log_path=case_serial_log_path("A5", "baseline_retry"),
        )
        if baseline["result"] != "pass":
            return print_summary(
                "A5",
                "fail",
                "baseline_failed_after_retry "
                f"missing_packet_count={baseline['missing_packet_count']} "
                f"missing_packet_indices={baseline['missing_packet_indices'][:16]}",
            )
    elif baseline["result"] != "pass":
        return print_summary(
            "A5",
            baseline["result"],
            "baseline_failed_without_retry "
            f"reason={baseline_failure_reason} "
            f"missing_packet_count={baseline.get('missing_packet_count', 0)} "
            f"missing_packet_indices={baseline.get('missing_packet_indices', [])[:16]}",
        )
    after_restart_capture_seconds = choose_duration(args, window=args.short_capture_window, label="a5_after_restart")
    after_restart_pre_start_delay_seconds = choose_delay_seconds(
        args,
        window=args.pre_start_delay_window,
        label="a5_after_restart_pre_start",
    )
    summary = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="A5",
        source_label="after_restart",
        capture_seconds=after_restart_capture_seconds,
        timeout_seconds=90,
        reset_before_capture=False,
        pre_start_delay_seconds=after_restart_pre_start_delay_seconds,
        output_dir=case_output_dir("A5"),
        serial_log_path=case_serial_log_path("A5", "after_restart"),
    )
    return ensure_pass("A5", summary)


async def run_a7(args) -> dict[str, str]:
    print_case_header("A7", "cancel during recording and recover (long + short hold)", 300)
    # First probe: long cancel hold
    long_cancel_hold = choose_delay_seconds(args, window=args.cancel_hold_window, label="a7_long_cancel")
    capture_seconds = choose_duration(args, window=args.short_capture_window, label="a7")
    pre_start_delay = choose_delay_seconds(args, window=args.pre_start_delay_window, label="a7_pre_start")
    summary = await play_and_capture_serial_toggle_after_cancel_probe(
        port=args.port,
        device_name=args.device_name,
        scenario="A7",
        capture_seconds=capture_seconds,
        cancel_hold_seconds=long_cancel_hold,
        timeout_seconds=90,
        reset_before_capture=args.reset_before_capture,
        pre_start_delay_seconds=pre_start_delay,
        output_dir=case_output_dir("A7"),
        serial_log_path=case_serial_log_path("A7", "long_hold"),
    )
    print(f"long_cancel_probe={'pass' if summary.get('cancel_requested') and summary.get('cancel_completed') else 'fail'}", flush=True)
    print(f"cancel_requested={1 if summary.get('cancel_requested') else 0}", flush=True)
    print(f"cancel_completed={1 if summary.get('cancel_completed') else 0}", flush=True)
    if summary["result"] != "pass":
        if summary.get("failure_reason") == "short_cancel_probe_failed":
            return print_summary("A7", "fail", "long_cancel_probe_failed")
        return print_summary(
            "A7",
            summary["result"],
            f"{summary.get('failure_reason') or summary.get('warning_reason') or ''} "
            f"missing_packet_count={summary['missing_packet_count']} "
            f"missing_packet_indices={summary['missing_packet_indices'][:16]}",
        )

    # Second probe: short cancel hold (merged from old P9)
    short_cancel_hold = choose_delay_seconds(args, window=args.short_cancel_hold_window, label="a7_short_cancel")
    short_summary = await play_and_capture_serial_toggle_after_cancel_probe(
        port=args.port,
        device_name=args.device_name,
        scenario="A7",
        capture_seconds=capture_seconds,
        cancel_hold_seconds=short_cancel_hold,
        timeout_seconds=90,
        reset_before_capture=False,
        pre_start_delay_seconds=pre_start_delay,
        output_dir=case_output_dir("A7"),
        serial_log_path=case_serial_log_path("A7", "short_hold"),
    )
    print(f"short_cancel_probe={'pass' if short_summary.get('cancel_requested') and short_summary.get('cancel_completed') else 'fail'}", flush=True)
    if short_summary["result"] != "pass":
        return print_summary("A7", short_summary["result"], f"short_cancel_failed: {short_summary.get('failure_reason', '')}")
    return print_summary("A7", "pass", "")


async def run_a4(args) -> dict[str, str]:
    round_count = args.round_count
    budget_seconds = max(240, round_count * 120)
    print_case_header("A4", f"multi-round independent reconnect ({round_count} rounds)", budget_seconds)
    failures = []
    warnings = []
    for round_index in range(1, round_count + 1):
        capture_seconds = choose_duration(args, window=args.short_capture_window, label=f"a4_r{round_index}")
        pre_start_delay = choose_delay_seconds(args, window=args.pre_start_delay_window, label=f"a4_r{round_index}_pre_start")
        summary = await play_and_capture_serial_toggle(
            port=args.port,
            device_name=args.device_name,
            scenario="A4",
            source_label=f"round{round_index}",
            capture_seconds=capture_seconds,
            timeout_seconds=90,
            reset_before_capture=True,  # Full reset each round
            pre_start_delay_seconds=pre_start_delay,
            output_dir=case_output_dir("A4"),
            serial_log_path=case_serial_log_path("A4", f"round{round_index}"),
        )
        print(f"a4_round_index={round_index}", flush=True)
        print(f"a4_round_result={summary['result']}", flush=True)
        print(f"a4_round_missing_packet_count={summary['missing_packet_count']}", flush=True)
        if summary["result"] == "fail":
            failures.append(round_index)
        elif summary["result"] == "warning":
            warnings.append(round_index)
    if failures:
        return print_summary("A4", "fail", "failed_rounds=" + ",".join(str(v) for v in failures))
    if warnings:
        return print_summary("A4", "warning", "warning_rounds=" + ",".join(str(v) for v in warnings))
    return print_summary("A4", "pass", "")


async def run_a8(args) -> dict[str, str]:
    print_case_header("A8", "silent/no-input negative test", 120)
    capture_seconds = choose_duration(args, window=args.short_capture_window, label="a8")
    pre_start_delay = choose_delay_seconds(args, window=args.pre_start_delay_window, label="a8_pre_start")
    capture_args = make_capture_args(
        port=args.port,
        device_name=args.device_name,
        capture_seconds=capture_seconds,
        capture_seconds_per_session=[capture_seconds],
        session_pre_start_delay_seconds=[pre_start_delay],
        timeout_seconds=90,
        reset_before_capture=args.reset_before_capture,
        output_dir=case_output_dir("A8"),
        serial_log_path=case_serial_log_path("A8"),
    )
    session_summaries = await capture_sessions(capture_args)
    if not session_summaries:
        return print_summary("A8", "fail", "no_session_captured")
    summary = dict(session_summaries[-1])
    recorded_wav = pathlib.Path(summary["wav_path"])
    if not recorded_wav.exists():
        return print_summary("A8", "fail", "no_output_wav")
    frames = read_wav_frames(recorded_wav)
    peak = max(abs(v) for v in frames) if frames else 0
    env = compute_envelope(frames)
    runs = detect_active_runs(env)
    active_frames = sum(end - start for start, end in runs)
    print(f"a8_recorded_peak={peak}", flush=True)
    print(f"a8_active_frame_count={active_frames}", flush=True)
    print(f"a8_total_frames={len(frames)}", flush=True)
    validation = validate_transport_summary(summary, capture_seconds=capture_seconds)
    transport_ok = validation["transport_result"] != "fail" or validation["transport_failure_reason"] != "no_audio_packets_received"
    print(f"a8_transport_ok={1 if transport_ok else 0}", flush=True)
    if not transport_ok:
        return print_summary("A8", "fail", "transport_failed_even_without_audio")
    return print_summary("A8", "pass", f"peak={peak} active_frames={active_frames}")


RAPID_TOGGLE_COUNT_DEFAULT = 10


async def run_a9(args) -> dict[str, str]:
    toggle_count = RAPID_TOGGLE_COUNT_DEFAULT
    print_case_header("A9", f"rapid toggle stress ({toggle_count} cycles)", 180)
    stress_ok = True
    stress_errors = []
    with open_serial_with_retry(args.port) as ser:
        ser.setDTR(False)
        ser.setRTS(False)
        ser.reset_input_buffer()
        for cycle in range(1, toggle_count + 1):
            try:
                send_toggle(ser)
                await asyncio.sleep(round(args.duration_rng.uniform(0.3, 0.8), 2))
                send_toggle(ser)
                await asyncio.sleep(round(args.duration_rng.uniform(0.1, 0.3), 2))
            except (OSError, serial.SerialException) as exc:
                stress_ok = False
                stress_errors.append(f"cycle={cycle} {type(exc).__name__}:{exc}")
                break
        print(f"a9_stress_toggle_count={toggle_count}", flush=True)
        print(f"a9_stress_ok={1 if stress_ok else 0}", flush=True)
        if stress_errors:
            for error in stress_errors[:5]:
                print(f"a9_stress_error={error}", flush=True)
        await asyncio.sleep(1.0)
        try:
            ser.reset_input_buffer()
        except (OSError, serial.SerialException):
            pass
    if not stress_ok:
        return print_summary("A9", "fail", "serial_error_during_stress")
    verify_capture_seconds = choose_duration(args, window=args.short_capture_window, label="a9_verify")
    pre_start_delay = choose_delay_seconds(args, window=args.pre_start_delay_window, label="a9_verify_pre_start")
    summary = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="A9",
        source_label="post_stress_verify",
        capture_seconds=verify_capture_seconds,
        timeout_seconds=90,
        reset_before_capture=True,
        pre_start_delay_seconds=pre_start_delay,
        output_dir=case_output_dir("A9"),
        serial_log_path=case_serial_log_path("A9"),
    )
    summary["stress_toggle_count"] = toggle_count
    summary["stress_ok"] = stress_ok
    return ensure_pass("A9", summary)


async def run_a10(args) -> dict[str, str]:
    print_case_header("A10", "concurrent BLE client attempt during capture", 180)
    address_hex = get_paired_device_address_hex(args.device_name)
    if not address_hex:
        return print_summary("A10", "fail", "unable_to_resolve_device_address")
    from winrt.windows.devices.bluetooth import BluetoothLEDevice
    rogue_device = None
    try:
        rogue_device = await BluetoothLEDevice.from_bluetooth_address_async(int(address_hex, 16))
    except Exception as exc:
        print(f"a10_rogue_connect_error={type(exc).__name__}:{exc}", flush=True)
    rogue_connected = False
    if rogue_device is not None:
        try:
            rogue_connected = rogue_device.connection_status == 1
        except Exception:
            pass
    print(f"a10_rogue_device_open={1 if rogue_device else 0}", flush=True)
    print(f"a10_rogue_connected={1 if rogue_connected else 0}", flush=True)
    capture_seconds = choose_duration(args, window=args.short_capture_window, label="a10")
    pre_start_delay = choose_delay_seconds(args, window=args.pre_start_delay_window, label="a10_pre_start")
    try:
        summary = await play_and_capture_serial_toggle(
            port=args.port,
            device_name=args.device_name,
            scenario="A10",
            source_label="concurrent_test",
            capture_seconds=capture_seconds,
            timeout_seconds=90,
            reset_before_capture=args.reset_before_capture,
            pre_start_delay_seconds=pre_start_delay,
            output_dir=case_output_dir("A10"),
            serial_log_path=case_serial_log_path("A10"),
        )
    finally:
        if rogue_device is not None:
            try:
                rogue_device.close()
            except Exception:
                pass
    summary["rogue_device_open"] = rogue_device is not None
    summary["rogue_connected"] = rogue_connected
    return ensure_pass("A10", summary)


async def run_h1(args) -> dict[str, str]:
    print_case_header("H1", "physical KEY1 semi-auto (waiting for button press)", 180)
    capture_seconds = choose_duration(args, window=args.short_capture_window, label="h1")
    serial_log_path = case_serial_log_path("H1")
    output_dir = case_output_dir("H1")
    capture_args = argparse.Namespace(
        port=args.port,
        device_name=args.device_name,
        capture_seconds=capture_seconds,
        output_dir=str(output_dir),
        serial_log_path=str(serial_log_path),
        timeout_seconds=90,
        boot_timeout_seconds=15,
        ble_connect_timeout_seconds=15,
        notify_ready_timeout_seconds=45,
        trigger_mode="physical-key",
        max_sessions=1,
        reset_before_capture=args.reset_before_capture,
    )
    summaries = await run_capture_with_args(capture_args)
    summary = dict(summaries[-1])
    serial_log_text = serial_log_path.read_text(encoding="utf-8", errors="replace") if serial_log_path.exists() else ""
    key_start_seen = "recording start source=key1" in serial_log_text
    key_stop_seen = "recording stop source=key1" in serial_log_text
    print(f"h1_key_start_seen={1 if key_start_seen else 0}", flush=True)
    print(f"h1_key_stop_seen={1 if key_stop_seen else 0}", flush=True)
    if not key_start_seen or not key_stop_seen:
        return print_summary("H1", "fail", f"physical_key_source_missing start={key_start_seen} stop={key_stop_seen}")
    validation = validate_transport_summary(summary, capture_seconds=capture_seconds, min_ratio=0.0, max_extra_seconds=float(90))
    summary.update(validation)
    if validation["transport_result"] == "fail" and validation["transport_failure_reason"] == "missing_expected_packet_count":
        return print_summary("H1", "fail", "no_audio_packets")
    result = "pass"
    reason = ""
    if validation["transport_result"] == "fail":
        result = "fail"
        reason = validation.get("transport_failure_reason", "")
    return print_summary("H1", result, reason)


CASE_RUNNERS = {
    "A1": run_a1,
    "A2": run_a2,
    "A3": run_a3,
    "A4": run_a4,
    "A5": run_a5,
    "A6": run_a6,
    "A7": run_a7,
    "A8": run_a8,
    "A9": run_a9,
    "A10": run_a10,
    "H1": run_h1,
}


async def main_async(args) -> None:
    if args.skip_preflight_recover:
        args.preflight_recover_mode = "none"

    case_order = resolve_case_execution_order(args)
    print(f"execution_profile={args.execution_profile}", flush=True)
    print(f"random_capture_durations_enabled={0 if args.disable_random_capture_durations else 1}", flush=True)
    print(f"random_usage_timing_enabled={0 if args.disable_random_usage_timing else 1}", flush=True)
    print(f"shuffle_auto_case_order={1 if args.shuffle_auto_case_order else 0}", flush=True)
    print(f"preflight_recover_mode={args.preflight_recover_mode}", flush=True)
    print(f"random_seed={args.random_seed_resolved}", flush=True)
    print(
        f"short_capture_window={args.short_capture_window[0]}-{args.short_capture_window[1]}",
        flush=True,
    )
    print(
        f"long_capture_window={args.long_capture_window[0]}-{args.long_capture_window[1]}",
        flush=True,
    )
    print(
        "pre_start_delay_window="
        f"{args.pre_start_delay_window[0]:.2f}-{args.pre_start_delay_window[1]:.2f}",
        flush=True,
    )
    print(
        "inter_session_gap_window="
        f"{args.inter_session_gap_window[0]:.2f}-{args.inter_session_gap_window[1]:.2f}",
        flush=True,
    )
    print(
        "idle_wait_window="
        f"{args.idle_wait_window[0]:.2f}-{args.idle_wait_window[1]:.2f}",
        flush=True,
    )
    print(
        "cancel_hold_window="
        f"{args.cancel_hold_window[0]:.2f}-{args.cancel_hold_window[1]:.2f}",
        flush=True,
    )
    print(
        "short_cancel_hold_window="
        f"{args.short_cancel_hold_window[0]:.2f}-{args.short_cancel_hold_window[1]:.2f}",
        flush=True,
    )
    print(f"case_execution_order={','.join(case_order)}", flush=True)
    results = []
    initial_preflight_done = False
    for case_id in case_order:
        if case_id in MANUAL_OR_EXTERNAL_CASES:
            results.append(print_summary(case_id, "skipped", MANUAL_OR_EXTERNAL_CASES[case_id]))
            continue
        runner = CASE_RUNNERS.get(case_id)
        if runner is None:
            results.append(print_summary(case_id, "skipped", "unknown_or_not_automated"))
            continue
        try:
            if args.preflight_recover_mode == "per-case":
                preflight_recover_host(args, case_id)
            elif args.preflight_recover_mode == "initial-only":
                if not initial_preflight_done:
                    preflight_recover_host(args, case_id)
                    initial_preflight_done = True
                else:
                    print(f"preflight_recover_deferred={case_id}", flush=True)
            else:
                print(f"preflight_recover_skipped={case_id}", flush=True)
            results.append(await runner(args))
        except Exception as exc:
            results.append(print_summary(case_id, "fail", f"{type(exc).__name__}:{exc}"))

    failed = [item for item in results if item["result"] == "fail"]
    warnings = [item for item in results if item["result"] == "warning"]
    skipped = [item for item in results if item["result"] == "skipped"]
    for item in results:
        print(
            f"matrix_item={item['case_id']}:{item['result']}:{item.get('reason', '')}",
            flush=True,
        )
    print("matrix_summary=1", flush=True)
    print(f"matrix_total={len(results)}", flush=True)
    print(f"matrix_failed={len(failed)}", flush=True)
    print(f"matrix_warning={len(warnings)}", flush=True)
    print(f"matrix_skipped={len(skipped)}", flush=True)
    matrix_result_path = write_matrix_result_json(
        path=args.matrix_result_json,
        summary_log=args.summary_log,
        results=results,
        failed=failed,
        warnings=warnings,
        skipped=skipped,
        fail_on_warning=args.fail_on_warning,
    )
    print(f"matrix_result_json={matrix_result_path}", flush=True)
    print(f"matrix_summary_log={args.summary_log}", flush=True)
    if failed:
        failed_ids = ",".join(item["case_id"] for item in failed)
        print(f"matrix_failed_cases={failed_ids}", flush=True)
        raise RuntimeError(f"verify_audio_ble_product_matrix: failed cases: {failed_ids}")
    if args.fail_on_warning and warnings:
        warning_ids = ",".join(item["case_id"] for item in warnings)
        print(f"matrix_warning_cases={warning_ids}", flush=True)
        raise RuntimeError(f"verify_audio_ble_product_matrix: warning cases: {warning_ids}")


def main() -> None:
    configure_utf8_stdio()
    args = parse_args()
    apply_execution_profile(args)
    prepare_duration_randomizer(args)
    summary_log_path = pathlib.Path(args.summary_log)
    summary_log_path.parent.mkdir(parents=True, exist_ok=True)
    original_stdout = sys.stdout
    log_file = summary_log_path.open("w", encoding="utf-8")
    sys.stdout = MatrixStdoutFilter(original_stdout, log_file, verbose=args.verbose)
    try:
        asyncio.run(main_async(args))
    except RuntimeError as exc:
        print(f"matrix_error={type(exc).__name__}:{exc}", flush=True)
        sys.exit(1)
    except KeyboardInterrupt:
        print("interrupted=1", flush=True)
        sys.exit(130)
    finally:
        sys.stdout.flush()
        sys.stdout = original_stdout
        log_file.close()


if __name__ == "__main__":
    main()
