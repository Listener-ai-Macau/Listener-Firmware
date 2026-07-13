#!/usr/bin/env python3
"""Guard the Windows validation toolchain against recurring shell frictions."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


FIRMWARE_ROOT = Path(__file__).resolve().parents[1]
LISTENER_ROOT = FIRMWARE_ROOT.parent
DENZIC_ROOT = LISTENER_ROOT.parent
TYPE_ROOT = LISTENER_ROOT / "Listener-Type"
WORKFLOW_ROOT = DENZIC_ROOT / "ai-collaboration-workflow"


VALIDATION_CHAIN_FILES = [
    FIRMWARE_ROOT / "tools" / "verify_audio_ble_product_matrix.py",
    FIRMWARE_ROOT / "tools" / "ble_audio_regression_common.py",
    FIRMWARE_ROOT / "tools" / "capture_audio_ble_wav.py",
    FIRMWARE_ROOT / "tools" / "ensure_ble_hid_connection.ps1",
    FIRMWARE_ROOT / "tools" / "recover_ble_hid_host.ps1",
    FIRMWARE_ROOT / "tools" / "reset_listener_ble_host.ps1",
    TYPE_ROOT / "tools" / "embedded_audio_replay" / "run_ble_background_rounds.ps1",
    TYPE_ROOT / "tools" / "embedded_audio_replay" / "run_ble_stream_smoke.ps1",
]

PUBLIC_ENTRY_FILES = [
    FIRMWARE_ROOT / "README.md",
    FIRMWARE_ROOT / "CONTRIBUTING.md",
    FIRMWARE_ROOT / "AGENTS.md",
    FIRMWARE_ROOT / "CLAUDE.md",
    FIRMWARE_ROOT / "docs" / "tools" / "device_maintenance.md",
]

FIRMWARE_COMMAND_ENTRY_FILES = [
    FIRMWARE_ROOT / "tools" / "build_firmware.ps1",
    FIRMWARE_ROOT / "tools" / "build.ps1",
    FIRMWARE_ROOT / "tools" / "flash.ps1",
    FIRMWARE_ROOT / "tools" / "flash_usb_light_sleep_debug.ps1",
    FIRMWARE_ROOT / "tools" / "flash_bootloader_double_click.ps1",
    FIRMWARE_ROOT / "tools" / "flash_bootloader_double_click.cmd",
    FIRMWARE_ROOT / "tools" / "device_maintenance.ps1",
    FIRMWARE_ROOT / "tools" / "setup_windows.ps1",
    FIRMWARE_ROOT / "tools" / "test.ps1",
    FIRMWARE_ROOT / "tools" / "verify_ble_tts_asr_smoke.py",
    FIRMWARE_ROOT / "tools" / "inject_text.py",
]

HARDWARE_OUTPUT_ENTRY_FILES = [
    FIRMWARE_ROOT / "tools" / "status_led_human_effect_review.ps1",
    FIRMWARE_ROOT / "tools" / "status_led_camera_calibration.ps1",
    FIRMWARE_ROOT / "tools" / "status_led_manual_calibration.ps1",
    FIRMWARE_ROOT / "tools" / "verify_battery_only_auto_shutdown_manual.ps1",
    FIRMWARE_ROOT / "tools" / "verify_ble_repair_physical_guided.ps1",
    FIRMWARE_ROOT / "tools" / "verify_low_power_unplug_wake_hardware.ps1",
    FIRMWARE_ROOT / "tools" / "verify_pwr_hold_shutdown_hardware.ps1",
    FIRMWARE_ROOT / "tools" / "verify_status_led_hardware.ps1",
]

ALLOWED_IDF_TEXT_FILES = {
    "tools/idf.ps1",
    "tools/idf_env.ps1",
    "tools/verify_idf_env_static.ps1",
    "tools/verify_validation_tooling_hygiene.py",
}

FORBIDDEN_SHELL_PATTERNS = [
    (re.compile(r'["\']powershell(?:\.exe)?["\']', re.IGNORECASE), "invoke pwsh instead of Windows PowerShell"),
    (re.compile(r"&\s*powershell(?:\.exe)?\b", re.IGNORECASE), "invoke pwsh instead of Windows PowerShell"),
    (re.compile(r"(?im)(^|\s)powershell(?:\.exe)?\s+-(NoProfile|ExecutionPolicy|File|Command)\b"), "document or invoke pwsh instead of Windows PowerShell"),
    (re.compile(r"\bStart-Process\s+-FilePath\s+['\"]powershell(?:\.exe)?['\"]", re.IGNORECASE), "Start-Process must use pwsh"),
    (re.compile(r'["\']-ExecutionPolicy["\']|\s-ExecutionPolicy\s', re.IGNORECASE), "do not carry Windows PowerShell execution-policy shims into pwsh validation"),
]


def read(path: Path) -> str:
    if not path.exists():
        raise AssertionError(f"missing required validation tooling file: {path}")
    return path.read_text(encoding="utf-8", errors="replace")


def git_ls_files() -> list[str]:
    result = subprocess.run(
        ["git", "-C", str(FIRMWARE_ROOT), "ls-files"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )
    return [line.strip().replace("\\", "/") for line in result.stdout.splitlines() if line.strip()]


def main() -> int:
    failures: list[str] = []

    tracked = git_ls_files()
    for prefix in ("!docs/", "docs/validation/"):
        offenders = [path for path in tracked if path.startswith(prefix)]
        if offenders:
            preview = ", ".join(offenders[:8])
            suffix = f", ... +{len(offenders) - 8}" if len(offenders) > 8 else ""
            failures.append(f"tracked generated/legacy docs are not allowed under {prefix}: {preview}{suffix}")

    for path in PUBLIC_ENTRY_FILES:
        text = read(path)
        if "!docs" in text:
            failures.append(f"{path.relative_to(DENZIC_ROOT)}: public entry docs must point to docs/, not !docs/")

    for path in [*VALIDATION_CHAIN_FILES, *PUBLIC_ENTRY_FILES, *FIRMWARE_COMMAND_ENTRY_FILES]:
        text = read(path)
        for pattern, message in FORBIDDEN_SHELL_PATTERNS:
            match = pattern.search(text)
            if match:
                failures.append(f"{path.relative_to(DENZIC_ROOT)}: {message}: {match.group(0)!r}")

    for path in HARDWARE_OUTPUT_ENTRY_FILES:
        text = read(path)
        if "docs/validation" in text or "docs\\validation" in text:
            failures.append(f"{path.relative_to(DENZIC_ROOT)}: default generated validation output must stay under .cache/validation")

    for rel in tracked:
        if not rel.startswith("tools/") or rel in ALLOWED_IDF_TEXT_FILES:
            continue
        if not rel.lower().endswith((".ps1", ".py", ".cmd")):
            continue
        text = read(FIRMWARE_ROOT / rel)
        if "idf.py" in text:
            failures.append(f"{rel}: use tools/idf.ps1 or tools/build.ps1 instead of mentioning or invoking raw idf.py")

    serial_capture = read(FIRMWARE_ROOT / "tools" / "send_serial_and_capture.ps1")
    if "[int]$CaptureSeconds" not in serial_capture:
        failures.append("tools/send_serial_and_capture.ps1: must accept -CaptureSeconds as a safe shorthand")
    if '$PSBoundParameters.ContainsKey("CaptureSeconds")' not in serial_capture:
        failures.append("tools/send_serial_and_capture.ps1: -CaptureSeconds must be explicitly mapped to CommandReadMs")
    if "--command-read-ms" not in serial_capture:
        failures.append("tools/send_serial_and_capture.ps1: must still delegate exact command read timing to serial_no_reset_capture.py")
    if "Unfiltered DIAGLOG:LAST:" not in serial_capture or "DIAGLOG:LAST:N:source" not in serial_capture:
        failures.append("tools/send_serial_and_capture.ps1: must reject watchdog-risky unfiltered diagnostic dumps and direct callers to bounded source tails")
    if "^~?DIAGLOG:DUMP$" not in serial_capture or "-gt 128" not in serial_capture:
        failures.append("tools/send_serial_and_capture.ps1: diagnostic safety gate must reject unbounded DUMP and unfiltered LAST tails above 128 events")

    ensure_ble = read(FIRMWARE_ROOT / "tools" / "ensure_ble_hid_connection.ps1")
    if "Microsoft.Windows.SDK.NET.dll" not in ensure_ble or "WinRT.Runtime.dll" not in ensure_ble:
        failures.append("tools/ensure_ble_hid_connection.ps1: pwsh BLE checks must load the Windows SDK WinRT projection")

    quickstart = read(WORKFLOW_ROOT / "docs" / "agent_quickstart.md")
    verify_pwsh7 = read(WORKFLOW_ROOT / "scripts" / "verify_pwsh7.ps1")
    if "pwsh -NoProfile -File <workflow-repo>\\scripts\\verify_pwsh7.ps1" not in quickstart:
        failures.append("ai-collaboration-workflow/docs/agent_quickstart.md: startup PowerShell gate must use the -File verify_pwsh7.ps1 entrypoint")
    if "scripts\\verify_pwsh7.ps1" not in quickstart:
        failures.append("ai-collaboration-workflow/docs/agent_quickstart.md: nested PowerShell guidance must point agents to verify_pwsh7.ps1")
    if "outer double-quoted `pwsh -Command" not in quickstart:
        failures.append("ai-collaboration-workflow/docs/agent_quickstart.md: must keep the nested pwsh -Command quoting rule visible")
    if "wrap the inner command in single quotes or escape" not in quickstart:
        failures.append("ai-collaboration-workflow/docs/agent_quickstart.md: must explain single-quote/escape handling for nested pwsh commands")
    if "#requires -Version 7.0" not in verify_pwsh7:
        failures.append("ai-collaboration-workflow/scripts/verify_pwsh7.ps1: must fail early outside PowerShell 7")
    if "Get-Command pwsh -CommandType Application" not in verify_pwsh7:
        failures.append("ai-collaboration-workflow/scripts/verify_pwsh7.ps1: must report the resolved pwsh executable")

    if failures:
        print("FAIL: validation tooling hygiene checks failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print("PASS: validation tooling hygiene keeps BLE/recording scripts on pwsh, keeps IDF access behind tools/idf.ps1, keeps generated evidence out of docs, and preserves serial capture shorthands.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
