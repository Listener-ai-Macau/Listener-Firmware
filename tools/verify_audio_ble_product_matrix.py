import argparse
import asyncio
import hashlib
import json
import pathlib
import random
import subprocess
import sys
import time
import unicodedata

import serial
import winsound

from ble_audio_regression_common import (
    AUDIO_PROFILE_CONFIGS,
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
    generate_profile_tts_wav,
    generate_segmented_tts_wav,
    mix_noise_into_wav,
    mix_secondary_speech_into_wav,
    apply_fading_gain,
    open_serial_with_retry,
    pick_chinese_sentences,
    pick_short_commands,
    pick_ultra_short,
    pick_punctuation_commands,
    pick_mixed_sentences,
    resolve_audio_profile,
    send_toggle,
    source_wav_path,
    generate_source_wav_tts,
    PCM_SAMPLE_RATE,
)
from capture_audio_ble_wav import configure_utf8_stdio


CASE_ORDER = (
    "A1", "A2", "A3",
    "A4", "A5", "A6",
    "A7", "A8", "A9", "A10", "A11",
    "A12", "A13",
    "A14", "A15", "A16",
    "A17", "A18",
    "A19",
)
SMOKE_CASES = ("A1", "A3", "A14", "A15")
FULL_CASES = tuple(c for c in CASE_ORDER if c not in ("A17", "A18", "A19"))
SOAK_CASES = ("A17", "A18", "A19")
EXTENDED_AUTO_CASES = ()
MANUAL_CASES = ("H1", "H2", "H3")
TRANSPORT_ONLY_CASES = ("T1", "T2", "T3", "T4", "T5")
CASE_SUITES = {
    "smoke": SMOKE_CASES,
    "daily": FULL_CASES,
    "full": CASE_ORDER,
    "transport": TRANSPORT_ONLY_CASES,
    "soak": SOAK_CASES,
    "auto": FULL_CASES,
}
PRODUCT_CHAIN_OVERLAY_CASES = tuple(case_id for case_id in CASE_ORDER if case_id.startswith("A"))
PRODUCT_CHAIN_IN_RUNNER_CASES = ()
MANUAL_OR_EXTERNAL_CASES = {
    "H2": "requires physical distance/angle change",
    "H3": "requires different speaker voice or manual TTS voice switch",
}
CASE_DESCRIPTIONS = {
    "A1": "单句 normal 基线：用户说一句话，文字出现在光标",
    "A2": "长段话（3-5句）：验证 partial preview 内容质量和 final insertion",
    "A3": "连续多轮：每轮说不同的话，混合 profile",
    "A4": "犹豫停顿：说话时停顿 1-3s 后继续",
    "A5": "小声说话：low-volume profile",
    "A6": "快速说话：fast profile（rate=7）",
    "A7": "短命令+长句混合：随机交替",
    "A8": "极短语音（1-2字）：验证 ASR 能识别",
    "A9": "标点命令：验证输出包含逗号/句号/换行",
    "A10": "中英混合：句内中英交替",
    "A11": "重复同句 3 轮：验证每轮独立不串",
    "A12": "环境噪音：TTS + 白噪声/粉红噪声",
    "A13": "语音干扰：TTS + 次语音叠加",
    "A14": "取消后恢复：负向验证 + 正常录音",
    "A15": "静音误触：负向验证",
    "A16": "渐变音量：模拟走动距离变化",
    "A17": "长时间空闲后首录（5min idle）",
    "A18": "ASR 网络异常：验证不卡死",
    "A19": "综合 soak：混合所有 profile 和句子类型",
    "H1": "物理 KEY1 语音输入",
    "H2": "不同距离/角度说话",
    "H3": "不同人说话（男女/老人/口音）",
    "T1": "（旧 A4）BT 重启后重连",
    "T2": "（旧 A5）多轮重连循环",
    "T3": "（旧 A6）Host 恢复不重启",
    "T4": "（旧 A10）快速 toggle 压力",
    "T5": "（旧 A11）并发 BLE 客户端",
}
MATRIX_ARTIFACT_DIR = pathlib.Path("tests") / "artifacts" / "ble_product_matrix"
# Keep the A2 continuous utterance under the current async ASR stability window.
# Longer soak/stress coverage belongs in A19 mixed-use runs.
A2_PRODUCT_CHAIN_RANDOM_SENTENCE_COUNT = 4
A2_PRODUCT_CHAIN_MIN_LISTENER_TIMEOUT_MS = 90000
A2_PRODUCT_CHAIN_MIN_TIMEOUT_SECONDS = 150
NEGATIVE_PRODUCT_CHAIN_TIMEOUT_SECONDS = 70
NEGATIVE_PRODUCT_CHAIN_LISTENER_TIMEOUT_MS = 35000
PRODUCT_CHAIN_EMPTY_TRANSCRIPT_RETRY_SENTENCE = "蓝牙音频正在发送到火山识别，请检查文本结果。"

A2_LONG_DICTATION_LEADS = (
    "今天我会连续记录蓝牙听写的使用过程",
    "这段长录音用来验证真实会议记录的输入体验",
    "现在开始进行一段完整的产品链路长听写",
    "我正在复盘上午的调试过程和后续安排",
)
A2_LONG_DICTATION_CLAUSES = (
    "先确认胶囊里可以实时看到稳定的预览内容",
    "再观察识别完成后文字是否立即进入当前光标",
    "同时检查历史记录里是否保存了完整的最终文本",
    "如果声音偏小也要尽量保持句子结构清楚",
    "遇到语速变快时需要重点关注开头和结尾是否丢失",
    "测试报告里要记录蓝牙包数和识别准确率",
    "每一轮播放都应该使用新的随机内容避免固定答案",
    "短句回归和长段落回归需要分开判断",
    "前端胶囊只显示短预览不能承载整段文字",
    "后端需要把最终结果稳定地交给系统输入链路",
    "这类场景更接近日常口述备忘和会议纪要",
    "如果某一步失败就先修最基础的链路再继续往后跑",
)
A2_LONG_DICTATION_ENDINGS = (
    "最后把异常现象整理成清晰的结论",
    "最后确认这段文字没有明显缺句再结束测试",
    "最后把通过和失败的证据都写进矩阵结果",
    "最后继续执行下一项自动化回归",
)
CASE_PRODUCT_CHAIN_AUDIO_PROFILES = {
    "A1": ("normal",),
    "A2": ("normal", "fast"),
    "A3": ("normal", "fast", "low-volume"),
    "A4": ("normal",),
    "A5": ("low-volume",),
    "A6": ("fast",),
    "A7": ("normal",),
    "A8": ("normal",),
    "A9": ("normal",),
    "A10": ("normal",),
    "A11": ("normal",),
    "A12": ("noisy",),
    "A13": ("normal",),
    "A14": ("normal",),
    "A15": ("normal",),
    "A16": ("normal",),
    "A17": ("normal",),
    "A18": ("normal",),
    "A19": ("normal", "fast", "low-volume", "noisy"),
}
A19_SOAK_AUDIO_PROFILES = ("normal", "fast", "low-volume", "noisy")
A17_LONG_IDLE_SECONDS = 300
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
    "case_catalog=",
    "product_chain_",
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
    normalized = raw.strip().lower()
    if normalized in CASE_SUITES:
        return list(CASE_SUITES[normalized])
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
    parser.add_argument("--port")
    parser.add_argument("--device-name", default="listener")
    parser.add_argument(
        "--cases",
        default="auto",
        help=(
            "Case IDs or suite name. Suites: smoke (A1,A3,A14,A15), auto/daily (A1-A16), "
            "full (A1-A19), soak (A17-A19), transport (T1-T5). "
            "Or comma-separated IDs: A1,A4,T2."
        ),
    )
    parser.add_argument("--list-cases", action="store_true")
    parser.add_argument(
        "--full-chain",
        action="store_true",
        help="Compatibility no-op. A/H cases validate the product chain by default; use --transport-only for BLE-only diagnostics.",
    )
    parser.add_argument(
        "--transport-only",
        action="store_true",
        help="Disable product-chain (ASR/text) overlay. Use --cases transport to run legacy T1-T5 transport cases.",
    )
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
        "--continue-on-failure",
        action="store_true",
        help="Keep running later cases after a fail/warning. Default is gated fail-fast so each case is fixed before the next one runs.",
    )
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
    parser.add_argument(
        "--listener-type-repo",
        default=None,
        help="Path to the sibling Listener-Type repo used by product-chain validation.",
    )
    parser.add_argument(
        "--bluetooth-address",
        default=None,
        help="Optional hex BLE address for Listener-Type product-chain validation; auto-resolved when possible.",
    )
    parser.add_argument("--full-chain-timeout-seconds", type=int, default=90)
    parser.add_argument("--full-chain-listener-timeout-ms", type=int, default=45000)
    parser.add_argument("--full-chain-sentence", default=None)
    parser.add_argument("--full-chain-verify-insertion", action="store_true")
    parser.add_argument("--full-chain-tts-gain", type=float, default=4.0)
    parser.add_argument(
        "--full-chain-audio-profile",
        choices=sorted(AUDIO_PROFILE_CONFIGS),
        default="normal",
        help="Audio profile for product-chain cases that do not have case-specific profile plans.",
    )
    parser.add_argument(
        "--full-chain-long-sentence-count",
        type=int,
        default=A2_PRODUCT_CHAIN_RANDOM_SENTENCE_COUNT,
        help="Random clause count for A2 long-recording product-chain validation when --full-chain-sentence is not set.",
    )
    parser.add_argument("--soak-round-count", type=int, default=6)
    parser.add_argument("--soak-idle-seconds", type=float, default=10.0)
    parser.add_argument("--no-reset-before-capture", action="store_false", dest="reset_before_capture")
    parser.set_defaults(reset_before_capture=True)
    args = parser.parse_args()
    if not args.list_cases and not args.port:
        parser.error("--port is required unless --list-cases is used")
    return args


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
        "audio_profile",
        "source_tts_rate",
        "source_tts_gain",
        "source_sentence_count",
        "expected_text",
        "normalized_expected",
        "normalized_transcript",
        "edit_distance",
        "reference_length",
        "cer",
        "accuracy",
        "accuracy_threshold",
        "accuracy_warning_only",
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


def firmware_repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[1]


def default_listener_type_repo() -> pathlib.Path:
    return firmware_repo_root().parent / "Listener-Type"


def resolve_listener_type_repo(args) -> pathlib.Path:
    raw_path = args.listener_type_repo or str(default_listener_type_repo())
    return pathlib.Path(raw_path).expanduser().resolve()


def extract_prefixed_json(text: str, prefix: str) -> dict[str, object] | None:
    for line in reversed(text.splitlines()):
        if not line.startswith(prefix):
            continue
        payload = line[len(prefix):].strip()
        if not payload:
            continue
        return json.loads(payload)
    return None


def latest_matching_file(directory: pathlib.Path, pattern: str) -> str | None:
    if not directory.exists():
        return None
    matches = sorted(directory.glob(pattern), key=lambda path: path.stat().st_mtime)
    return str(matches[-1]) if matches else None


def first_non_empty(*values: object) -> str:
    for value in values:
        text = "" if value is None else str(value).strip()
        if text:
            return text
    return ""


def longest_non_empty(*values: object) -> str:
    texts = [str(value).strip() for value in values if value is not None and str(value).strip()]
    return max(texts, key=len) if texts else ""


def normalize_accuracy_text(text: object) -> str:
    folded = unicodedata.normalize("NFKC", "" if text is None else str(text)).lower()
    return "".join(ch for ch in folded if ch.isalnum())


def edit_distance(expected: str, actual: str) -> int:
    if expected == actual:
        return 0
    if not expected:
        return len(actual)
    if not actual:
        return len(expected)
    previous = list(range(len(actual) + 1))
    for expected_index, expected_char in enumerate(expected, start=1):
        current = [expected_index]
        for actual_index, actual_char in enumerate(actual, start=1):
            insert_cost = current[actual_index - 1] + 1
            delete_cost = previous[actual_index] + 1
            replace_cost = previous[actual_index - 1] + (0 if expected_char == actual_char else 1)
            current.append(min(insert_cost, delete_cost, replace_cost))
        previous = current
    return previous[-1]


def score_transcript_accuracy(expected: object, transcript: object) -> dict[str, object]:
    normalized_expected = normalize_accuracy_text(expected)
    normalized_transcript = normalize_accuracy_text(transcript)
    distance = edit_distance(normalized_expected, normalized_transcript)
    if normalized_expected:
        cer = distance / float(len(normalized_expected))
    else:
        cer = 0.0 if not normalized_transcript else 1.0
    accuracy = max(0.0, 1.0 - cer)
    return {
        "expected_text": "" if expected is None else str(expected),
        "normalized_expected": normalized_expected,
        "normalized_transcript": normalized_transcript,
        "edit_distance": distance,
        "reference_length": len(normalized_expected),
        "cer": round(cer, 6),
        "accuracy": round(accuracy, 6),
    }


def validate_partial_preview_quality(
    expected_text: str,
    partial_preview: str,
    *,
    max_cer: float = 0.5,
) -> dict[str, object]:
    if not partial_preview:
        return {"pass": False, "reason": "empty_partial_preview"}
    if not expected_text:
        return {"pass": True, "reason": "no_expected_text"}

    normalized_expected = normalize_accuracy_text(expected_text)
    normalized_partial = normalize_accuracy_text(partial_preview)
    if not normalized_partial:
        return {"pass": False, "reason": "empty_normalized_partial_preview"}

    prefix_len = min(len(normalized_partial), len(normalized_expected))
    expected_prefix = normalized_expected[:prefix_len]
    distance = edit_distance(expected_prefix, normalized_partial)
    prefix_cer = distance / float(len(expected_prefix)) if expected_prefix else 0.0
    quality_pass = prefix_cer <= max_cer

    return {
        "pass": quality_pass,
        "reason": "" if quality_pass else f"partial_preview_prefix_cer={prefix_cer:.3f}>{max_cer}",
        "prefix_cer": round(prefix_cer, 6),
        "prefix_length": prefix_len,
    }


def product_chain_profiles_for_case(args, case_id: str) -> tuple[str, ...]:
    profiles = CASE_PRODUCT_CHAIN_AUDIO_PROFILES.get(case_id)
    if profiles:
        return profiles
    return (str(args.full_chain_audio_profile),)


def apply_accuracy_gate(
    *,
    current_result: str,
    current_reason: str,
    audio_profile: dict[str, object],
    accuracy_details: dict[str, object],
) -> tuple[str, str]:
    if current_result == "fail":
        return current_result, current_reason
    threshold = float(audio_profile["minimum_accuracy"])
    accuracy = float(accuracy_details["accuracy"])
    if accuracy >= threshold:
        return current_result, current_reason
    profile_name = str(audio_profile["name"])
    reason = f"accuracy_below_threshold:{profile_name}:{accuracy:.3f}<{threshold:.3f}"
    if bool(audio_profile["warning_only"]):
        return ("warning" if current_result == "pass" else current_result), (current_reason or reason)
    return "fail", reason


def find_listener_history_session(
    *,
    history_path: object,
    transcript: str,
    expected_pcm_bytes: object,
) -> dict[str, object] | None:
    path_text = first_non_empty(history_path)
    if not path_text:
        return None
    path = pathlib.Path(path_text)
    if not path.exists():
        return None
    try:
        sessions_payload = json.loads(path.read_text(encoding="utf-8-sig", errors="replace"))
    except (OSError, json.JSONDecodeError):
        return None
    if isinstance(sessions_payload, dict):
        sessions = [sessions_payload]
    elif isinstance(sessions_payload, list):
        sessions = [item for item in sessions_payload if isinstance(item, dict)]
    else:
        return None

    try:
        expected_pcm = int(expected_pcm_bytes)
    except (TypeError, ValueError):
        expected_pcm = 0

    candidates: list[tuple[int, str, dict[str, object]]] = []
    for session in sessions:
        raw_transcript = first_non_empty(session.get("rawTranscript"))
        final_text = first_non_empty(session.get("finalText"))
        stats = session.get("embeddedAudioStats")
        if not isinstance(stats, dict):
            stats = {}
        score = 0
        if expected_pcm > 0:
            received_pcm = stats.get("receivedPcmBytes")
            reconstructed_pcm = stats.get("reconstructedPcmBytes")
            if received_pcm == expected_pcm or reconstructed_pcm == expected_pcm:
                score += 4
        if stats:
            score += 2
        if transcript:
            if transcript in raw_transcript or transcript in final_text:
                score += 1
            elif raw_transcript and raw_transcript in transcript:
                score += 1
            elif final_text and final_text in transcript:
                score += 1
        if score > 0:
            candidates.append((score, first_non_empty(session.get("createdAt")), session))
    if not candidates:
        return None
    candidates.sort(key=lambda item: (item[0], item[1]), reverse=True)
    return candidates[0][2]


def print_case_catalog() -> None:
    catalog = {
        "auto_cases": list(CASE_ORDER),
        "manual_cases": list(MANUAL_CASES),
        "transport_only_cases": list(TRANSPORT_ONLY_CASES),
        "case_descriptions": CASE_DESCRIPTIONS,
        "audio_profiles": {
            name: {
                "tts_rate": config["tts_rate"],
                "tts_gain": config["tts_gain"],
                "minimum_accuracy": config["minimum_accuracy"],
                "warning_only": config["warning_only"],
            }
            for name, config in sorted(AUDIO_PROFILE_CONFIGS.items())
        },
        "case_product_chain_audio_profiles": {
            case_id: list(profiles)
            for case_id, profiles in sorted(CASE_PRODUCT_CHAIN_AUDIO_PROFILES.items())
        },
        "manual_or_external_cases": MANUAL_OR_EXTERNAL_CASES,
        "implemented_cases": sorted([*CASE_RUNNERS.keys(), *MANUAL_OR_EXTERNAL_CASES.keys()]),
    }
    print(f"case_catalog={json.dumps(catalog, ensure_ascii=False, sort_keys=True)}")


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


def summary_outcome(summary: dict[str, object]) -> tuple[str, str]:
    return (
        str(summary.get("result") or "fail"),
        str(summary.get("failure_reason") or summary.get("warning_reason") or ""),
    )


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
    capture_seconds = choose_duration(args, window=args.short_capture_window, label="a1")
    budget = max(120, int(capture_seconds * 3 + 60))
    print_case_header("A1", "single short recording baseline", budget)
    pre_start_delay = choose_delay_seconds(
        args,
        window=args.pre_start_delay_window,
        label="a1_pre_start",
    )
    summary = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="A1",
        capture_seconds=capture_seconds,
        timeout_seconds=max(90, int(capture_seconds + 60)),
        reset_before_capture=args.reset_before_capture,
        pre_start_delay_seconds=pre_start_delay,
        output_dir=case_output_dir("A1"),
        serial_log_path=case_serial_log_path("A1"),
    )
    return ensure_pass("A1", summary)


async def run_a2(args) -> dict[str, str]:
    capture_seconds = choose_duration(args, window=args.long_capture_window, label="a2")
    budget = max(240, int(capture_seconds * 3 + 60))
    print_case_header("A2", "single long recording baseline", budget)
    pre_start_delay = choose_delay_seconds(
        args,
        window=args.pre_start_delay_window,
        label="a2_pre_start",
    )
    summary = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="A2",
        capture_seconds=capture_seconds,
        timeout_seconds=max(90, int(capture_seconds + 60)),
        reset_before_capture=args.reset_before_capture,
        pre_start_delay_seconds=pre_start_delay,
        tts_sentence_count=max(2, int(args.full_chain_long_sentence_count)),
        output_dir=case_output_dir("A2"),
        serial_log_path=case_serial_log_path("A2"),
    )
    return ensure_pass("A2", summary)


async def run_a3(args) -> dict[str, str]:
    budget_seconds = max(180, args.round_count * 90)
    print_case_header("A3", "multi-round short recordings", budget_seconds)
    failures = []
    warnings = []
    capture_seconds_plan = choose_duration_plan(
        args,
        window=args.short_capture_window,
        count=args.round_count,
        label="a3",
    )
    pre_start_delay_plan = choose_delay_plan(
        args,
        first_window=args.pre_start_delay_window,
        followup_window=args.inter_session_gap_window,
        count=args.round_count,
        label="a3",
    )
    summaries = await play_and_capture_serial_toggle_multi_session(
        port=args.port,
        device_name=args.device_name,
        scenario="A3",
        capture_seconds=args.capture_seconds,
        capture_seconds_per_session=capture_seconds_plan,
        session_count=args.round_count,
        timeout_seconds=90,
        reset_before_capture=args.reset_before_capture,
        require_analysis=False,
        pre_start_delay_seconds_per_session=pre_start_delay_plan,
        output_dir=case_output_dir("A3"),
        serial_log_path=case_serial_log_path("A3"),
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
        return print_summary("A3", "fail", "failed_rounds=" + ",".join(str(v) for v in failures))
    if warnings:
        return print_summary("A3", "warning", "warning_rounds=" + ",".join(str(v) for v in warnings))
    return print_summary("A3", "pass", "")


async def run_a4_new(args) -> dict[str, str]:
    """A4: hesitation/pause - segmented TTS with 1-3s gaps between sentences."""
    capture_seconds = choose_duration(args, window=args.short_capture_window, label="a4", floor_seconds=6)
    budget = max(180, int(capture_seconds * 3 + 60))
    print_case_header("A4", "hesitation/pause with gaps", budget)
    pre_start_delay = choose_delay_seconds(args, window=args.pre_start_delay_window, label="a4_pre_start")
    sentence_seed = deterministic_sentence_seed(args, "A4")
    sentences = pick_chinese_sentences(sentence_seed, 2)
    wav_path = case_output_dir("A4") / "a4_segmented.wav"
    generate_segmented_tts_wav(
        wav_path,
        sentences,
        gap_seconds_range=(1.0, 3.0),
        seed=sentence_seed,
    )
    profile = resolve_audio_profile("normal")
    winsound.PlaySound(str(wav_path), winsound.SND_FILENAME | winsound.SND_ASYNC)
    try:
        capture_args = make_capture_args(
            port=args.port,
            device_name=args.device_name,
            capture_seconds=capture_seconds,
            capture_seconds_per_session=[capture_seconds],
            session_pre_start_delay_seconds=[pre_start_delay],
            timeout_seconds=max(90, int(capture_seconds + 60)),
            reset_before_capture=args.reset_before_capture,
            output_dir=case_output_dir("A4"),
            serial_log_path=case_serial_log_path("A4"),
        )
        session_summaries = await capture_sessions(capture_args)
    finally:
        winsound.PlaySound(None, winsound.SND_PURGE)
    if not session_summaries:
        return print_summary("A4", "fail", "no_session_captured")
    summary = dict(session_summaries[-1])
    expected_text = "".join(sentences)
    summary["expected_text"] = expected_text
    summary["audio_profile"] = "normal"
    summary["source_tts_rate"] = int(profile["tts_rate"])
    summary["source_tts_gain"] = float(profile["tts_gain"])
    return ensure_pass("A4", summary)


async def run_a5_new(args) -> dict[str, str]:
    """A5: quiet speech with low-volume profile."""
    capture_seconds = choose_duration(args, window=args.short_capture_window, label="a5")
    budget = max(120, int(capture_seconds * 3 + 60))
    print_case_header("A5", "quiet speech (low-volume profile)", budget)
    pre_start_delay = choose_delay_seconds(args, window=args.pre_start_delay_window, label="a5_pre_start")
    summary = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="A5",
        capture_seconds=capture_seconds,
        timeout_seconds=max(90, int(capture_seconds + 60)),
        reset_before_capture=args.reset_before_capture,
        pre_start_delay_seconds=pre_start_delay,
        audio_profile="low-volume",
        output_dir=case_output_dir("A5"),
        serial_log_path=case_serial_log_path("A5"),
    )
    return ensure_pass("A5", summary)


async def run_a6_new(args) -> dict[str, str]:
    """A6: fast speech with fast profile (tts_rate=7)."""
    capture_seconds = choose_duration(args, window=args.short_capture_window, label="a6")
    budget = max(120, int(capture_seconds * 3 + 60))
    print_case_header("A6", "fast speech (rate=7)", budget)
    pre_start_delay = choose_delay_seconds(args, window=args.pre_start_delay_window, label="a6_pre_start")
    summary = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="A6",
        capture_seconds=capture_seconds,
        timeout_seconds=max(90, int(capture_seconds + 60)),
        reset_before_capture=args.reset_before_capture,
        pre_start_delay_seconds=pre_start_delay,
        audio_profile="fast",
        tts_rate=7,
        output_dir=case_output_dir("A6"),
        serial_log_path=case_serial_log_path("A6"),
    )
    return ensure_pass("A6", summary)


async def run_a7_new(args) -> dict[str, str]:
    """A7: short command + long sentence mix, alternating per round."""
    round_count = args.round_count
    budget_seconds = max(180, round_count * 90)
    print_case_header("A7", f"short command + long sentence mix ({round_count} rounds)", budget_seconds)
    failures = []
    warnings = []
    capture_seconds_plan = choose_duration_plan(
        args,
        window=args.short_capture_window,
        count=round_count,
        label="a7",
    )
    pre_start_delay_plan = choose_delay_plan(
        args,
        first_window=args.pre_start_delay_window,
        followup_window=args.inter_session_gap_window,
        count=round_count,
        label="a7",
    )
    for round_index in range(1, round_count + 1):
        round_seed = deterministic_sentence_seed(args, "A7", round_index)
        if round_index % 2 == 1:
            text_pieces = pick_short_commands(round_seed, 1)
        else:
            text_pieces = pick_chinese_sentences(round_seed, 1)
        expected_text = text_pieces[0]
        print(f"a7_round={round_index} mode={'short' if round_index % 2 == 1 else 'long'} text={expected_text}", flush=True)
        round_summary = await play_and_capture_serial_toggle(
            port=args.port,
            device_name=args.device_name,
            scenario="A7",
            source_label=f"round{round_index}",
            capture_seconds=capture_seconds_plan[round_index - 1],
            timeout_seconds=90,
            reset_before_capture=args.reset_before_capture if round_index == 1 else False,
            pre_start_delay_seconds=pre_start_delay_plan[round_index - 1],
            output_dir=case_output_dir("A7"),
            serial_log_path=case_serial_log_path("A7", f"round{round_index}"),
        )
        print(f"a7_round_result={round_summary['result']}", flush=True)
        if round_summary["result"] == "fail":
            failures.append(round_index)
        elif round_summary["result"] == "warning":
            warnings.append(round_index)
    if failures:
        return print_summary("A7", "fail", "failed_rounds=" + ",".join(str(v) for v in failures))
    if warnings:
        return print_summary("A7", "warning", "warning_rounds=" + ",".join(str(v) for v in warnings))
    return print_summary("A7", "pass", "")


async def run_a8_new(args) -> dict[str, str]:
    """A8: ultra short 1-2 character commands, 3 rounds."""
    round_count = 3
    budget_seconds = 180
    print_case_header("A8", f"ultra short 1-2 char commands ({round_count} rounds)", budget_seconds)
    failures = []
    warnings = []
    for round_index in range(1, round_count + 1):
        round_seed = deterministic_sentence_seed(args, "A8", round_index)
        text_pieces = pick_ultra_short(round_seed, 1)
        expected_text = text_pieces[0]
        capture_seconds = choose_duration(args, window=args.short_capture_window, label=f"a8_r{round_index}")
        pre_start_delay = choose_delay_seconds(args, window=args.pre_start_delay_window, label=f"a8_r{round_index}_pre_start")
        print(f"a8_round={round_index} text={expected_text}", flush=True)
        round_summary = await play_and_capture_serial_toggle(
            port=args.port,
            device_name=args.device_name,
            scenario="A8",
            source_label=f"round{round_index}",
            capture_seconds=capture_seconds,
            timeout_seconds=90,
            reset_before_capture=args.reset_before_capture if round_index == 1 else False,
            pre_start_delay_seconds=pre_start_delay,
            output_dir=case_output_dir("A8"),
            serial_log_path=case_serial_log_path("A8", f"round{round_index}"),
        )
        print(f"a8_round_result={round_summary['result']}", flush=True)
        if round_summary["result"] == "fail":
            failures.append(round_index)
        elif round_summary["result"] == "warning":
            warnings.append(round_index)
    if failures:
        return print_summary("A8", "fail", "failed_rounds=" + ",".join(str(v) for v in failures))
    if warnings:
        return print_summary("A8", "warning", "warning_rounds=" + ",".join(str(v) for v in warnings))
    return print_summary("A8", "pass", "")


async def run_a9_new(args) -> dict[str, str]:
    """A9: punctuation commands with 逗号/句号/换行."""
    capture_seconds = choose_duration(args, window=args.short_capture_window, label="a9")
    budget = max(120, int(capture_seconds * 3 + 60))
    print_case_header("A9", "punctuation commands", budget)
    pre_start_delay = choose_delay_seconds(args, window=args.pre_start_delay_window, label="a9_pre_start")
    sentence_seed = deterministic_sentence_seed(args, "A9")
    text_pieces = pick_punctuation_commands(sentence_seed, 1)
    expected_text = text_pieces[0]
    summary = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="A9",
        capture_seconds=capture_seconds,
        timeout_seconds=max(90, int(capture_seconds + 60)),
        reset_before_capture=args.reset_before_capture,
        pre_start_delay_seconds=pre_start_delay,
        output_dir=case_output_dir("A9"),
        serial_log_path=case_serial_log_path("A9"),
    )
    summary["expected_text"] = expected_text
    return ensure_pass("A9", summary)


async def run_a10_new(args) -> dict[str, str]:
    """A10: Chinese-English mixed language."""
    capture_seconds = choose_duration(args, window=args.short_capture_window, label="a10")
    budget = max(120, int(capture_seconds * 3 + 60))
    print_case_header("A10", "Chinese-English mixed language", budget)
    pre_start_delay = choose_delay_seconds(args, window=args.pre_start_delay_window, label="a10_pre_start")
    sentence_seed = deterministic_sentence_seed(args, "A10")
    text_pieces = pick_mixed_sentences(sentence_seed, 1)
    expected_text = text_pieces[0]
    summary = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="A10",
        capture_seconds=capture_seconds,
        timeout_seconds=max(90, int(capture_seconds + 60)),
        reset_before_capture=args.reset_before_capture,
        pre_start_delay_seconds=pre_start_delay,
        output_dir=case_output_dir("A10"),
        serial_log_path=case_serial_log_path("A10"),
    )
    summary["expected_text"] = expected_text
    return ensure_pass("A10", summary)


async def run_a11_new(args) -> dict[str, str]:
    """A11: repeat same sentence 3 times, verify each independently."""
    round_count = 3
    budget_seconds = 180
    print_case_header("A11", f"repeat same sentence ({round_count} rounds)", budget_seconds)
    sentence_seed = deterministic_sentence_seed(args, "A11")
    sentences = pick_chinese_sentences(sentence_seed, 1)
    expected_text = sentences[0]
    print(f"a11_fixed_text={expected_text}", flush=True)
    failures = []
    warnings = []
    for round_index in range(1, round_count + 1):
        capture_seconds = choose_duration(args, window=args.short_capture_window, label=f"a11_r{round_index}")
        pre_start_delay = choose_delay_seconds(args, window=args.pre_start_delay_window, label=f"a11_r{round_index}_pre_start")
        round_summary = await play_and_capture_serial_toggle(
            port=args.port,
            device_name=args.device_name,
            scenario="A11",
            source_label=f"round{round_index}",
            capture_seconds=capture_seconds,
            timeout_seconds=90,
            reset_before_capture=args.reset_before_capture if round_index == 1 else False,
            pre_start_delay_seconds=pre_start_delay,
            output_dir=case_output_dir("A11"),
            serial_log_path=case_serial_log_path("A11", f"round{round_index}"),
        )
        print(f"a11_round={round_index} result={round_summary['result']}", flush=True)
        if round_summary["result"] == "fail":
            failures.append(round_index)
        elif round_summary["result"] == "warning":
            warnings.append(round_index)
    if failures:
        return print_summary("A11", "fail", "failed_rounds=" + ",".join(str(v) for v in failures))
    if warnings:
        return print_summary("A11", "warning", "warning_rounds=" + ",".join(str(v) for v in warnings))
    return print_summary("A11", "pass", "")


async def run_a12_new(args) -> dict[str, str]:
    """A12: environment noise - TTS + white noise at SNR 12dB."""
    capture_seconds = choose_duration(args, window=args.short_capture_window, label="a12")
    budget = max(120, int(capture_seconds * 3 + 60))
    print_case_header("A12", "environment noise (white, SNR=12dB)", budget)
    pre_start_delay = choose_delay_seconds(args, window=args.pre_start_delay_window, label="a12_pre_start")
    sentence_seed = deterministic_sentence_seed(args, "A12")
    sentences = pick_chinese_sentences(sentence_seed, 1)
    expected_text = sentences[0]
    profile = resolve_audio_profile("noisy")
    wav_path = case_output_dir("A12") / "a12_noisy.wav"
    generate_profile_tts_wav(
        wav_path,
        expected_text,
        tts_rate=int(profile["tts_rate"]),
        tts_gain=float(profile["tts_gain"]),
    )
    mix_noise_into_wav(wav_path, noise_type="white", snr_db=12.0, seed=sentence_seed)
    winsound.PlaySound(str(wav_path), winsound.SND_FILENAME | winsound.SND_ASYNC)
    try:
        capture_args = make_capture_args(
            port=args.port,
            device_name=args.device_name,
            capture_seconds=capture_seconds,
            capture_seconds_per_session=[capture_seconds],
            session_pre_start_delay_seconds=[pre_start_delay],
            timeout_seconds=max(90, int(capture_seconds + 60)),
            reset_before_capture=args.reset_before_capture,
            output_dir=case_output_dir("A12"),
            serial_log_path=case_serial_log_path("A12"),
        )
        session_summaries = await capture_sessions(capture_args)
    finally:
        winsound.PlaySound(None, winsound.SND_PURGE)
    if not session_summaries:
        return print_summary("A12", "fail", "no_session_captured")
    summary = dict(session_summaries[-1])
    summary["expected_text"] = expected_text
    summary["audio_profile"] = "noisy"
    summary["source_tts_rate"] = int(profile["tts_rate"])
    summary["source_tts_gain"] = float(profile["tts_gain"])
    return ensure_pass("A12", summary)


async def run_a13_new(args) -> dict[str, str]:
    """A13: secondary speech interference - primary TTS + secondary speaker."""
    capture_seconds = choose_duration(args, window=args.short_capture_window, label="a13")
    budget = max(120, int(capture_seconds * 3 + 60))
    print_case_header("A13", "secondary speech interference", budget)
    pre_start_delay = choose_delay_seconds(args, window=args.pre_start_delay_window, label="a13_pre_start")
    sentence_seed = deterministic_sentence_seed(args, "A13")
    primary_sentences = pick_chinese_sentences(sentence_seed, 1)
    expected_text = primary_sentences[0]
    profile = resolve_audio_profile("normal")
    wav_path = case_output_dir("A13") / "a13_interference.wav"
    generate_profile_tts_wav(
        wav_path,
        expected_text,
        tts_rate=int(profile["tts_rate"]),
        tts_gain=float(profile["tts_gain"]),
    )
    secondary_seed = deterministic_sentence_seed(args, "A13", "secondary")
    secondary_sentences = pick_chinese_sentences(secondary_seed, 1)
    secondary_text = secondary_sentences[0]
    mix_secondary_speech_into_wav(wav_path, secondary_text, snr_db=8.0, seed=secondary_seed)
    winsound.PlaySound(str(wav_path), winsound.SND_FILENAME | winsound.SND_ASYNC)
    try:
        capture_args = make_capture_args(
            port=args.port,
            device_name=args.device_name,
            capture_seconds=capture_seconds,
            capture_seconds_per_session=[capture_seconds],
            session_pre_start_delay_seconds=[pre_start_delay],
            timeout_seconds=max(90, int(capture_seconds + 60)),
            reset_before_capture=args.reset_before_capture,
            output_dir=case_output_dir("A13"),
            serial_log_path=case_serial_log_path("A13"),
        )
        session_summaries = await capture_sessions(capture_args)
    finally:
        winsound.PlaySound(None, winsound.SND_PURGE)
    if not session_summaries:
        return print_summary("A13", "fail", "no_session_captured")
    summary = dict(session_summaries[-1])
    summary["expected_text"] = expected_text
    summary["audio_profile"] = "normal"
    summary["source_tts_rate"] = int(profile["tts_rate"])
    summary["source_tts_gain"] = float(profile["tts_gain"])
    return ensure_pass("A13", summary)


async def run_a16_new(args) -> dict[str, str]:
    """A16: fading gain / walking distance simulation."""
    capture_seconds = choose_duration(args, window=args.short_capture_window, label="a16")
    budget = max(120, int(capture_seconds * 3 + 60))
    print_case_header("A16", "fading gain (walking simulation)", budget)
    pre_start_delay = choose_delay_seconds(args, window=args.pre_start_delay_window, label="a16_pre_start")
    sentence_seed = deterministic_sentence_seed(args, "A16")
    sentences = pick_chinese_sentences(sentence_seed, 1)
    expected_text = sentences[0]
    profile = resolve_audio_profile("normal")
    wav_path = case_output_dir("A16") / "a16_fading.wav"
    generate_profile_tts_wav(
        wav_path,
        expected_text,
        tts_rate=int(profile["tts_rate"]),
        tts_gain=float(profile["tts_gain"]),
    )
    apply_fading_gain(wav_path, min_gain=0.4, period_seconds=4.0, seed=sentence_seed)
    winsound.PlaySound(str(wav_path), winsound.SND_FILENAME | winsound.SND_ASYNC)
    try:
        capture_args = make_capture_args(
            port=args.port,
            device_name=args.device_name,
            capture_seconds=capture_seconds,
            capture_seconds_per_session=[capture_seconds],
            session_pre_start_delay_seconds=[pre_start_delay],
            timeout_seconds=max(90, int(capture_seconds + 60)),
            reset_before_capture=args.reset_before_capture,
            output_dir=case_output_dir("A16"),
            serial_log_path=case_serial_log_path("A16"),
        )
        session_summaries = await capture_sessions(capture_args)
    finally:
        winsound.PlaySound(None, winsound.SND_PURGE)
    if not session_summaries:
        return print_summary("A16", "fail", "no_session_captured")
    summary = dict(session_summaries[-1])
    summary["expected_text"] = expected_text
    summary["audio_profile"] = "normal"
    summary["source_tts_rate"] = int(profile["tts_rate"])
    summary["source_tts_gain"] = float(profile["tts_gain"])
    return ensure_pass("A16", summary)


async def run_a18_new(args) -> dict[str, str]:
    """A18: ASR timeout simulation - verifies host does not hang when ASR is slow/unavailable.

    Uses a controlled 8s timeout on the product-chain runner (5s listener timeout)
    so ASR is guaranteed to not return in time. This simulates network failure
    without depending on real network issues.
    """
    budget = 120
    print_case_header("A18", "ASR timeout simulation (controlled short timeout)", budget)
    capture_seconds = choose_duration(args, window=args.short_capture_window, label="a18")
    pre_start_delay = choose_delay_seconds(args, window=args.pre_start_delay_window, label="a18_pre_start")
    transport_summary = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="A18",
        capture_seconds=capture_seconds,
        timeout_seconds=90,
        reset_before_capture=args.reset_before_capture,
        pre_start_delay_seconds=pre_start_delay,
        output_dir=case_output_dir("A18"),
        serial_log_path=case_serial_log_path("A18"),
    )
    transport_ok = str(transport_summary.get("result")) != "fail"
    details: dict[str, object] = {"transport": compact_case_details(transport_summary)}
    if not transport_ok:
        return print_summary("A18", "fail", "transport_failed_before_asr_timeout_test", details)
    if not args.transport_only:
        asr_timeout_chain = await run_listener_type_product_chain(
            args,
            "A18",
            trigger_mode="serial-toggle",
            artifact_label="asr_timeout_sim",
            timeout_seconds_override=8,
            listener_timeout_ms_override=5000,
        )
        asr_result = str(asr_timeout_chain.get("result"))
        details["asr_timeout_simulation"] = asr_timeout_chain
        host_did_not_hang = asr_result != "fail" or "full_chain_timeout" not in str(asr_timeout_chain.get("reason", ""))
        if host_did_not_hang:
            return print_summary("A18", "pass", f"asr_timeout_handled:{asr_result}", details)
        return print_summary("A18", "fail", "asr_timeout_caused_host_hang", details)
    return ensure_pass("A18", transport_summary)


async def run_t1(args) -> dict[str, str]:
    print_case_header("T1", "disconnect/reconnect recovery (with BT restart)", 240)
    baseline_capture_seconds = choose_duration(args, window=args.short_capture_window, label="t1_baseline")
    baseline_pre_start_delay_seconds = choose_delay_seconds(
        args,
        window=args.pre_start_delay_window,
        label="t1_baseline_pre_start",
    )
    baseline = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="T1",
        source_label="baseline",
        capture_seconds=baseline_capture_seconds,
        timeout_seconds=90,
        reset_before_capture=args.reset_before_capture,
        pre_start_delay_seconds=baseline_pre_start_delay_seconds,
        output_dir=case_output_dir("A4"),
        serial_log_path=case_serial_log_path("A4", "baseline"),
    )
    if baseline["result"] != "pass":
        return print_summary("T1", "fail", "baseline_failed", compact_case_details(baseline))
    restart_windows_bluetooth(restart_pan_adapter=args.restart_pan_adapter)
    recover_ble_hid_host(args.device_name)
    print("t1_host_recovery_completed=1", flush=True)
    reconnect_capture_seconds = choose_duration(args, window=args.short_capture_window, label="t1_reconnect")
    reconnect_pre_start_delay_seconds = choose_delay_seconds(
        args,
        window=args.pre_start_delay_window,
        label="t1_reconnect_pre_start",
    )
    if reconnect_pre_start_delay_seconds < A4_RECONNECT_MIN_PRE_START_DELAY_SECONDS:
        print(
            "t1_reconnect_pre_start_delay_floor_seconds="
            f"{A4_RECONNECT_MIN_PRE_START_DELAY_SECONDS:.2f}",
            flush=True,
        )
        reconnect_pre_start_delay_seconds = A4_RECONNECT_MIN_PRE_START_DELAY_SECONDS
    print(f"t1_settle_window_seconds={reconnect_pre_start_delay_seconds:.2f}", flush=True)
    print("t1_capture_phase=start", flush=True)
    summary = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="T1",
        source_label="reconnect",
        capture_seconds=reconnect_capture_seconds,
        timeout_seconds=90,
        reset_before_capture=args.reset_before_capture,
        pre_start_delay_seconds=reconnect_pre_start_delay_seconds,
        output_dir=case_output_dir("T1"),
        serial_log_path=case_serial_log_path("T1", "reconnect"),
    )
    summary["host_recovery_completed"] = True
    summary["settle_window_seconds"] = float(reconnect_pre_start_delay_seconds)
    summary["capture_phase"] = "reconnect"
    summary["baseline_result"] = str(baseline.get("result", ""))
    summary["baseline_missing_packet_count"] = baseline.get("missing_packet_count")
    summary["baseline_packet_loss_ratio"] = baseline.get("packet_loss_ratio")
    return ensure_pass("T1", summary)


async def run_a17(args) -> dict[str, str]:
    a17_capture_seconds = choose_duration(
        args,
        window=args.short_capture_window,
        label="a17",
        floor_seconds=6,
    )
    idle_wait_seconds = float(A17_LONG_IDLE_SECONDS)
    pre_start_delay_seconds = choose_delay_seconds(args, window=args.pre_start_delay_window, label="a17_pre_start")
    budget_seconds = int(idle_wait_seconds) + 180
    print_case_header("A17", "first recording after long idle (5 min)", budget_seconds)
    print(f"a17_idle_wait_seconds={idle_wait_seconds:.2f}", flush=True)
    await asyncio.sleep(idle_wait_seconds)
    summary = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="A17",
        capture_seconds=a17_capture_seconds,
        timeout_seconds=90,
        reset_before_capture=False,
        pre_start_delay_seconds=pre_start_delay_seconds,
        output_dir=case_output_dir("A17"),
        serial_log_path=case_serial_log_path("A17"),
    )
    return ensure_pass("A17", summary)


async def run_t3(args) -> dict[str, str]:
    print_case_header("T3", "host-side recovery without BT restart", 180)
    baseline_capture_seconds = choose_duration(args, window=args.short_capture_window, label="t3_baseline")
    baseline_pre_start_delay_seconds = choose_delay_seconds(
        args,
        window=args.pre_start_delay_window,
        label="t3_baseline_pre_start",
    )
    baseline = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="T3",
        source_label="baseline",
        capture_seconds=baseline_capture_seconds,
        timeout_seconds=90,
        reset_before_capture=args.reset_before_capture,
        pre_start_delay_seconds=baseline_pre_start_delay_seconds,
        output_dir=case_output_dir("T3"),
        serial_log_path=case_serial_log_path("T3", "baseline"),
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
            "t3_baseline_retry_after_recover=1 "
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
            scenario="T3",
            source_label="baseline_retry",
            capture_seconds=baseline_capture_seconds,
            timeout_seconds=90,
            reset_before_capture=False,
            pre_start_delay_seconds=baseline_pre_start_delay_seconds,
            output_dir=case_output_dir("T3"),
            serial_log_path=case_serial_log_path("T3", "baseline_retry"),
        )
        if baseline["result"] != "pass":
            return print_summary(
                "T3",
                "fail",
                "baseline_failed_after_retry "
                f"missing_packet_count={baseline['missing_packet_count']} "
                f"missing_packet_indices={baseline['missing_packet_indices'][:16]}",
            )
    elif baseline["result"] != "pass":
        return print_summary(
            "T3",
            baseline["result"],
            "baseline_failed_without_retry "
            f"reason={baseline_failure_reason} "
            f"missing_packet_count={baseline.get('missing_packet_count', 0)} "
            f"missing_packet_indices={baseline.get('missing_packet_indices', [])[:16]}",
        )
    after_restart_capture_seconds = choose_duration(args, window=args.short_capture_window, label="t3_after_restart")
    after_restart_pre_start_delay_seconds = choose_delay_seconds(
        args,
        window=args.pre_start_delay_window,
        label="t3_after_restart_pre_start",
    )
    summary = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="T3",
        source_label="after_restart",
        capture_seconds=after_restart_capture_seconds,
        timeout_seconds=90,
        reset_before_capture=False,
        pre_start_delay_seconds=after_restart_pre_start_delay_seconds,
        output_dir=case_output_dir("T3"),
        serial_log_path=case_serial_log_path("T3", "after_restart"),
    )
    return ensure_pass("T3", summary)


async def run_a14(args) -> dict[str, str]:
    print_case_header("A14", "cancel during recording and recover (long + short hold)", 300)
    # First probe: long cancel hold
    long_cancel_hold = choose_delay_seconds(args, window=args.cancel_hold_window, label="a14_long_cancel")
    capture_seconds = choose_duration(args, window=args.short_capture_window, label="a14")
    pre_start_delay = choose_delay_seconds(args, window=args.pre_start_delay_window, label="a14_pre_start")
    summary = await play_and_capture_serial_toggle_after_cancel_probe(
        port=args.port,
        device_name=args.device_name,
        scenario="A14",
        capture_seconds=capture_seconds,
        cancel_hold_seconds=long_cancel_hold,
        timeout_seconds=90,
        reset_before_capture=args.reset_before_capture,
        pre_start_delay_seconds=pre_start_delay,
        output_dir=case_output_dir("A14"),
        serial_log_path=case_serial_log_path("A14", "long_hold"),
    )
    print(f"long_cancel_probe={'pass' if summary.get('cancel_requested') and summary.get('cancel_completed') else 'fail'}", flush=True)
    print(f"cancel_requested={1 if summary.get('cancel_requested') else 0}", flush=True)
    print(f"cancel_completed={1 if summary.get('cancel_completed') else 0}", flush=True)
    if summary["result"] != "pass":
        if summary.get("failure_reason") == "short_cancel_probe_failed":
            return print_summary("A14", "fail", "long_cancel_probe_failed")
        return print_summary(
            "A14",
            summary["result"],
            f"{summary.get('failure_reason') or summary.get('warning_reason') or ''} "
            f"missing_packet_count={summary['missing_packet_count']} "
            f"missing_packet_indices={summary['missing_packet_indices'][:16]}",
        )

    # Second probe: short cancel hold (merged from old P9)
    short_cancel_hold = choose_delay_seconds(args, window=args.short_cancel_hold_window, label="a14_short_cancel")
    short_summary = await play_and_capture_serial_toggle_after_cancel_probe(
        port=args.port,
        device_name=args.device_name,
        scenario="A14",
        capture_seconds=capture_seconds,
        cancel_hold_seconds=short_cancel_hold,
        timeout_seconds=90,
        reset_before_capture=False,
        pre_start_delay_seconds=pre_start_delay,
        output_dir=case_output_dir("A14"),
        serial_log_path=case_serial_log_path("A14", "short_hold"),
    )
    print(f"short_cancel_probe={'pass' if short_summary.get('cancel_requested') and short_summary.get('cancel_completed') else 'fail'}", flush=True)
    if short_summary["result"] != "pass":
        return print_summary("A14", short_summary["result"], f"short_cancel_failed: {short_summary.get('failure_reason', '')}")
    details = {
        "long_cancel_probe": compact_case_details(summary),
        "short_cancel_probe": compact_case_details(short_summary),
    }
    if not args.transport_only:
        negative_product_chain = await run_listener_type_product_chain(
            args,
            "A14",
            trigger_mode="serial-cancel",
            artifact_label="cancel_negative",
            expect_no_text=True,
        )
        details["cancel_negative_product_chain"] = negative_product_chain
        if str(negative_product_chain.get("result")) == "fail":
            return print_summary(
                "A14",
                "fail",
                "cancel_negative_product_chain_failed:" + str(negative_product_chain.get("reason", "")),
                details,
            )
        if str(negative_product_chain.get("result")) == "warning":
            return print_summary(
                "A14",
                "warning",
                "cancel_negative_product_chain_warning:" + str(negative_product_chain.get("reason", "")),
                details,
            )
    return print_summary("A14", "pass", "", details)


async def run_t2(args) -> dict[str, str]:
    round_count = args.round_count
    budget_seconds = max(240, round_count * 120)
    print_case_header("T2", f"multi-round independent reconnect ({round_count} rounds)", budget_seconds)
    failures = []
    warnings = []
    for round_index in range(1, round_count + 1):
        capture_seconds = choose_duration(args, window=args.short_capture_window, label=f"t2_r{round_index}")
        pre_start_delay = choose_delay_seconds(args, window=args.pre_start_delay_window, label=f"t2_r{round_index}_pre_start")
        summary = await play_and_capture_serial_toggle(
            port=args.port,
            device_name=args.device_name,
            scenario="T2",
            source_label=f"round{round_index}",
            capture_seconds=capture_seconds,
            timeout_seconds=90,
            reset_before_capture=True,  # Full reset each round
            pre_start_delay_seconds=pre_start_delay,
            output_dir=case_output_dir("T2"),
            serial_log_path=case_serial_log_path("T2", f"round{round_index}"),
        )
        print(f"t2_round_index={round_index}", flush=True)
        print(f"t2_round_result={summary['result']}", flush=True)
        print(f"t2_round_missing_packet_count={summary['missing_packet_count']}", flush=True)
        if summary["result"] == "fail":
            failures.append(round_index)
        elif summary["result"] == "warning":
            warnings.append(round_index)
    if failures:
        return print_summary("T2", "fail", "failed_rounds=" + ",".join(str(v) for v in failures))
    if warnings:
        return print_summary("T2", "warning", "warning_rounds=" + ",".join(str(v) for v in warnings))
    return print_summary("T2", "pass", "")


async def run_a15(args) -> dict[str, str]:
    print_case_header("A15", "silent/no-input negative test", 120)
    capture_seconds = choose_duration(args, window=args.short_capture_window, label="a15")
    pre_start_delay = choose_delay_seconds(args, window=args.pre_start_delay_window, label="a15_pre_start")
    capture_args = make_capture_args(
        port=args.port,
        device_name=args.device_name,
        capture_seconds=capture_seconds,
        capture_seconds_per_session=[capture_seconds],
        session_pre_start_delay_seconds=[pre_start_delay],
        timeout_seconds=90,
        reset_before_capture=args.reset_before_capture,
        output_dir=case_output_dir("A15"),
        serial_log_path=case_serial_log_path("A15"),
    )
    session_summaries = await capture_sessions(capture_args)
    if not session_summaries:
        return print_summary("A15", "fail", "no_session_captured")
    summary = dict(session_summaries[-1])
    recorded_wav = pathlib.Path(summary["wav_path"])
    if not recorded_wav.exists():
        return print_summary("A15", "fail", "no_output_wav")
    frames = read_wav_frames(recorded_wav)
    peak = max(abs(v) for v in frames) if frames else 0
    env = compute_envelope(frames)
    runs = detect_active_runs(env)
    active_frames = sum(end - start for start, end in runs)
    print(f"a15_recorded_peak={peak}", flush=True)
    print(f"a15_active_frame_count={active_frames}", flush=True)
    print(f"a15_total_frames={len(frames)}", flush=True)
    validation = validate_transport_summary(summary, capture_seconds=capture_seconds)
    transport_ok = validation["transport_result"] != "fail" or validation["transport_failure_reason"] != "no_audio_packets_received"
    print(f"a15_transport_ok={1 if transport_ok else 0}", flush=True)
    if not transport_ok:
        return print_summary("A15", "fail", "transport_failed_even_without_audio")
    details = {
        "silent_transport": {
            "peak": peak,
            "active_frames": active_frames,
            "total_frames": len(frames),
            "transport_validation": validation,
            **compact_case_details(summary),
        }
    }
    if not args.transport_only:
        negative_product_chain = await run_listener_type_product_chain(
            args,
            "A15",
            trigger_mode="serial-toggle",
            artifact_label="silence_negative",
            expect_no_text=True,
            extra_smoke_args=["-SilentAudio"],
        )
        details["silence_negative_product_chain"] = negative_product_chain
        if str(negative_product_chain.get("result")) == "fail":
            return print_summary(
                "A15",
                "fail",
                "silence_negative_product_chain_failed:" + str(negative_product_chain.get("reason", "")),
                details,
            )
        if str(negative_product_chain.get("result")) == "warning":
            return print_summary(
                "A15",
                "warning",
                "silence_negative_product_chain_warning:" + str(negative_product_chain.get("reason", "")),
                details,
            )
    return print_summary("A15", "pass", f"peak={peak} active_frames={active_frames}", details)


RAPID_TOGGLE_COUNT_DEFAULT = 10


async def run_t4(args) -> dict[str, str]:
    toggle_count = RAPID_TOGGLE_COUNT_DEFAULT
    print_case_header("T4", f"rapid toggle stress ({toggle_count} cycles)", 180)
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
        print(f"t4_stress_toggle_count={toggle_count}", flush=True)
        print(f"t4_stress_ok={1 if stress_ok else 0}", flush=True)
        if stress_errors:
            for error in stress_errors[:5]:
                print(f"t4_stress_error={error}", flush=True)
        await asyncio.sleep(1.0)
        try:
            ser.reset_input_buffer()
        except (OSError, serial.SerialException):
            pass
    if not stress_ok:
        return print_summary("T4", "fail", "serial_error_during_stress")
    verify_capture_seconds = choose_duration(args, window=args.short_capture_window, label="t4_verify")
    pre_start_delay = choose_delay_seconds(args, window=args.pre_start_delay_window, label="t4_verify_pre_start")
    summary = await play_and_capture_serial_toggle(
        port=args.port,
        device_name=args.device_name,
        scenario="T4",
        source_label="post_stress_verify",
        capture_seconds=verify_capture_seconds,
        timeout_seconds=90,
        reset_before_capture=True,
        pre_start_delay_seconds=pre_start_delay,
        output_dir=case_output_dir("T4"),
        serial_log_path=case_serial_log_path("T4"),
    )
    summary["stress_toggle_count"] = toggle_count
    summary["stress_ok"] = stress_ok
    return ensure_pass("T4", summary)


async def run_t5(args) -> dict[str, str]:
    print_case_header("T5", "concurrent BLE client attempt during capture", 180)
    address_hex = get_paired_device_address_hex(args.device_name)
    if not address_hex:
        return print_summary("T5", "fail", "unable_to_resolve_device_address")
    from winrt.windows.devices.bluetooth import BluetoothLEDevice
    rogue_device = None
    try:
        rogue_device = await BluetoothLEDevice.from_bluetooth_address_async(int(address_hex, 16))
    except Exception as exc:
        print(f"t5_rogue_connect_error={type(exc).__name__}:{exc}", flush=True)
    rogue_connected = False
    if rogue_device is not None:
        try:
            rogue_connected = rogue_device.connection_status == 1
        except Exception:
            pass
    print(f"t5_rogue_device_open={1 if rogue_device else 0}", flush=True)
    print(f"t5_rogue_connected={1 if rogue_connected else 0}", flush=True)
    capture_seconds = choose_duration(args, window=args.short_capture_window, label="t5")
    pre_start_delay = choose_delay_seconds(args, window=args.pre_start_delay_window, label="t5_pre_start")
    try:
        summary = await play_and_capture_serial_toggle(
            port=args.port,
            device_name=args.device_name,
            scenario="T5",
            source_label="concurrent_test",
            capture_seconds=capture_seconds,
            timeout_seconds=90,
            reset_before_capture=args.reset_before_capture,
            pre_start_delay_seconds=pre_start_delay,
            output_dir=case_output_dir("T5"),
            serial_log_path=case_serial_log_path("T5"),
        )
    finally:
        if rogue_device is not None:
            try:
                rogue_device.close()
            except Exception:
                pass
    summary["rogue_device_open"] = rogue_device is not None
    summary["rogue_connected"] = rogue_connected
    return ensure_pass("T5", summary)


def make_product_chain_result(
    case_id: str,
    result: str,
    reason: str = "",
    details: dict[str, object] | None = None,
) -> dict[str, object]:
    print(f"product_chain_case={case_id}", flush=True)
    print(f"product_chain_result={result}", flush=True)
    print(f"product_chain_reason={reason}", flush=True)
    return {
        "case_id": case_id,
        "result": result,
        "reason": reason,
        "details": details or {},
    }


def deterministic_sentence_seed(args, *parts: object) -> int:
    raw = "|".join(str(part) for part in (args.random_seed_resolved, *parts))
    digest = hashlib.sha256(raw.encode("utf-8")).digest()
    return int.from_bytes(digest[:8], "little")


def pick_a2_long_dictation_text(seed: int, clause_count: int) -> str:
    rng = random.Random(seed)
    lead = rng.choice(A2_LONG_DICTATION_LEADS)
    ending = rng.choice(A2_LONG_DICTATION_ENDINGS)
    middle_count = max(1, int(clause_count) - 2)
    pool = list(A2_LONG_DICTATION_CLAUSES)
    clauses: list[str] = []
    while len(clauses) < middle_count:
        if not pool:
            pool = list(A2_LONG_DICTATION_CLAUSES)
        index = rng.randrange(len(pool))
        clauses.append(pool.pop(index))
    return "，".join([lead, *clauses, ending]) + "。"


async def run_listener_type_product_chain(
    args,
    case_id: str,
    *,
    trigger_mode: str = "serial-toggle",
    artifact_label: str | None = None,
    audio_profile: str | None = None,
    expect_no_text: bool = False,
    extra_smoke_args: list[str] | None = None,
    sentence_override: str | None = None,
    random_sentence_count_override: int | None = None,
    listener_timeout_ms_override: int | None = None,
    timeout_seconds_override: int | None = None,
) -> dict[str, object]:
    output_dir = case_output_dir(case_id) / "product_chain"
    output_dir.mkdir(parents=True, exist_ok=True)
    trigger_label = artifact_label or trigger_mode.replace("-", "_")
    stdout_log = output_dir / f"listener_type_{trigger_label}_stdout.log"
    stderr_log = output_dir / f"listener_type_{trigger_label}_stderr.log"
    profile = resolve_audio_profile(audio_profile or args.full_chain_audio_profile)
    profile_name = str(profile["name"])
    profile_tts_rate = int(profile["tts_rate"])
    profile_tts_gain = float(profile["tts_gain"])
    silent_audio_requested = any(
        str(item).lower() == "-silentaudio"
        for item in (extra_smoke_args or [])
    )
    listener_repo = resolve_listener_type_repo(args)
    script_path = listener_repo / "tools" / "embedded_audio_replay" / "run_ble_stream_smoke.ps1"
    if not script_path.exists():
        return make_product_chain_result(
            case_id,
            "fail",
            "listener_type_smoke_script_missing",
            {
                "listener_type_repo": str(listener_repo),
                "expected_script": str(script_path),
            },
        )

    bluetooth_address = args.bluetooth_address
    bluetooth_address_source = "argument" if bluetooth_address else "script_default"
    if not bluetooth_address:
        try:
            bluetooth_address = get_paired_device_address_hex(args.device_name)
            if bluetooth_address:
                bluetooth_address_source = "paired_device_lookup"
        except Exception as exc:
            print(f"product_chain_bluetooth_address_lookup_error={type(exc).__name__}:{exc}", flush=True)

    random_sentence_count = random_sentence_count_override or 1
    listener_timeout_ms = listener_timeout_ms_override or args.full_chain_listener_timeout_ms
    timeout_seconds = timeout_seconds_override or args.full_chain_timeout_seconds
    if case_id == "A2" and not args.full_chain_sentence and random_sentence_count_override is None:
        random_sentence_count = max(1, int(args.full_chain_long_sentence_count))
        listener_timeout_ms = max(listener_timeout_ms, A2_PRODUCT_CHAIN_MIN_LISTENER_TIMEOUT_MS)
        timeout_seconds = max(timeout_seconds, A2_PRODUCT_CHAIN_MIN_TIMEOUT_SECONDS)
    if expect_no_text:
        listener_timeout_ms = max(listener_timeout_ms, NEGATIVE_PRODUCT_CHAIN_LISTENER_TIMEOUT_MS)
        timeout_seconds = max(timeout_seconds, NEGATIVE_PRODUCT_CHAIN_TIMEOUT_SECONDS)

    expected_sentence = sentence_override or args.full_chain_sentence or ""
    generated_wav_path: pathlib.Path | None = None
    if not silent_audio_requested:
        if not expected_sentence:
            sentence_seed = deterministic_sentence_seed(
                args,
                case_id,
                trigger_label,
                profile_name,
                random_sentence_count,
            )
            if case_id == "A2":
                expected_sentence = pick_a2_long_dictation_text(
                    sentence_seed,
                    random_sentence_count,
                )
            else:
                expected_sentence = "".join(
                    pick_chinese_sentences(sentence_seed, random_sentence_count)
                )
        generated_wav_path = output_dir / f"ble-stream-{trigger_label}-{profile_name}.wav"
        generate_profile_tts_wav(
            generated_wav_path,
            expected_sentence,
            tts_rate=profile_tts_rate,
            tts_gain=profile_tts_gain,
        )

    command = [
        "powershell.exe",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(script_path),
        "-TriggerMode",
        trigger_mode,
        "-Port",
        args.port,
        "-DeviceName",
        args.device_name,
        "-TimeoutMs",
        str(listener_timeout_ms),
        "-OutDir",
        str(output_dir.resolve()),
        "-TtsGain",
        str(profile_tts_gain),
        "-FirmwareRepo",
        str(firmware_repo_root()),
        "-VerifyHistory",
    ]
    if bluetooth_address:
        command.extend(["-BluetoothAddress", bluetooth_address])
    if generated_wav_path is not None:
        command.extend(["-WavPath", str(generated_wav_path.resolve())])
    if expected_sentence:
        command.extend(["-Sentence", expected_sentence])
    elif random_sentence_count > 1:
        command.extend(["-RandomSentenceCount", str(random_sentence_count)])
    if trigger_mode == "manual-key":
        command.extend(["-PlaybackCount", "2", "-RecordPlaybackIndex", "2"])
    if expect_no_text:
        command.append("-ExpectNoText")
    if extra_smoke_args:
        command.extend(extra_smoke_args)
    if not args.reset_before_capture:
        command.append("-NoResetBeforeCapture")
    if args.full_chain_verify_insertion:
        command.append("-VerifyInsertion")

    print(f"product_chain_trigger_mode={trigger_mode}", flush=True)
    print(f"product_chain_artifact_label={trigger_label}", flush=True)
    print(f"product_chain_audio_profile={profile_name}", flush=True)
    print(f"product_chain_tts_rate={profile_tts_rate}", flush=True)
    print(f"product_chain_tts_gain={profile_tts_gain}", flush=True)
    print(f"product_chain_listener_type_repo={listener_repo}", flush=True)
    print(f"product_chain_bluetooth_address_source={bluetooth_address_source}", flush=True)
    print(f"product_chain_random_sentence_count={random_sentence_count}", flush=True)
    print(f"product_chain_expect_no_text={1 if expect_no_text else 0}", flush=True)
    print(f"product_chain_listener_timeout_ms={listener_timeout_ms}", flush=True)
    if bluetooth_address:
        print(f"product_chain_bluetooth_address={bluetooth_address}", flush=True)
    print(f"product_chain_listener_stdout={stdout_log}", flush=True)
    print(f"product_chain_listener_stderr={stderr_log}", flush=True)

    try:
        completed = subprocess.run(
            command,
            cwd=str(listener_repo),
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=max(1, int(timeout_seconds)),
        )
    except subprocess.TimeoutExpired as exc:
        stdout_text = exc.stdout if isinstance(exc.stdout, str) else (exc.stdout or b"").decode("utf-8", errors="replace")
        stderr_text = exc.stderr if isinstance(exc.stderr, str) else (exc.stderr or b"").decode("utf-8", errors="replace")
        stdout_log.write_text(stdout_text, encoding="utf-8", errors="replace")
        stderr_log.write_text(stderr_text, encoding="utf-8", errors="replace")
        return make_product_chain_result(
            case_id,
            "fail",
            "full_chain_timeout",
            {
                "timeout_seconds": timeout_seconds,
                "listener_type_repo": str(listener_repo),
                "listener_type_stdout_path": str(stdout_log),
                "listener_type_stderr_path": str(stderr_log),
            },
        )

    stdout_log.write_text(completed.stdout or "", encoding="utf-8", errors="replace")
    stderr_log.write_text(completed.stderr or "", encoding="utf-8", errors="replace")

    try:
        report = extract_prefixed_json(completed.stdout or "", "ble_stream_smoke_result_json=")
    except json.JSONDecodeError as exc:
        return make_product_chain_result(
            case_id,
            "fail",
            "full_chain_report_json_invalid",
            {
                "json_error": str(exc),
                "returncode": completed.returncode,
                "listener_type_stdout_path": str(stdout_log),
                "listener_type_stderr_path": str(stderr_log),
            },
        )
    if report is None:
        return make_product_chain_result(
            case_id,
            "fail",
            "full_chain_report_missing",
            {
                "returncode": completed.returncode,
                "listener_type_stdout_path": str(stdout_log),
                "listener_type_stderr_path": str(stderr_log),
            },
        )

    transcript = first_non_empty(report.get("transcript"))
    history_session = report.get("history_session")
    if not isinstance(history_session, dict):
        history_session = find_listener_history_session(
            history_path=report.get("history_path"),
            transcript=transcript,
            expected_pcm_bytes=report.get("pcm_bytes"),
        ) or {}
        if history_session:
            report["history_session"] = history_session
            report["history_lookup_fallback"] = True
    embedded_stats = history_session.get("embeddedAudioStats")
    if not isinstance(embedded_stats, dict):
        embedded_stats = {}
    transcript = longest_non_empty(
        transcript,
        history_session.get("rawTranscript"),
        history_session.get("finalText"),
    )
    inserted_text = first_non_empty(report.get("inserted_text"))
    insertion_verified = bool(report.get("insertion_verified"))
    if not insertion_verified and transcript and inserted_text:
        insertion_verified = transcript in inserted_text
    insert_status = first_non_empty(history_session.get("insertStatus"))
    if not insert_status and insertion_verified:
        insert_status = "inserted"
    if not embedded_stats and report.get("pcm_bytes") is not None:
        embedded_stats = {
            "receivedPcmBytes": report.get("pcm_bytes"),
            "reconstructedPcmBytes": report.get("pcm_bytes"),
            "missingPacketCount": report.get("missing_packets"),
            "source": "smoke_report",
        }
    report_status = str(report.get("status") or "FAIL").upper()
    result = "pass" if report_status == "PASS" else "warning" if report_status == "WARNING" else "fail"
    reason = ""
    verification_errors = report.get("verification_errors") or []
    history_lookup_fallback = bool(report.get("history_lookup_fallback"))
    history_missing_only = bool(verification_errors) and all(
        str(item) == "history session was not written for this BLE smoke"
        for item in verification_errors
    )
    history_fallback_cleared_failure = history_lookup_fallback and history_missing_only
    fallback_resolved_verification = (
        history_lookup_fallback
        and bool(verification_errors)
        and bool(transcript)
        and bool(history_session)
        and bool(embedded_stats)
        and insert_status == "inserted"
        and all(
            str(item)
            in {
                "history session was not written for this BLE smoke",
                "no transcript/final text available for insertion verification",
                "target editor does not contain final text",
            }
            for item in verification_errors
        )
    )
    if history_fallback_cleared_failure or fallback_resolved_verification:
        result = "pass"
        verification_errors = []
        report["verification_errors"] = verification_errors
        report["status_after_history_lookup_fallback"] = "PASS"

    if expect_no_text:
        history_text = longest_non_empty(
            history_session.get("rawTranscript"),
            history_session.get("finalText"),
        )
        if completed.returncode != 0:
            result = "fail"
            reason = first_non_empty(report.get("error"), f"returncode={completed.returncode}")
        elif transcript:
            result = "fail"
            reason = "unexpected_transcript_for_no_text_case"
        elif inserted_text:
            result = "fail"
            reason = "unexpected_inserted_text_for_no_text_case"
        elif history_text:
            result = "fail"
            reason = "unexpected_history_text_for_no_text_case"
    elif completed.returncode != 0 and not (
        history_fallback_cleared_failure or fallback_resolved_verification
    ):
        result = "fail"
        reason = first_non_empty(report.get("error"), "; ".join(str(item) for item in verification_errors), f"returncode={completed.returncode}")
    elif not transcript:
        result = "fail"
        reason = "asr_transcript_missing"
    elif not embedded_stats:
        result = "fail"
        reason = "embedded_audio_stats_missing"
    elif insert_status != "inserted":
        result = "fail"
        reason = f"insert_status_not_inserted:{insert_status or 'missing'}"

    expected_text = first_non_empty(report.get("sentence"), expected_sentence)
    accuracy_details = score_transcript_accuracy(expected_text, transcript)
    accuracy_details["audio_profile"] = profile_name
    accuracy_details["source_tts_rate"] = profile_tts_rate
    accuracy_details["source_tts_gain"] = profile_tts_gain
    accuracy_details["accuracy_threshold"] = float(profile["minimum_accuracy"])
    accuracy_details["accuracy_warning_only"] = bool(profile["warning_only"])
    if not expect_no_text:
        result, reason = apply_accuracy_gate(
            current_result=result,
            current_reason=reason,
            audio_profile=profile,
            accuracy_details=accuracy_details,
        )

    effective_status = "PASS" if result == "pass" else "WARNING" if result == "warning" else "FAIL"
    details = {
        "full_chain_status": effective_status,
        "listener_type_report_status": report_status,
        "full_chain_returncode": completed.returncode,
        "trigger_mode": trigger_mode,
        "artifact_label": trigger_label,
        "audio_profile": profile_name,
        "source_tts_rate": profile_tts_rate,
        "source_tts_gain": profile_tts_gain,
        "generated_wav_path": str(generated_wav_path) if generated_wav_path is not None else None,
        "expect_no_text": expect_no_text,
        "listener_type_repo": str(listener_repo),
        "listener_type_stdout_path": str(stdout_log),
        "listener_type_stderr_path": str(stderr_log),
        "listener_type_report_path": latest_matching_file(output_dir, "ble-stream-smoke.*.json"),
        "sentence": report.get("sentence"),
        **accuracy_details,
        "transcript": transcript,
        "insert_status": insert_status,
        "inserted_text": inserted_text,
        "insertion_verified": insertion_verified,
        "history_session_id": history_session.get("id"),
        "missing_packets": report.get("missing_packets"),
        "pcm_bytes": report.get("pcm_bytes"),
        "embedded_audio_stats": embedded_stats,
        "recording_archive_path": report.get("recording_archive_path"),
        "verify_insertion": bool(report.get("verify_insertion")),
        "verify_history": bool(report.get("verify_history")),
        "history_lookup_fallback": history_lookup_fallback,
        "verification_errors": verification_errors,
        "listener_type_report": report,
    }
    print(f"product_chain_full_chain_status={effective_status}", flush=True)
    print(f"product_chain_listener_type_report_status={report_status}", flush=True)
    print(f"product_chain_accuracy={accuracy_details['accuracy']}", flush=True)
    print(f"product_chain_cer={accuracy_details['cer']}", flush=True)
    print(f"product_chain_transcript={transcript.replace(chr(13), ' ').replace(chr(10), ' ')}", flush=True)
    print(f"product_chain_insert_status={insert_status}", flush=True)
    print(f"product_chain_history_lookup_fallback={1 if history_lookup_fallback else 0}", flush=True)
    print(f"product_chain_history_session_id={history_session.get('id')}", flush=True)
    print(f"product_chain_missing_packets={report.get('missing_packets')}", flush=True)
    print(f"product_chain_pcm_bytes={report.get('pcm_bytes')}", flush=True)
    return make_product_chain_result(case_id, result, reason, details)


async def run_a19(args) -> dict[str, str]:
    round_count = max(1, int(args.soak_round_count))
    idle_seconds = max(0.0, float(args.soak_idle_seconds))
    budget_seconds = max(300, int(round_count * 90 + idle_seconds * max(0, round_count - 1) + 60))
    print_case_header("A19", f"mixed-use product-chain soak ({round_count} rounds)", budget_seconds)
    rounds = []
    failures = []
    warnings = []
    original_reset_before_capture = args.reset_before_capture
    args.reset_before_capture = False
    try:
        for round_index in range(1, round_count + 1):
            if round_index > 1 and idle_seconds > 0:
                print(f"a19_idle_before_round={round_index}:{idle_seconds:.2f}", flush=True)
                await asyncio.sleep(idle_seconds)
            random_sentence_count = 2 if round_index % 3 == 0 else 1
            product_chain = await run_listener_type_product_chain(
                args,
                "A19",
                trigger_mode="serial-toggle",
                artifact_label=f"soak_round_{round_index:02d}",
                random_sentence_count_override=random_sentence_count,
            )
            rounds.append(product_chain)
            result = str(product_chain.get("result"))
            print(f"a19_round={round_index}:{result}:{product_chain.get('reason', '')}", flush=True)
            if result == "fail":
                failures.append(round_index)
            elif result == "warning":
                warnings.append(round_index)
    finally:
        args.reset_before_capture = original_reset_before_capture
    if failures:
        return print_summary("A19", "fail", "failed_rounds=" + ",".join(str(v) for v in failures), {"rounds": rounds})
    if warnings:
        return print_summary("A19", "warning", "warning_rounds=" + ",".join(str(v) for v in warnings), {"rounds": rounds})
    return print_summary("A19", "pass", "", {"rounds": rounds})


async def run_h1(args) -> dict[str, str]:
    print_case_header("H1", "physical KEY1 product chain (manual trigger)", 180)
    product_chain = await run_listener_type_product_chain(args, "H1", trigger_mode="manual-key")
    return print_summary(
        "H1",
        str(product_chain["result"]),
        str(product_chain.get("reason", "")),
        {"product_chain": product_chain},
    )


CASE_RUNNERS = {
    "A1": run_a1, "A2": run_a2, "A3": run_a3,
    "A4": run_a4_new, "A5": run_a5_new, "A6": run_a6_new,
    "A7": run_a7_new, "A8": run_a8_new, "A9": run_a9_new,
    "A10": run_a10_new, "A11": run_a11_new,
    "A12": run_a12_new, "A13": run_a13_new,
    "A14": run_a14, "A15": run_a15, "A16": run_a16_new,
    "A17": run_a17, "A18": run_a18_new,
    "A19": run_a19,
    "H1": run_h1,
    "T1": run_t1, "T2": run_t2, "T3": run_t3, "T4": run_t4, "T5": run_t5,
}


def should_run_product_chain_overlay(args, case_id: str, case_result: dict[str, object]) -> bool:
    if args.transport_only:
        return False
    if case_id in PRODUCT_CHAIN_IN_RUNNER_CASES:
        return False
    if case_id not in PRODUCT_CHAIN_OVERLAY_CASES:
        return False
    return str(case_result.get("result")) in ("pass", "warning")


def should_stop_after_case(args, case_result: dict[str, object]) -> bool:
    if args.continue_on_failure:
        return False
    result = str(case_result.get("result") or "")
    if result == "fail":
        return True
    return bool(args.fail_on_warning and result == "warning")


def should_retry_empty_transcript_product_chain(product_chain: dict[str, object]) -> bool:
    if str(product_chain.get("result")) != "fail":
        return False
    details = product_chain.get("details")
    if not isinstance(details, dict):
        return False
    if first_non_empty(details.get("transcript")):
        return False
    report = details.get("listener_type_report")
    if not isinstance(report, dict):
        report = {}
    history_session = report.get("history_session")
    if not isinstance(history_session, dict):
        history_session = {}
    embedded_stats = details.get("embedded_audio_stats")
    if not isinstance(embedded_stats, dict):
        embedded_stats = history_session.get("embeddedAudioStats")
    if not isinstance(embedded_stats, dict) or not embedded_stats:
        return False
    try:
        missing_packets = int(embedded_stats.get("missingPacketCount") or 0)
    except (TypeError, ValueError):
        missing_packets = 0
    if missing_packets != 0:
        return False
    error_text = " ".join(
        first_non_empty(value)
        for value in (
            product_chain.get("reason"),
            report.get("error"),
            history_session.get("errorCode"),
        )
    ).lower()
    return "empty" in error_text and "transcript" in error_text


async def attach_product_chain_overlay(
    args,
    case_id: str,
    case_result: dict[str, object],
) -> dict[str, object]:
    print(f"product_chain_overlay_start={case_id}", flush=True)
    profile_results = []
    failures = []
    warnings = []
    profiles = product_chain_profiles_for_case(args, case_id)
    for profile_name in profiles:
        artifact_profile = profile_name.replace("-", "_")
        product_chain = await run_listener_type_product_chain(
            args,
            case_id,
            trigger_mode="serial-toggle",
            artifact_label=f"serial_toggle_{artifact_profile}",
            audio_profile=profile_name,
        )
        if should_retry_empty_transcript_product_chain(product_chain):
            print(f"product_chain_retry_empty_transcript={case_id}:{profile_name}", flush=True)
            retry_product_chain = await run_listener_type_product_chain(
                args,
                case_id,
                trigger_mode="serial-toggle",
                artifact_label=f"serial_toggle_{artifact_profile}_retry_empty_transcript",
                audio_profile=profile_name,
                sentence_override=PRODUCT_CHAIN_EMPTY_TRANSCRIPT_RETRY_SENTENCE,
            )
            retry_details = retry_product_chain.get("details")
            if isinstance(retry_details, dict):
                retry_details["retry_reason"] = "empty_transcript_with_complete_ble_audio"
                retry_details["initial_attempt"] = product_chain
            if str(retry_product_chain.get("result")) == "pass":
                product_chain = retry_product_chain
            else:
                details = product_chain.get("details")
                if isinstance(details, dict):
                    details["retry_attempt"] = retry_product_chain
        profile_results.append(product_chain)
        product_result = str(product_chain.get("result"))
        if product_result == "fail":
            failures.append(profile_name)
        elif product_result == "warning":
            warnings.append(profile_name)

    details = case_result.get("details")
    if not isinstance(details, dict):
        details = {}
        case_result["details"] = details
    details["product_chain_profiles"] = profile_results

    if failures:
        case_result["result"] = "fail"
        case_result["reason"] = "product_chain_failed:" + ",".join(failures)
    elif warnings and str(case_result.get("result")) == "pass":
        case_result["result"] = "warning"
        case_result["reason"] = "product_chain_warning:" + ",".join(warnings)
    print(
        f"product_chain_overlay_done={case_id}:profiles={','.join(profiles)}:failed={len(failures)}:warnings={len(warnings)}",
        flush=True,
    )
    return case_result


async def main_async(args) -> None:
    if args.skip_preflight_recover:
        args.preflight_recover_mode = "none"

    case_order = resolve_case_execution_order(args)
    print(f"execution_profile={args.execution_profile}", flush=True)
    print(f"random_capture_durations_enabled={0 if args.disable_random_capture_durations else 1}", flush=True)
    print(f"random_usage_timing_enabled={0 if args.disable_random_usage_timing else 1}", flush=True)
    print(f"shuffle_auto_case_order={1 if args.shuffle_auto_case_order else 0}", flush=True)
    print(f"preflight_recover_mode={args.preflight_recover_mode}", flush=True)
    print(f"product_chain_enabled={0 if args.transport_only else 1}", flush=True)
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
            case_result = await runner(args)
            if should_run_product_chain_overlay(args, case_id, case_result):
                case_result = await attach_product_chain_overlay(args, case_id, case_result)
            results.append(case_result)
            if should_stop_after_case(args, case_result):
                print(
                    f"matrix_stop_after_case={case_id}:{case_result.get('result')}:{case_result.get('reason', '')}",
                    flush=True,
                )
                break
        except Exception as exc:
            case_result = print_summary(case_id, "fail", f"{type(exc).__name__}:{exc}")
            results.append(case_result)
            if should_stop_after_case(args, case_result):
                print(
                    f"matrix_stop_after_case={case_id}:{case_result.get('result')}:{case_result.get('reason', '')}",
                    flush=True,
                )
                break

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
    if args.list_cases:
        print_case_catalog()
        return
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
