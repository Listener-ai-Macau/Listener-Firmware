import argparse
import asyncio
from datetime import datetime, timezone
import hashlib
import json
import os
import pathlib
import random
import re
import subprocess
import sys
import time
import unicodedata

import serial


def run_with_process_tree_timeout(
    command: list[str],
    *,
    cwd: str,
    timeout: int,
) -> subprocess.CompletedProcess[str]:
    process = subprocess.Popen(
        command,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    try:
        stdout_text, stderr_text = process.communicate(timeout=max(1, int(timeout)))
        return subprocess.CompletedProcess(command, process.returncode, stdout_text, stderr_text)
    except subprocess.TimeoutExpired as exc:
        if sys.platform == "win32":
            subprocess.run(
                ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
            )
        else:
            process.kill()
        try:
            stdout_text, stderr_text = process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            stdout_text, stderr_text = process.communicate(timeout=5)
        raise subprocess.TimeoutExpired(
            exc.cmd,
            exc.timeout,
            output=stdout_text,
            stderr=stderr_text,
        ) from exc


from ble_audio_regression_common import (
    AUDIO_PROFILE_CONFIGS,
    recover_ble_hid_host,
    validate_transport_summary,
    get_paired_device_address_hex,
    generate_profile_tts_wav,
    pick_chinese_sentences,
    pick_short_commands,
    resolve_audio_profile,
    DEFAULT_PLAYBACK_VOLUME_PERCENT,
    PLAYBACK_VOLUME_ENV,
    KEEP_PLAYBACK_VOLUME_ENV,
)
from capture_audio_ble_wav import configure_utf8_stdio


CASE_ORDER = ("A1", "A2")
EXTREME_CASES = (
    "A1",
    "A2",
    "A14",
    "T1",
    "T3",
    "L1",
    "H1",
    "H4",
    "A17",
    "A15",
    "D1",
)
MANUAL_CASES: tuple[str, ...] = ("H1", "H4")
TRANSPORT_ONLY_CASES: tuple[str, ...] = ("T1", "T3")
CASE_SUITES = {
    "smoke": CASE_ORDER,
    "daily": CASE_ORDER,
    "full": CASE_ORDER,
    "extreme": EXTREME_CASES,
    "auto": CASE_ORDER,
}
PRODUCT_CHAIN_OVERLAY_CASES = CASE_ORDER
PRODUCT_CHAIN_IN_RUNNER_CASES = ("A1", "A2")
CASE_DESCRIPTIONS = {
    "A1": "短录音稳定性：3 轮重启 Type + 3 轮持续打开 Type；持续模式胶囊关闭到下一次开启需小于 1 秒，短句准确率只记录不做 gate",
    "A2": "长段录音（约一分钟，默认 14 个分句）：验证完整传输 + partial preview 质量 + 最终识别准确率；extreme suite 使用 fast profile",
    "A14": "取消后恢复：中途取消当前录音，确认没有插入旧文本，然后立刻重试一轮正常录音",
    "A15": "静音误触/负向 case：没有有效语音时不应产生可见文本、历史插入或成功假象",
    "A17": "空闲/睡眠恢复：长时间 idle 或低功耗恢复后的首轮录音仍可解释并继续使用",
    "T1": "BLE 断连/回连：流式录音或准备阶段断开后恢复，合法下一次录音不被吞掉",
    "T3": "Notify disabled 或 Windows stale GATT/cache：host recovery 后重新达到 notify-ready",
    "L1": "Listener-Type 重启：桌面进程重启后设备和胶囊状态收敛到可继续录音",
    "H1": "EC11 物理录音键压力：物理 EC11 start/stop/recovery 不产生 stuck capsule 或重复 session",
    "H4": "KEY1-KEY4 物理/注入压力：自定义键和 fallback HID 压力下录音链路仍稳定",
    "D1": "诊断导出：固件 diag、BLE、Listener-Type 日志和 UI timeline 可对齐并带 artifact 引用",
}
MANUAL_OR_EXTERNAL_CASES = {
    "A14": "pending_automation: requires Listener-Type cancel/capsule recovery runner or manual cancel timing",
    "A15": "pending_automation: requires silent negative product-chain runner on current Listener-Type build",
    "A17": "manual_or_long_running: requires idle/sleep timing and optional power-state observation",
    "T1": "external_ble_state: requires intentional BLE disconnect/reconnect or Windows Bluetooth restart",
    "T3": "external_ble_state: requires notify disable/stale GATT-cache recovery on Windows",
    "L1": "external_desktop_state: requires Listener-Type process restart during or between sessions",
    "H1": "manual_hardware: requires physical EC11 operation under workflow hardware lock",
    "H4": "manual_hardware: requires physical or injection stress for KEY1-KEY4 under workflow hardware lock",
    "D1": "diagnostic_export: requires firmware diag pull plus Listener-Type log collection artifacts",
}
CASE_CONTRACTS = {
    "A1": {
        "scenario": "Short recordings split into restart-Type rounds and continuous-Type rounds with sub-1s capsule reopen latency.",
        "expected_user_visible_behavior": "Each legal short recording shows a new capsule quickly and does not lose a round silently; short transcript text is evidence-only by default because ambient speech can interfere.",
        "firmware_observables": [
            "voice recording source/state/session id",
            "BLE packet counts and missing/duplicate packet counters",
            "no QueueFull or session storm",
        ],
        "desktop_observables": [
            "capsule visible timestamp/source",
            "history session id",
            "continuous Type hidden_to_visible_seconds",
            "ASR partial/final text",
        ],
        "failure_classification": "latency, dropped_round, transport, asr, insertion, capsule",
        "artifact_requirements": [
            "matrix JSON",
            "summary log",
            "Listener-Type smoke report",
            "continuous Type background-round report",
            "firmware serial or diag reference when available",
        ],
        "automation_status": "automated",
    },
    "A2": {
        "scenario": "About 60 seconds of fast long-form dictation in the extreme suite.",
        "expected_user_visible_behavior": "Capsule stays visible through the long capture, final text inserts once, and the user can continue after completion.",
        "firmware_observables": [
            "expected/received/missing packet counts",
            "duplicate packet count",
            "audio transport summary",
            "no QueueFull or SessionError",
        ],
        "desktop_observables": [
            "partial preview count and content quality",
            "history session id",
            "insert status",
            "accuracy/CER",
        ],
        "failure_classification": "transport, packet_loss, duplicate_packet, asr, insertion, stuck_capsule",
        "artifact_requirements": [
            "matrix JSON",
            "summary log",
            "Listener-Type smoke report",
            "generated WAV path",
            "firmware diag reference when available",
        ],
        "automation_status": "automated",
    },
    "A14": {
        "scenario": "Cancel during an active or pending capture, then retry a normal capture.",
        "expected_user_visible_behavior": "Cancel stops capture without inserting stale text; retry starts a fresh session and inserts only retry text.",
        "firmware_observables": [
            "cancel source/result",
            "session id transition",
            "stop/cancel ownership",
        ],
        "desktop_observables": [
            "capsule cancelled state",
            "no stale history insertion",
            "retry capsule/session timeline",
        ],
        "failure_classification": "cancel_lost, stale_insert, stuck_capsule, retry_blocked",
        "artifact_requirements": [
            "serial report",
            "Listener-Type timeline",
            "history before/after",
            "matrix JSON",
        ],
        "automation_status": "manual_or_external",
    },
    "A15": {
        "scenario": "Silent or accidental trigger negative case.",
        "expected_user_visible_behavior": "No meaningful text is inserted and any visible error explains the absence of speech.",
        "firmware_observables": [
            "short or empty audio session summary",
            "cancel/error terminal reason",
        ],
        "desktop_observables": [
            "no ASR final text",
            "no history insert",
            "actionable no-speech/error state when surfaced",
        ],
        "failure_classification": "false_positive_text, stale_history, silent_success",
        "artifact_requirements": [
            "silent audio report",
            "history snapshot",
            "capsule timeline",
        ],
        "automation_status": "manual_or_external",
    },
    "A17": {
        "scenario": "Idle or sleep/resume before first recording.",
        "expected_user_visible_behavior": "After idle/resume, the next legal press starts or explains recovery instead of being swallowed.",
        "firmware_observables": [
            "power/idle status",
            "wake or recovery diag events",
            "recording source/result",
        ],
        "desktop_observables": [
            "BLE readiness timeline",
            "capsule/error visible state",
            "history result",
        ],
        "failure_classification": "resume_not_ready, lost_press, stale_ble_state",
        "artifact_requirements": [
            "idle duration",
            "serial/diag export",
            "Listener-Type logs",
            "matrix JSON",
        ],
        "automation_status": "manual_or_external",
    },
    "T1": {
        "scenario": "BLE disconnect/reconnect before or during recording.",
        "expected_user_visible_behavior": "Recovery reaches ready state within the scenario budget or shows an actionable reconnect error.",
        "firmware_observables": [
            "connection epoch",
            "GAP disconnect/connect",
            "session abort/recovery summary",
        ],
        "desktop_observables": [
            "BLE readiness timeline",
            "capsule terminal state",
            "reconnect result",
        ],
        "failure_classification": "reconnect_timeout, stale_epoch, stuck_capsule, lost_legal_press",
        "artifact_requirements": [
            "Windows BLE evidence",
            "firmware diag export",
            "Listener-Type logs",
            "matrix JSON",
        ],
        "automation_status": "manual_or_external",
    },
    "T3": {
        "scenario": "Notify disabled or Windows stale GATT/cache recovery.",
        "expected_user_visible_behavior": "The app either repairs notify readiness or gives a clear next step; it must not claim a successful recording without audio.",
        "firmware_observables": [
            "notify readiness",
            "MTU/subscription epoch",
            "stale event discard",
        ],
        "desktop_observables": [
            "cache recovery action",
            "notify-ready timestamp",
            "visible error text when recovery fails",
        ],
        "failure_classification": "notify_not_ready, stale_cache, false_success, transport_not_ready",
        "artifact_requirements": [
            "BLE address/cache evidence",
            "firmware diag export",
            "Listener-Type log",
            "matrix JSON",
        ],
        "automation_status": "manual_or_external",
    },
    "L1": {
        "scenario": "Listener-Type restart while firmware remains powered and paired.",
        "expected_user_visible_behavior": "After restart, the app discovers or recovers the device and the next recording behaves like a fresh session.",
        "firmware_observables": [
            "connection state after desktop restart",
            "active session ownership",
            "diagnostic pull availability",
        ],
        "desktop_observables": [
            "process restart timeline",
            "BLE readiness state",
            "capsule/history session ownership",
        ],
        "failure_classification": "desktop_restart_loss, stale_session, reconnect_timeout",
        "artifact_requirements": [
            "Listener-Type restart log",
            "firmware serial/diag",
            "history session id",
            "matrix JSON",
        ],
        "automation_status": "manual_or_external",
    },
    "H1": {
        "scenario": "Physical EC11 start/stop/recovery stress.",
        "expected_user_visible_behavior": "Physical presses map to one session action each and do not create stuck recording/transferring UI.",
        "firmware_observables": [
            "voice_key source label",
            "press/cancel/recovery result",
            "session count",
        ],
        "desktop_observables": [
            "capsule state per press",
            "history insertion once per valid session",
        ],
        "failure_classification": "double_trigger, missed_press, stuck_state, stale_history",
        "artifact_requirements": [
            "hardware lock evidence",
            "serial log",
            "Listener-Type log",
            "matrix JSON or written timeline",
        ],
        "automation_status": "manual_hardware",
    },
    "H4": {
        "scenario": "KEY1-KEY4 physical/custom fallback stress while BLE audio remains available.",
        "expected_user_visible_behavior": "Custom/fallback key actions are delivered or explained without disrupting legal voice recordings.",
        "firmware_observables": [
            "custom key diag events",
            "HID dispatch result",
            "recording control state",
        ],
        "desktop_observables": [
            "focused app input result",
            "BLE/capsule readiness after stress",
        ],
        "failure_classification": "hid_loss, recording_disruption, stuck_modifier, lost_voice_press",
        "artifact_requirements": [
            "hardware lock evidence",
            "serial/custom-key log",
            "Listener-Type log",
            "matrix JSON or written timeline",
        ],
        "automation_status": "manual_hardware",
    },
    "D1": {
        "scenario": "Diagnostic export after an extreme or failed scenario.",
        "expected_user_visible_behavior": "A support package can explain firmware, BLE, desktop, and UI timeline without asking the user to reproduce blindly.",
        "firmware_observables": [
            "diag_log export path/count/CRC",
            "audio/session diag references",
            "BLE readiness events",
        ],
        "desktop_observables": [
            "Listener-Type log path",
            "capsule timeline",
            "package commit/hash",
            "history session id",
        ],
        "failure_classification": "diagnostics_missing, timeline_unaligned, package_unidentified",
        "artifact_requirements": [
            "firmware diag export",
            "Listener-Type log bundle",
            "matrix JSON",
            "package SHA256/commit",
        ],
        "automation_status": "manual_or_external",
    },
}
MATRIX_ARTIFACT_DIR = pathlib.Path("tests") / "artifacts" / "ble_product_matrix"
A2_PRODUCT_CHAIN_RANDOM_SENTENCE_COUNT = 14
A2_PRODUCT_CHAIN_MIN_LISTENER_TIMEOUT_MS = 180000
A2_PRODUCT_CHAIN_MIN_TIMEOUT_SECONDS = 240
NEGATIVE_PRODUCT_CHAIN_TIMEOUT_SECONDS = 70
NEGATIVE_PRODUCT_CHAIN_LISTENER_TIMEOUT_MS = 35000
PRODUCT_CHAIN_EMPTY_TRANSCRIPT_RETRY_SENTENCE = "蓝牙音频正在发送到火山识别，请检查文本结果。"
A1_RESTART_ROUND_COUNT = 3
A1_CONTINUOUS_ROUND_COUNT = 3
A1_CONTINUOUS_MAX_HIDDEN_TO_VISIBLE_SECONDS = 1.0

SHORT_DICTATION_SENTENCES = (
    "今天天气不错，适合出去走走，散散心。",
    "蓝牙音频正在发送到火山识别，请检查文本结果。",
    "我正在测试语音输入，确认文字可以稳定出现。",
    "火山识别和蓝牙传输正在接受测试。",
)

SHORT_DICTATION_CASES = {"A1"}

A2_LONG_DICTATION_LEADS = (
    "今天我会连续记录蓝牙听写的使用过程",
    "这段长录音用来验证真实会议记录的输入体验",
    "现在开始进行一段完整的产品链路长听写",
    "我正在复盘上午的调试过程和后续安排",
    "蓝牙听写测试现在开始记录第一句话",
    "这段录音正在验证长时间语音输入",
    "请把这段长录音完整转换成文字",
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
    "A2": ("normal",),
}
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
            "Case IDs or suite name. Suites: smoke/daily/full/auto (A1,A2), "
            "extreme (A1,A2,A14,T1,T3,L1,H1,H4,A17,A15,D1). "
            "Or comma-separated IDs: A1,A2,A14."
        ),
    )
    parser.add_argument("--list-cases", action="store_true")
    parser.add_argument(
        "--full-chain",
        action="store_true",
        help="Compatibility no-op. A1/A2 validate the product chain by default; use --transport-only for BLE-only diagnostics.",
    )
    parser.add_argument(
        "--transport-only",
        action="store_true",
        help="Disable product-chain ASR/text overlay for diagnostics. The simplified matrix no longer has legacy transport-only cases.",
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
    parser.add_argument(
        "--inter-session-gap-min-seconds",
        type=float,
        default=None,
        help="Minimum A1 gap after the previous text/history appears before starting the next short recording (default window: 0-1 seconds).",
    )
    parser.add_argument(
        "--inter-session-gap-max-seconds",
        type=float,
        default=None,
        help="Maximum A1 gap after the previous text/history appears before starting the next short recording (default window: 0-1 seconds).",
    )
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
        "--listener-exe",
        default=None,
        help="Optional listener-type.exe path passed to the Listener-Type product-chain smoke script.",
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
        "--playback-volume-percent",
        type=int,
        default=DEFAULT_PLAYBACK_VOLUME_PERCENT,
        help="Set the Windows playback endpoint to this volume before scripted WAV playback. Use -1 to disable.",
    )
    parser.add_argument(
        "--keep-playback-volume",
        action="store_true",
        help="Do not restore the previous Windows playback endpoint volume after scripted WAV playback.",
    )
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
        help="Random clause count for A2 long-recording product-chain validation when --full-chain-sentence is not set (default: 14, about one minute).",
    )
    parser.add_argument(
        "--a1-round-count",
        type=int,
        default=None,
        help="Legacy A1 rapid-round count. Values above 3 are capped by the current 3 restart-round product contract.",
    )
    parser.add_argument(
        "--a1-restart-round-count",
        type=int,
        default=A1_RESTART_ROUND_COUNT,
        help="Number of A1 short-recording rounds that restart Listener-Type each time (default: 3).",
    )
    parser.add_argument(
        "--a1-continuous-round-count",
        type=int,
        default=A1_CONTINUOUS_ROUND_COUNT,
        help="Number of A1 short-recording rounds with Listener-Type kept open continuously (default: 3).",
    )
    parser.add_argument(
        "--a1-continuous-max-hidden-to-visible-seconds",
        type=float,
        default=A1_CONTINUOUS_MAX_HIDDEN_TO_VISIBLE_SECONDS,
        help="Maximum allowed capsule hidden-to-visible latency for continuous A1 rounds (default: 1.0).",
    )
    parser.add_argument(
        "--a1-short-check-accuracy",
        action="store_true",
        help="Also gate short A1 recordings by ASR accuracy. Default is evidence-only; long A2 still gates accuracy.",
    )
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


def configure_playback_volume(args) -> None:
    os.environ[PLAYBACK_VOLUME_ENV] = str(args.playback_volume_percent)
    if args.keep_playback_volume:
        os.environ[KEEP_PLAYBACK_VOLUME_ENV] = "1"
    else:
        os.environ.pop(KEEP_PLAYBACK_VOLUME_ENV, None)


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
        default_seconds=0.5,
        min_seconds=args.inter_session_gap_min_seconds,
        max_seconds=args.inter_session_gap_max_seconds,
        min_padding=0.5,
        max_padding=0.5,
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


def is_listener_type_capsule_window_visible() -> bool:
    if sys.platform != "win32":
        return False

    try:
        import ctypes
        from ctypes import wintypes

        user32 = ctypes.windll.user32
        visible = False

        enum_windows_proc = ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)

        def callback(hwnd, _lparam):
            nonlocal visible
            if not user32.IsWindowVisible(hwnd):
                return True
            length = user32.GetWindowTextLengthW(hwnd)
            if length <= 0:
                return True
            buffer = ctypes.create_unicode_buffer(length + 1)
            user32.GetWindowTextW(hwnd, buffer, length + 1)
            title = buffer.value
            if title == "Listener Type Capsule" or "Listener Type Capsule" in title:
                visible = True
                return False
            return True

        user32.EnumWindows(enum_windows_proc(callback), 0)
        return visible
    except Exception as exc:
        print(f"capsule_window_probe_error={type(exc).__name__}:{exc}", flush=True)
        return False


async def wait_for_capsule_window_hidden(
    *,
    label: str,
    timeout_seconds: float = 8.0,
    poll_seconds: float = 0.25,
) -> dict[str, object]:
    supported = sys.platform == "win32"
    start = time.monotonic()
    visible_at_start = is_listener_type_capsule_window_visible() if supported else False
    hidden = not visible_at_start

    if supported and visible_at_start:
        deadline = start + max(0.0, timeout_seconds)
        while time.monotonic() < deadline:
            await asyncio.sleep(max(0.05, poll_seconds))
            if not is_listener_type_capsule_window_visible():
                hidden = True
                break

    elapsed_seconds = round(time.monotonic() - start, 2)
    print(f"{label}_capsule_window_supported={str(supported).lower()}", flush=True)
    print(f"{label}_capsule_visible_at_gap_start={str(visible_at_start).lower()}", flush=True)
    print(f"{label}_capsule_hidden_before_gap={str(hidden).lower()}", flush=True)
    print(f"{label}_capsule_hidden_wait_seconds={elapsed_seconds:.2f}", flush=True)
    return {
        "label": label,
        "supported": supported,
        "visible_at_start": visible_at_start,
        "hidden": hidden,
        "wait_seconds": elapsed_seconds,
        "timeout_seconds": timeout_seconds,
    }


def probe_capsule_window_state(*, label: str) -> dict[str, object]:
    supported = sys.platform == "win32"
    visible = is_listener_type_capsule_window_visible() if supported else False
    print(f"{label}_capsule_window_supported={str(supported).lower()}", flush=True)
    print(f"{label}_capsule_visible_before_gap={str(visible).lower()}", flush=True)
    return {
        "label": label,
        "supported": supported,
        "visible_before_gap": visible,
    }


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
        "duplicate_packet_count",
        "duplicate_packet_sequences",
        "duplicate_packet_details",
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
    contract = CASE_CONTRACTS.get(case_id, {})
    details_payload = details or {}
    if contract:
        details_payload = {
            "contract": contract,
            **details_payload,
        }
    return {
        "case_id": case_id,
        "description": CASE_DESCRIPTIONS.get(case_id, ""),
        "automation_status": contract.get("automation_status"),
        "failure_classification": contract.get("failure_classification"),
        "artifact_requirements": contract.get("artifact_requirements", []),
        "result": result,
        "reason": reason,
        "failure_timestamp_utc": datetime.now(timezone.utc).isoformat() if result == "fail" else None,
        "details": details_payload,
        "artifacts": case_artifact_manifest(case_id),
    }


def preflight_recover_host(args, case_id: str) -> None:
    if args.skip_preflight_recover:
        print(f"preflight_recover_skipped={case_id}", flush=True)
        return
    print(f"preflight_recover_start={case_id}", flush=True)
    recover_ble_hid_host(args.device_name, args.bluetooth_address)
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


def run_git_text(args: list[str], *, cwd: pathlib.Path) -> str:
    try:
        completed = subprocess.run(
            ["git", *args],
            cwd=str(cwd),
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=10,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        return ""
    if completed.returncode != 0:
        return ""
    return (completed.stdout or "").strip()


def git_snapshot(path: pathlib.Path) -> dict[str, object]:
    if not path.exists():
        return {
            "path": str(path),
            "missing": True,
            "commit": None,
            "branch": None,
            "dirty": None,
        }
    commit = run_git_text(["rev-parse", "HEAD"], cwd=path)
    branch = run_git_text(["rev-parse", "--abbrev-ref", "HEAD"], cwd=path)
    dirty = bool(run_git_text(["status", "--porcelain"], cwd=path))
    return {
        "path": str(path),
        "commit": commit or None,
        "branch": branch or None,
        "dirty": dirty,
    }


def sha256_file(path: pathlib.Path) -> str | None:
    if not path.exists() or not path.is_file():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def package_snapshot(args) -> dict[str, object]:
    if not args.listener_exe:
        return {"listener_exe": None, "package_hash": None}
    exe_path = pathlib.Path(args.listener_exe).expanduser().resolve()
    digest = sha256_file(exe_path)
    return {
        "listener_exe": str(exe_path),
        "listener_exe_sha256": digest,
        "package_hash": digest,
    }


def matrix_run_metadata(args) -> dict[str, object]:
    listener_repo = resolve_listener_type_repo(args)
    return {
        "schema": "voice_keyboard_extreme_matrix.v1",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "firmware": git_snapshot(firmware_repo_root()),
        "listener_type": git_snapshot(listener_repo),
        "package": package_snapshot(args),
    }


def first_non_empty(*values: object) -> str:
    for value in values:
        text = "" if value is None else str(value).strip()
        if text:
            return text
    return ""


def longest_non_empty(*values: object) -> str:
    texts = [str(value).strip() for value in values if value is not None and str(value).strip()]
    return max(texts, key=len) if texts else ""


def parse_iso_datetime_utc(value: object) -> datetime | None:
    text = first_non_empty(value)
    if not text:
        return None
    normalized = text.replace("Z", "+00:00")
    if "." in normalized:
        prefix, suffix = normalized.split(".", 1)
        tz_index = min(
            [index for index in (suffix.find("+"), suffix.find("-")) if index >= 0]
            or [len(suffix)]
        )
        fraction = suffix[:tz_index]
        tz_suffix = suffix[tz_index:]
        normalized = f"{prefix}.{(fraction + '000000')[:6]}{tz_suffix}"
    try:
        parsed = datetime.fromisoformat(normalized)
    except ValueError:
        return None
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return parsed.astimezone(timezone.utc)


def history_session_is_stale_for_report(
    report: dict[str, object],
    history_session: dict[str, object],
) -> bool:
    started_at = parse_iso_datetime_utc(report.get("started_at_utc"))
    created_at = parse_iso_datetime_utc(history_session.get("createdAt"))
    return bool(started_at and created_at and created_at < started_at)


def normalize_accuracy_text(text: object) -> str:
    folded = unicodedata.normalize("NFKC", "" if text is None else str(text)).lower()
    folded = normalize_chinese_numerals_for_accuracy(folded)
    return "".join(ch for ch in folded if ch.isalnum())


CHINESE_NUMERAL_DIGITS = {
    "零": "0",
    "〇": "0",
    "一": "1",
    "二": "2",
    "两": "2",
    "三": "3",
    "四": "4",
    "五": "5",
    "六": "6",
    "七": "7",
    "八": "8",
    "九": "9",
}


def parse_chinese_numeral_token(token: str) -> str | None:
    token = token.strip()
    if not token:
        return None
    if "十" in token:
        parts = token.split("十", 1)
        tens = 1 if not parts[0] else int(CHINESE_NUMERAL_DIGITS.get(parts[0], "0"))
        units = 0 if not parts[1] else int(CHINESE_NUMERAL_DIGITS.get(parts[1], "0"))
        return str(tens * 10 + units)
    if all(ch in CHINESE_NUMERAL_DIGITS for ch in token):
        return "".join(CHINESE_NUMERAL_DIGITS[ch] for ch in token)
    return None


def normalize_chinese_numerals_for_accuracy(text: str) -> str:
    digit_chars = "零〇一二两三四五六七八九"
    number_chars = f"{digit_chars}十"

    def replace_token(match: re.Match[str]) -> str:
        parsed = parse_chinese_numeral_token(match.group(0))
        return parsed if parsed is not None else match.group(0)

    def replace_decimal(match: re.Match[str]) -> str:
        lhs = parse_chinese_numeral_token(match.group(1))
        rhs = parse_chinese_numeral_token(match.group(2))
        if lhs is None or rhs is None:
            return match.group(0)
        return f"{lhs}.{rhs}"

    text = re.sub(
        rf"百分之([{number_chars}]+)",
        lambda match: parse_chinese_numeral_token(match.group(1)) or match.group(0),
        text,
    )
    text = re.sub(
        rf"([{number_chars}]+)点([{digit_chars}]+)",
        replace_decimal,
        text,
    )
    text = re.sub(
        rf"[{number_chars}]+(?=年|点|分钟|秒|位|个|条|次|页|行|列|号|%|％)",
        replace_token,
        text,
    )
    text = re.sub(
        rf"(?<=第)[{number_chars}]+(?=页|章|节|行|列|个|次|段|句|项|条)",
        replace_token,
        text,
    )
    return text


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
    min_chars: int = 6,
) -> dict[str, object]:
    if not partial_preview:
        return {"pass": False, "reason": "empty_partial_preview"}
    if not expected_text:
        return {"pass": True, "reason": "no_expected_text"}

    normalized_expected = normalize_accuracy_text(expected_text)
    normalized_partial = normalize_accuracy_text(partial_preview)
    if not normalized_partial:
        return {"pass": False, "reason": "empty_normalized_partial_preview"}
    if len(normalized_partial) < min_chars:
        return {
            "pass": False,
            "reason": f"partial_preview_too_short:{len(normalized_partial)}<{min_chars}",
            "prefix_cer": 1.0,
            "best_window_cer": 1.0,
            "match_cer": 1.0,
        }

    prefix_len = min(len(normalized_partial), len(normalized_expected))
    expected_prefix = normalized_expected[:prefix_len]
    distance = edit_distance(expected_prefix, normalized_partial)
    prefix_cer = distance / float(len(expected_prefix)) if expected_prefix else 0.0
    best_window_cer = prefix_cer
    best_window_offset = 0
    if normalized_expected:
        window_len = min(len(normalized_partial), len(normalized_expected))
        if len(normalized_partial) <= len(normalized_expected):
            best_window_cer = 1.0
            for offset in range(0, len(normalized_expected) - window_len + 1):
                expected_window = normalized_expected[offset:offset + window_len]
                window_distance = edit_distance(expected_window, normalized_partial)
                window_cer = window_distance / float(window_len) if window_len else 0.0
                if window_cer < best_window_cer:
                    best_window_cer = window_cer
                    best_window_offset = offset
        else:
            window_distance = edit_distance(normalized_expected, normalized_partial)
            best_window_cer = window_distance / float(len(normalized_expected))
    match_cer = min(prefix_cer, best_window_cer)
    quality_pass = match_cer <= max_cer
    match_type = "prefix" if prefix_cer <= best_window_cer else "window"

    return {
        "pass": quality_pass,
        "reason": "" if quality_pass else f"partial_preview_match_cer={match_cer:.3f}>{max_cer}",
        "prefix_cer": round(prefix_cer, 6),
        "best_window_cer": round(best_window_cer, 6),
        "best_window_offset": best_window_offset,
        "match_cer": round(match_cer, 6),
        "match_type": match_type,
        "prefix_length": prefix_len,
        "preview": partial_preview,
    }


def validate_best_partial_preview_quality(
    expected_text: str,
    partial_previews: list[str],
    *,
    max_cer: float = 0.5,
) -> dict[str, object]:
    meaningful_previews = [preview for preview in partial_previews if preview and preview.strip()]
    if not meaningful_previews:
        return {"pass": False, "reason": "empty_partial_preview"}
    best: dict[str, object] | None = None
    for preview in meaningful_previews:
        quality = validate_partial_preview_quality(expected_text, preview, max_cer=max_cer)
        if best is None or float(quality.get("match_cer", 1.0)) < float(best.get("match_cer", 1.0)):
            best = quality
    return best or {"pass": False, "reason": "empty_partial_preview"}


def validate_capsule_evidence(
    case_id: str,
    product_chain: dict[str, object],
    *,
    expect_partial: bool = True,
    expect_no_text: bool = False,
) -> dict[str, object]:
    details = product_chain.get("details")
    if not isinstance(details, dict):
        return {"pass": True, "reason": "no_product_chain_details", "capsule_validated": False}

    partial_preview_count = details.get("partial_preview_count")
    last_partial_preview = details.get("last_partial_preview")
    transcript = details.get("transcript")
    expected_text = first_non_empty(details.get("expected_text"), details.get("sentence"))
    final_text = first_non_empty(details.get("final_text"), transcript)
    raw_updates = details.get("asr_text_updates")
    text_updates = [
        str(item).strip()
        for item in raw_updates
        if item is not None and str(item).strip()
    ] if isinstance(raw_updates, list) else []
    normalized_final = normalize_accuracy_text(final_text)
    partial_only_updates = [
        item for item in text_updates
        if not normalized_final or normalize_accuracy_text(item) != normalized_final
    ]
    first_partial_preview = partial_only_updates[0] if partial_only_updates else (
        text_updates[0] if text_updates else ""
    )
    final_preview_update_seen = bool(
        normalized_final
        and any(normalize_accuracy_text(item) == normalized_final for item in text_updates)
    )
    normalized_expected = normalize_accuracy_text(expected_text)
    expected_front_prefix = normalized_expected[: min(12, len(normalized_expected))]
    front_prefix_seen_before_final = bool(
        expected_front_prefix
        and any(expected_front_prefix in normalize_accuracy_text(item) for item in partial_only_updates)
    )

    if expect_no_text:
        had_unexpected_partial = (
            isinstance(partial_preview_count, int) and partial_preview_count > 0
            and isinstance(last_partial_preview, str) and len(last_partial_preview.strip()) > 2
        )
        if had_unexpected_partial and str(product_chain.get("result")) != "fail":
            return {
                "pass": False,
                "reason": "unexpected_capsule_partial_preview_for_no_text_case",
                "partial_preview_count": partial_preview_count,
                "capsule_validated": True,
            }
        return {"pass": True, "reason": "no_unexpected_capsule_activity", "capsule_validated": True}

    if not expect_partial:
        return {"pass": True, "reason": "capsule_check_not_required", "capsule_validated": False}

    # CLI mode (no capsule window): skip partial preview check but still verify transcript.
    timeline = details.get("timeline")
    capsule_visible = isinstance(timeline, dict) and timeline.get("capsule_visible_at_utc")
    if expect_partial and not capsule_visible:
        had_transcript = bool(transcript and str(transcript).strip())
        return {
            "pass": had_transcript,
            "reason": "cli_mode_no_capsule" if had_transcript else "cli_mode_no_transcript",
            "capsule_validated": False,
            "partial_preview_visible": None,
            "final_text_received": had_transcript,
        }

    had_partial = isinstance(partial_preview_count, int) and partial_preview_count > 0
    had_transcript = bool(transcript and str(transcript).strip())

    checks: dict[str, object] = {
        "partial_preview_visible": had_partial,
        "final_text_received": had_transcript,
        "preview_evidence": {
            "text_update_count": len(text_updates),
            "first_partial_preview": first_partial_preview,
            "last_partial_preview": str(last_partial_preview or ""),
            "final_preview_update_seen": final_preview_update_seen,
            "front_prefix_seen_before_final": front_prefix_seen_before_final,
            "expected_front_prefix": expected_front_prefix,
            "final_text": final_text,
        },
    }

    if case_id == "A2" and had_partial:
        preview_quality = validate_best_partial_preview_quality(
            str(first_non_empty(expected_text, final_text, transcript)),
            partial_only_updates or [str(last_partial_preview or "")],
            max_cer=0.5,
        )
        checks["partial_preview_content_quality"] = preview_quality["pass"]
        checks["partial_preview_prefix_cer"] = preview_quality.get("prefix_cer")
        checks["partial_preview_best_window_cer"] = preview_quality.get("best_window_cer")
        checks["partial_preview_match_type"] = preview_quality.get("match_type")
        checks["partial_preview_quality_source"] = preview_quality.get("preview")

    all_pass = all(v for v in checks.values() if isinstance(v, bool))
    reasons = [k for k, v in checks.items() if isinstance(v, bool) and not v]

    return {
        "pass": all_pass,
        "reason": "" if all_pass else "capsule_check_failed:" + ",".join(reasons),
        "capsule_validated": True,
        **checks,
    }


def product_chain_profiles_for_case(args, case_id: str) -> tuple[str, ...]:
    requested_profile = str(args.full_chain_audio_profile)
    if requested_profile != "normal":
        return (requested_profile,)
    profiles = CASE_PRODUCT_CHAIN_AUDIO_PROFILES.get(case_id)
    if profiles:
        return profiles
    return (requested_profile,)


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
        "extreme_cases": list(EXTREME_CASES),
        "manual_cases": list(MANUAL_CASES),
        "transport_only_cases": list(TRANSPORT_ONLY_CASES),
        "case_suites": {
            name: list(cases)
            for name, cases in sorted(CASE_SUITES.items())
        },
        "case_descriptions": CASE_DESCRIPTIONS,
        "case_contracts": CASE_CONTRACTS,
        "extreme_suite_defaults": {
            "a1_restart_round_count": A1_RESTART_ROUND_COUNT,
            "a1_continuous_round_count": A1_CONTINUOUS_ROUND_COUNT,
            "a1_continuous_max_hidden_to_visible_seconds": A1_CONTINUOUS_MAX_HIDDEN_TO_VISIBLE_SECONDS,
            "a1_short_accuracy_gate": "evidence_only",
            "legacy_a1_round_count": "accepted for compatibility; values above 3 are capped by the current product contract",
            "inter_session_gap_seconds": "0-1 for restart-mode compatibility; continuous mode measures capsule hidden-to-visible latency",
            "a2_long_capture_seconds": "60",
            "a2_audio_profile": "fast",
            "required_lock": "aiw with-lock for COMx and BLE-<address> before real hardware execution",
            "artifact_root": str(MATRIX_ARTIFACT_DIR),
        },
        "matrix_result_required_fields": [
            "text_to_next_capsule_latencies",
            "capsule_source",
            "history_session_id",
            "expected_packet_count",
            "received_packet_count",
            "missing_packet_count",
            "duplicate_packet_count",
            "firmware_diag_refs",
            "listener_type_log_refs",
            "package_hash",
            "commit",
            "failure_timestamp_utc",
        ],
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
        "implemented_cases": sorted(CASE_RUNNERS.keys()),
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


def finalize_transport_capture_summary(
    summary: dict[str, object],
    *,
    capture_seconds: int,
    pre_start_delay_seconds: float,
) -> dict[str, object]:
    validation = validate_transport_summary(summary, capture_seconds=capture_seconds)
    summary.update(validation)
    summary["pre_start_delay_seconds"] = float(pre_start_delay_seconds)
    summary["result"] = "pass"
    summary["failure_reason"] = ""
    summary["warning_reason"] = ""
    if validation["transport_result"] == "fail":
        summary["result"] = "fail"
        summary["failure_reason"] = (
            validation["transport_failure_reason"] or "transport_validation_failed"
        )
    elif validation["transport_result"] == "warning":
        summary["result"] = "warning"
        summary["warning_reason"] = (
            validation["transport_warning_reason"] or "transport_warning"
        )
    return summary


def summary_outcome(summary: dict[str, object]) -> tuple[str, str]:
    return (
        str(summary.get("result") or "fail"),
        str(summary.get("failure_reason") or summary.get("warning_reason") or ""),
    )


def skipped_case_summary(case_id: str, reason: str) -> dict[str, object]:
    return print_summary(
        case_id,
        "skipped",
        reason,
        details={
            "skip_is_baseline": True,
            "baseline_note": "Scenario is part of the durable extreme-use contract but is not automated in this firmware-only harness step.",
            "automation_gap": reason,
            "required_artifacts": CASE_CONTRACTS.get(case_id, {}).get("artifact_requirements", []),
        },
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
    metadata: dict[str, object],
    requested_cases: list[str],
    case_suite: str,
) -> pathlib.Path:
    output_path = pathlib.Path(path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    status = "FAIL" if failed or (fail_on_warning and warnings) else "PASS_WITH_SKIPS" if skipped else "PASS"
    payload = {
        "schema": "voice_keyboard_extreme_matrix_result.v1",
        "status": status,
        "fail_on_warning": bool(fail_on_warning),
        "matrix_incomplete": bool(skipped),
        "skipped_is_not_pass_evidence": bool(skipped),
        "case_suite": case_suite,
        "requested_cases": requested_cases,
        "run_metadata": metadata,
        "case_contracts": {
            case_id: CASE_CONTRACTS[case_id]
            for case_id in requested_cases
            if case_id in CASE_CONTRACTS
        },
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


async def run_product_chain_primary_case(
    args,
    case_id: str,
    title: str,
    budget_seconds: int,
) -> dict[str, object]:
    print_case_header(case_id, title, budget_seconds)
    print(f"transport_capture_skipped={case_id}:product_chain_primary", flush=True)
    case_result: dict[str, object] = {
        "case_id": case_id,
        "result": "pass",
        "reason": "",
        "details": {
            "transport_capture_skipped": True,
            "transport_skip_reason": "product_chain_primary_requires_capsule_before_playback",
        },
    }
    return await attach_product_chain_overlay(args, case_id, case_result)


def resolve_a1_round_plan(args) -> tuple[int, int]:
    restart_count = max(0, int(args.a1_restart_round_count))
    continuous_count = max(0, int(args.a1_continuous_round_count))
    legacy_round_count = getattr(args, "a1_round_count", None)
    if legacy_round_count is not None:
        restart_count = min(restart_count, max(0, int(legacy_round_count)))
    return restart_count, continuous_count


async def run_listener_type_background_rounds(
    args,
    *,
    round_count: int,
) -> dict[str, object]:
    output_dir = case_output_dir("A1") / "continuous_type"
    output_dir.mkdir(parents=True, exist_ok=True)
    listener_repo = resolve_listener_type_repo(args)
    script_path = listener_repo / "tools" / "embedded_audio_replay" / "run_ble_background_rounds.ps1"
    stdout_log = output_dir / "listener_type_continuous_stdout.log"
    stderr_log = output_dir / "listener_type_continuous_stderr.log"
    if not script_path.exists():
        return make_product_chain_result(
            "A1",
            "fail",
            "continuous_background_script_missing",
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
            print(f"a1_continuous_bluetooth_address_lookup_error={type(exc).__name__}:{exc}", flush=True)

    profile = resolve_audio_profile("normal")
    round_specs: list[dict[str, object]] = []
    for round_idx in range(round_count):
        sentence_seed = deterministic_sentence_seed(args, "A1", "continuous", round_idx)
        sentence = pick_short_commands(sentence_seed, 1)[0]
        wav_path = output_dir / f"ble-stream-continuous-round{round_idx + 1}-normal.wav"
        generate_profile_tts_wav(
            wav_path,
            sentence,
            tts_rate=int(profile["tts_rate"]),
            tts_gain=float(profile["tts_gain"]),
        )
        round_specs.append(
            {
                "round": round_idx + 1,
                "label": f"continuous_round{round_idx + 1}_normal",
                "sentence": sentence,
                "wav_path": str(wav_path.resolve()),
            }
        )
    spec_path = output_dir / "background_rounds_spec.json"
    spec_path.write_text(json.dumps(round_specs, ensure_ascii=False, indent=2), encoding="utf-8")

    command = [
        "powershell.exe",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(script_path),
        "-Port",
        args.port,
        "-DeviceName",
        args.device_name,
        "-RoundSpecJson",
        str(spec_path.resolve()),
        "-OutDir",
        str(output_dir.resolve()),
        "-FirmwareRepo",
        str(firmware_repo_root()),
        "-MaxHiddenToVisibleSeconds",
        str(float(args.a1_continuous_max_hidden_to_visible_seconds)),
    ]
    if not bool(args.a1_short_check_accuracy):
        command.append("-SkipAccuracyGate")
    if bluetooth_address:
        command.extend(["-BluetoothAddress", bluetooth_address])
    if args.listener_exe:
        command.extend(["-ListenerExe", str(pathlib.Path(args.listener_exe).resolve())])

    print(f"a1_continuous_listener_type_repo={listener_repo}", flush=True)
    print(f"a1_continuous_background_script={script_path}", flush=True)
    print(f"a1_continuous_bluetooth_address_source={bluetooth_address_source}", flush=True)
    if bluetooth_address:
        print(f"a1_continuous_bluetooth_address={bluetooth_address}", flush=True)
    print(f"a1_continuous_round_spec={spec_path}", flush=True)
    print(f"a1_continuous_stdout={stdout_log}", flush=True)
    print(f"a1_continuous_stderr={stderr_log}", flush=True)

    timeout_seconds = max(120, int(round_count) * 60)
    try:
        completed = run_with_process_tree_timeout(
            command,
            cwd=str(listener_repo),
            timeout=timeout_seconds,
        )
    except subprocess.TimeoutExpired as exc:
        stdout_text = exc.stdout if isinstance(exc.stdout, str) else (exc.stdout or b"").decode("utf-8", errors="replace")
        stderr_text = exc.stderr if isinstance(exc.stderr, str) else (exc.stderr or b"").decode("utf-8", errors="replace")
        stdout_log.write_text(stdout_text, encoding="utf-8", errors="replace")
        stderr_log.write_text(stderr_text, encoding="utf-8", errors="replace")
        return make_product_chain_result(
            "A1",
            "fail",
            "continuous_background_rounds_timeout",
            {
                "timeout_seconds": timeout_seconds,
                "listener_type_stdout_path": str(stdout_log),
                "listener_type_stderr_path": str(stderr_log),
            },
        )

    stdout_log.write_text(completed.stdout or "", encoding="utf-8", errors="replace")
    stderr_log.write_text(completed.stderr or "", encoding="utf-8", errors="replace")
    try:
        report = extract_prefixed_json(completed.stdout or "", "ble_background_rounds_result_json=")
    except json.JSONDecodeError as exc:
        return make_product_chain_result(
            "A1",
            "fail",
            "continuous_background_rounds_report_json_invalid",
            {
                "json_error": str(exc),
                "returncode": completed.returncode,
                "listener_type_stdout_path": str(stdout_log),
                "listener_type_stderr_path": str(stderr_log),
            },
        )
    if report is None:
        return make_product_chain_result(
            "A1",
            "fail",
            "continuous_background_rounds_report_missing",
            {
                "returncode": completed.returncode,
                "listener_type_stdout_path": str(stdout_log),
                "listener_type_stderr_path": str(stderr_log),
            },
        )

    rounds = report.get("rounds")
    if not isinstance(rounds, list):
        rounds = []
    failures: list[str] = []
    for item in rounds:
        if not isinstance(item, dict):
            continue
        score = score_transcript_accuracy(item.get("expected_text"), item.get("transcript"))
        item["accuracy_details"] = score
        round_no = item.get("round")
        status = str(item.get("status") or "FAIL").lower()
        failure_text = ",".join(str(value) for value in item.get("failures") or [])
        warning_text = ",".join(str(value) for value in item.get("warnings") or [])
        print(f"a1_continuous_round_result={round_no}:{status}:{failure_text}", flush=True)
        if warning_text:
            print(f"a1_continuous_round_warnings={round_no}:{warning_text}", flush=True)
        hidden_latency = item.get("hidden_to_visible_seconds")
        if hidden_latency is not None:
            print(f"a1_continuous_hidden_to_visible_seconds=round{round_no}:{hidden_latency}", flush=True)
        if status != "pass":
            failures.append(f"continuous_round{round_no}")

    report_status = str(report.get("status") or "FAIL").upper()
    result = "pass" if completed.returncode == 0 and report_status == "PASS" and not failures else "fail"
    reason = "" if result == "pass" else "continuous_background_rounds_failed"
    details = {
        "continuous_type": True,
        "accuracy_gate_skipped": not bool(args.a1_short_check_accuracy),
        "listener_type_repo": str(listener_repo),
        "listener_type_stdout_path": str(stdout_log),
        "listener_type_stderr_path": str(stderr_log),
        "listener_type_report_path": latest_matching_file(output_dir, "background-rounds-*.json"),
        "round_spec_path": str(spec_path),
        "rounds": rounds,
        "listener_type_report": report,
        "max_hidden_to_visible_seconds": float(args.a1_continuous_max_hidden_to_visible_seconds),
    }
    return make_product_chain_result("A1", result, reason, details)


async def run_a1(args) -> dict[str, object]:
    """A1: three restart-Type short rounds plus three continuous-Type short rounds."""
    restart_count, continuous_count = resolve_a1_round_plan(args)
    budget = max(180, (restart_count + continuous_count) * 90)
    print_case_header("A1", "short recordings: 3 restart rounds + 3 continuous Type rounds", budget)
    print(f"transport_capture_skipped=A1:product_chain_primary", flush=True)
    print(f"a1_restart_round_count={restart_count}", flush=True)
    print(f"a1_continuous_round_count={continuous_count}", flush=True)
    print(
        f"a1_continuous_max_hidden_to_visible_seconds={float(args.a1_continuous_max_hidden_to_visible_seconds):.3f}",
        flush=True,
    )
    print(
        f"a1_short_accuracy_gate={'enabled' if args.a1_short_check_accuracy else 'evidence_only'}",
        flush=True,
    )

    restart_results: list[dict[str, object]] = []
    capsule_checks: list[dict[str, object]] = []
    failures: list[str] = []
    warnings: list[str] = []
    skip_short_accuracy_gate = not bool(args.a1_short_check_accuracy)

    for round_idx in range(restart_count):
        sentence_seed = deterministic_sentence_seed(args, "A1", "restart", round_idx)
        sentence = pick_short_commands(sentence_seed, 1)[0]
        print(f"a1_restart_round={round_idx + 1}/{restart_count}:sentence={sentence}", flush=True)
        product_chain = await run_listener_type_product_chain(
            args,
            "A1",
            trigger_mode="serial-toggle",
            artifact_label=f"restart_round{round_idx + 1}_normal",
            audio_profile="normal",
            sentence_override=sentence,
            skip_accuracy_gate=skip_short_accuracy_gate,
        )
        restart_results.append(product_chain)
        result_str = str(product_chain.get("result"))
        if result_str == "fail":
            failures.append(f"restart_round{round_idx + 1}")
        elif result_str == "warning":
            warnings.append(f"restart_round{round_idx + 1}")
        check = validate_capsule_evidence("A1", product_chain, expect_partial=False, expect_no_text=False)
        capsule_checks.append(check)
        print(f"capsule_evidence=A1:restart_round{round_idx + 1}:{check.get('pass')}:{check.get('reason')}", flush=True)
        if result_str == "fail" and not args.continue_on_failure:
            break

    continuous_result: dict[str, object] | None = None
    if continuous_count > 0 and not failures:
        continuous_result = await run_listener_type_background_rounds(args, round_count=continuous_count)
        if str(continuous_result.get("result")) == "fail":
            failures.append("continuous_background_rounds")
        elif str(continuous_result.get("result")) == "warning":
            warnings.append("continuous_background_rounds")

    details: dict[str, object] = {
        "transport_capture_skipped": True,
        "transport_skip_reason": "product_chain_primary_requires_capsule_before_playback",
        "restart_rounds": restart_results,
        "continuous_rounds": continuous_result,
        "capsule_evidence": capsule_checks,
        "restart_round_count": restart_count,
        "continuous_round_count": continuous_count,
        "short_accuracy_gate": "enabled" if args.a1_short_check_accuracy else "evidence_only",
    }

    if failures:
        result = "fail"
        reason = "product_chain_failed:" + ",".join(failures)
    elif warnings:
        result = "warning"
        reason = "product_chain_warning:" + ",".join(warnings)
    else:
        result = "pass"
        reason = ""

    return print_summary("A1", result, reason, details=details)


async def run_a2(args) -> dict[str, object]:
    profiles = product_chain_profiles_for_case(args, "A2")
    budget = max(300, len(profiles) * A2_PRODUCT_CHAIN_MIN_TIMEOUT_SECONDS)
    return await run_product_chain_primary_case(
        args,
        "A2",
        "single long recording product chain",
        budget,
    )


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


def pick_short_dictation_text(
    seed: int,
    sentence_count: int,
    pool: tuple[str, ...] = SHORT_DICTATION_SENTENCES,
) -> str:
    rng = random.Random(seed)
    candidates = list(pool)
    picked: list[str] = []
    while len(picked) < max(1, int(sentence_count)):
        if not candidates:
            candidates = list(pool)
        index = rng.randrange(len(candidates))
        picked.append(candidates.pop(index))
    return "".join(picked)


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
    skip_accuracy_gate: bool = False,
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
            elif case_id in SHORT_DICTATION_CASES:
                short_pool = (
                    LOW_VOLUME_DICTATION_SENTENCES
                    if profile_name == "low-volume"
                    else SHORT_DICTATION_SENTENCES
                )
                expected_sentence = pick_short_dictation_text(
                    sentence_seed,
                    random_sentence_count,
                    short_pool,
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
        "-TtsRate",
        str(profile_tts_rate),
        "-PlaybackVolumePercent",
        str(args.playback_volume_percent),
        "-AudioProfile",
        profile_name,
        "-FirmwareRepo",
        str(firmware_repo_root()),
        "-VerifyHistory",
    ]
    if bluetooth_address:
        command.extend(["-BluetoothAddress", bluetooth_address])
    if args.listener_exe:
        command.extend(["-ListenerExe", str(pathlib.Path(args.listener_exe).resolve())])
    if generated_wav_path is not None:
        command.extend(["-WavPath", str(generated_wav_path.resolve())])
    if expected_sentence:
        command.extend(["-Sentence", expected_sentence])
        command.extend(["-ExpectedText", expected_sentence])
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
    if args.keep_playback_volume:
        command.append("-KeepPlaybackVolume")
    if skip_accuracy_gate:
        command.append("-SkipAccuracyGate")

    print(f"product_chain_trigger_mode={trigger_mode}", flush=True)
    print(f"product_chain_artifact_label={trigger_label}", flush=True)
    print(f"product_chain_audio_profile={profile_name}", flush=True)
    print(f"product_chain_tts_rate={profile_tts_rate}", flush=True)
    print(f"product_chain_tts_gain={profile_tts_gain}", flush=True)
    print(f"product_chain_listener_type_repo={listener_repo}", flush=True)
    if args.listener_exe:
        print(f"product_chain_listener_exe={pathlib.Path(args.listener_exe).resolve()}", flush=True)
    print(f"product_chain_bluetooth_address_source={bluetooth_address_source}", flush=True)
    print(f"product_chain_random_sentence_count={random_sentence_count}", flush=True)
    print(f"product_chain_expect_no_text={1 if expect_no_text else 0}", flush=True)
    print(f"product_chain_accuracy_gate_skipped={1 if skip_accuracy_gate else 0}", flush=True)
    print(f"product_chain_listener_timeout_ms={listener_timeout_ms}", flush=True)
    if bluetooth_address:
        print(f"product_chain_bluetooth_address={bluetooth_address}", flush=True)
    print(f"product_chain_listener_stdout={stdout_log}", flush=True)
    print(f"product_chain_listener_stderr={stderr_log}", flush=True)

    try:
        completed = run_with_process_tree_timeout(
            command,
            cwd=str(listener_repo),
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
        try:
            fallback_pcm_bytes = int(report.get("pcm_bytes") or 0)
        except (TypeError, ValueError):
            fallback_pcm_bytes = 0
        try:
            fallback_playback_ms = int(report.get("record_playback_actual_ms") or 0)
        except (TypeError, ValueError):
            fallback_playback_ms = 0
        history_fallback_eligible = (
            not expect_no_text
            and (completed.returncode == 0 or fallback_pcm_bytes > 0 or fallback_playback_ms > 0)
        )
        if history_fallback_eligible:
            history_session = find_listener_history_session(
                history_path=report.get("history_path"),
                transcript=transcript,
                expected_pcm_bytes=report.get("pcm_bytes"),
            ) or {}
            if history_session:
                report["history_session"] = history_session
                report["history_lookup_fallback"] = True
        else:
            history_session = {}
            report["history_lookup_fallback_skipped"] = "no_current_recording_evidence"
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

    no_text_acceptance: dict[str, object] | None = None
    if expect_no_text:
        history_text = longest_non_empty(
            history_session.get("rawTranscript"),
            history_session.get("finalText"),
        )
        serial_report_text = first_non_empty(report.get("serial_report"))
        history_is_stale = history_session_is_stale_for_report(report, history_session)
        expected_stream_failure = first_non_empty(report.get("expected_stream_failure"))
        try:
            report_pcm_bytes = int(report.get("pcm_bytes") or 0)
        except (TypeError, ValueError):
            report_pcm_bytes = -1
        cancel_negative_ok = (
            trigger_mode == "serial-cancel"
            and bool(expected_stream_failure)
            and report_pcm_bytes == 0
            and (not history_session or history_is_stale)
            and not first_non_empty(report.get("recording_archive_path"))
            and not inserted_text
            and not insertion_verified
            and "cancel_completed=True" in serial_report_text
        )
        if cancel_negative_ok:
            no_text_acceptance = {
                "accepted": True,
                "reason": "serial_cancel_expected_stream_failure_without_current_history",
                "expected_stream_failure": expected_stream_failure,
                "ignored_stale_history_session_id": history_session.get("id"),
                "ignored_stale_history_created_at": history_session.get("createdAt"),
                "raw_verification_errors": verification_errors,
                "raw_transcript": transcript,
            }
            result = "pass"
            reason = ""
            transcript = ""
            history_session = {}
            embedded_stats = {}
            insert_status = ""
            verification_errors = []
            report["status_after_no_text_acceptance"] = "PASS"
        elif completed.returncode != 0:
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
    if not expect_no_text and not skip_accuracy_gate:
        result, reason = apply_accuracy_gate(
            current_result=result,
            current_reason=reason,
            audio_profile=profile,
            accuracy_details=accuracy_details,
        )

    effective_status = "PASS" if result == "pass" else "WARNING" if result == "warning" else "FAIL"
    expected_packet_count = None
    received_packet_count = None
    missing_packet_count = report.get("missing_packets")
    duplicate_packet_count = None
    if isinstance(embedded_stats, dict):
        expected_packet_count = embedded_stats.get("expectedPacketCount")
        received_packet_count = embedded_stats.get("receivedPacketCount")
        missing_packet_count = first_non_empty(
            embedded_stats.get("missingPacketCount"),
            report.get("missing_packets"),
        )
        duplicate_packet_count = embedded_stats.get("duplicatePacketCount")
    details = {
        "full_chain_status": effective_status,
        "listener_type_report_status": report_status,
        "full_chain_returncode": completed.returncode,
        "trigger_mode": trigger_mode,
        "artifact_label": trigger_label,
        "audio_profile": profile_name,
        "accuracy_gate_skipped": skip_accuracy_gate,
        "source_tts_rate": profile_tts_rate,
        "source_tts_gain": profile_tts_gain,
        "generated_wav_path": str(generated_wav_path) if generated_wav_path is not None else None,
        "expect_no_text": expect_no_text,
        "listener_type_repo": str(listener_repo),
        "listener_type_stdout_path": str(stdout_log),
        "listener_type_stderr_path": str(stderr_log),
        "listener_type_report_path": latest_matching_file(output_dir, "ble-stream-smoke.*.json"),
        "listener_type_log_refs": [
            value
            for value in (
                str(stdout_log),
                str(stderr_log),
                first_non_empty(report.get("log_path")),
            )
            if value
        ],
        "firmware_diag_refs": [
            value
            for value in (
                first_non_empty(report.get("serial_log_path")),
                first_non_empty(report.get("firmware_diag_path")),
                first_non_empty(report.get("diag_log_path")),
            )
            if value
        ],
        "sentence": report.get("sentence"),
        **accuracy_details,
        "transcript": transcript,
        "final_text": first_non_empty(report.get("final_text"), transcript),
        "partial_preview_count": report.get("partial_preview_count"),
        "last_partial_preview": report.get("last_partial_preview"),
        "asr_text_update_count": report.get("asr_text_update_count"),
        "asr_text_updates": report.get("asr_text_updates"),
        "insert_status": insert_status,
        "inserted_text": inserted_text,
        "insertion_verified": insertion_verified,
        "history_session_id": history_session.get("id"),
        "expected_packet_count": expected_packet_count,
        "received_packet_count": received_packet_count,
        "missing_packet_count": missing_packet_count,
        "duplicate_packet_count": duplicate_packet_count,
        "missing_packets": report.get("missing_packets"),
        "pcm_bytes": report.get("pcm_bytes"),
        "embedded_audio_stats": embedded_stats,
        "recording_archive_path": report.get("recording_archive_path"),
        "timeline": report.get("timeline"),
        "verify_insertion": bool(report.get("verify_insertion")),
        "verify_history": bool(report.get("verify_history")),
        "history_lookup_fallback": history_lookup_fallback,
        "verification_errors": verification_errors,
        "no_text_acceptance": no_text_acceptance,
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


CASE_RUNNERS = {
    "A1": run_a1,
    "A2": run_a2,
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


def compact_product_chain_attempt(product_chain: dict[str, object]) -> dict[str, object]:
    details = product_chain.get("details")
    if not isinstance(details, dict):
        details = {}
    return {
        "case_id": product_chain.get("case_id"),
        "result": product_chain.get("result"),
        "reason": product_chain.get("reason"),
        "details": {
            "full_chain_status": details.get("full_chain_status"),
            "listener_type_report_status": details.get("listener_type_report_status"),
            "full_chain_returncode": details.get("full_chain_returncode"),
            "artifact_label": details.get("artifact_label"),
            "audio_profile": details.get("audio_profile"),
            "sentence": details.get("sentence"),
            "transcript": details.get("transcript"),
            "accuracy": details.get("accuracy"),
            "cer": details.get("cer"),
            "insert_status": details.get("insert_status"),
            "history_session_id": details.get("history_session_id"),
            "missing_packets": details.get("missing_packets"),
            "pcm_bytes": details.get("pcm_bytes"),
            "embedded_audio_stats": details.get("embedded_audio_stats"),
            "listener_type_report_path": details.get("listener_type_report_path"),
        },
    }


def product_chain_timeline(details: object) -> dict[str, object] | None:
    if not isinstance(details, dict):
        return None
    timeline = details.get("timeline")
    if isinstance(timeline, dict):
        return timeline
    report = details.get("listener_type_report")
    if isinstance(report, dict):
        timeline = report.get("timeline")
        if isinstance(timeline, dict):
            return timeline
    return None


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
                retry_details["initial_attempt"] = compact_product_chain_attempt(product_chain)
            if str(retry_product_chain.get("result")) == "pass":
                product_chain = retry_product_chain
            else:
                details = product_chain.get("details")
                if isinstance(details, dict):
                    details["retry_attempt"] = compact_product_chain_attempt(retry_product_chain)
        profile_results.append(product_chain)
        product_result = str(product_chain.get("result"))
        if product_result == "fail":
            failures.append(profile_name)
            if not args.continue_on_failure:
                print(f"product_chain_profile_stop={case_id}:{profile_name}:fail", flush=True)
                break
        elif product_result == "warning":
            warnings.append(profile_name)
            if args.fail_on_warning and not args.continue_on_failure:
                print(f"product_chain_profile_stop={case_id}:{profile_name}:warning", flush=True)
                break

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

    capsule_checks = []
    for pc in profile_results:
        pc_details = pc.get("details") if isinstance(pc.get("details"), dict) else {}
        is_no_text = bool(pc_details.get("expect_no_text"))
        check = validate_capsule_evidence(
            case_id, pc,
            expect_partial=not is_no_text,
            expect_no_text=is_no_text,
        )
        capsule_checks.append(check)
        print(f"capsule_evidence={case_id}:{check.get('pass')}:{check.get('reason')}", flush=True)
    details["capsule_evidence"] = capsule_checks
    failed_capsule = [c for c in capsule_checks if not c.get("pass") and c.get("capsule_validated")]
    if failed_capsule and str(case_result.get("result")) == "pass":
        case_result["result"] = "warning"
        case_result["reason"] = "capsule_evidence_warning:" + ";".join(
            str(c.get("reason")) for c in failed_capsule
        )

    print(
        f"product_chain_overlay_done={case_id}:profiles={','.join(profiles)}:failed={len(failures)}:warnings={len(warnings)}:capsule_failed={len(failed_capsule)}",
        flush=True,
    )
    return case_result


async def main_async(args) -> None:
    if args.skip_preflight_recover:
        args.preflight_recover_mode = "none"

    case_order = resolve_case_execution_order(args)
    requested_suite = args.cases.strip().lower() if args.cases.strip().lower() in CASE_SUITES else "custom"
    run_metadata = matrix_run_metadata(args)
    print(f"execution_profile={args.execution_profile}", flush=True)
    print(f"random_capture_durations_enabled={0 if args.disable_random_capture_durations else 1}", flush=True)
    print(f"random_usage_timing_enabled={0 if args.disable_random_usage_timing else 1}", flush=True)
    print(f"shuffle_auto_case_order={1 if args.shuffle_auto_case_order else 0}", flush=True)
    print(f"preflight_recover_mode={args.preflight_recover_mode}", flush=True)
    print(f"product_chain_enabled={0 if args.transport_only else 1}", flush=True)
    print(f"playback_volume_percent={args.playback_volume_percent}", flush=True)
    print(f"keep_playback_volume={1 if args.keep_playback_volume else 0}", flush=True)
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
            results.append(skipped_case_summary(case_id, MANUAL_OR_EXTERNAL_CASES[case_id]))
            continue
        runner = CASE_RUNNERS.get(case_id)
        if runner is None:
            results.append(skipped_case_summary(case_id, "unknown_or_not_automated"))
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
        metadata=run_metadata,
        requested_cases=case_order,
        case_suite=requested_suite,
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
    configure_playback_volume(args)
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
