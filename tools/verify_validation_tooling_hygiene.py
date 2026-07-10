#!/usr/bin/env python3
"""Guard the Windows validation toolchain against recurring shell frictions."""

from __future__ import annotations

import re
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

FORBIDDEN_SHELL_PATTERNS = [
    (re.compile(r'["\']powershell(?:\.exe)?["\']', re.IGNORECASE), "invoke pwsh instead of Windows PowerShell"),
    (re.compile(r"&\s*powershell(?:\.exe)?\b", re.IGNORECASE), "invoke pwsh instead of Windows PowerShell"),
    (re.compile(r"\bStart-Process\s+-FilePath\s+['\"]powershell(?:\.exe)?['\"]", re.IGNORECASE), "Start-Process must use pwsh"),
    (re.compile(r'["\']-ExecutionPolicy["\']|\s-ExecutionPolicy\s', re.IGNORECASE), "do not carry Windows PowerShell execution-policy shims into pwsh validation"),
]


def read(path: Path) -> str:
    if not path.exists():
        raise AssertionError(f"missing required validation tooling file: {path}")
    return path.read_text(encoding="utf-8", errors="replace")


def main() -> int:
    failures: list[str] = []

    for path in VALIDATION_CHAIN_FILES:
        text = read(path)
        for pattern, message in FORBIDDEN_SHELL_PATTERNS:
            match = pattern.search(text)
            if match:
                failures.append(f"{path.relative_to(DENZIC_ROOT)}: {message}: {match.group(0)!r}")

    serial_capture = read(FIRMWARE_ROOT / "tools" / "send_serial_and_capture.ps1")
    if "[int]$CaptureSeconds" not in serial_capture:
        failures.append("tools/send_serial_and_capture.ps1: must accept -CaptureSeconds as a safe shorthand")
    if '$PSBoundParameters.ContainsKey("CaptureSeconds")' not in serial_capture:
        failures.append("tools/send_serial_and_capture.ps1: -CaptureSeconds must be explicitly mapped to CommandReadMs")
    if "--command-read-ms" not in serial_capture:
        failures.append("tools/send_serial_and_capture.ps1: must still delegate exact command read timing to serial_no_reset_capture.py")

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

    print("PASS: validation tooling hygiene keeps BLE/recording scripts on pwsh and preserves serial capture shorthands.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
