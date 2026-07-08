from pathlib import Path
import re
import sys


REPO_ROOT = Path(__file__).resolve().parents[1]


UNSAFE_PATTERNS = [
    (
        re.compile(r"serial\.Serial\s*\(\s*port\s*=", re.MULTILINE),
        "serial.Serial(port=...) opens before DTR/RTS can be forced low",
    ),
    (
        re.compile(r"serial\.Serial\s*\(\s*['\"]COM", re.MULTILINE),
        "serial.Serial('COM...') opens before DTR/RTS can be forced low",
    ),
    (
        re.compile(r"serial\.Serial\s*\(\s*port,", re.MULTILINE),
        "serial.Serial(port, ...) opens before DTR/RTS can be forced low",
    ),
]


SKIP_DIRS = {
    ".git",
    ".cache",
    "build",
    "managed_components",
    "docs",
    "tests",
}


def iter_tool_sources() -> list[Path]:
    tools_dir = REPO_ROOT / "tools"
    return sorted(
        path
        for path in tools_dir.rglob("*")
        if path.is_file()
        and path.suffix.lower() in {".py", ".ps1"}
        and path.name != Path(__file__).name
        and not any(part in SKIP_DIRS for part in path.parts)
    )


def main() -> int:
    failures: list[str] = []
    for path in iter_tool_sources():
        text = path.read_text(encoding="utf-8", errors="replace")
        rel = path.relative_to(REPO_ROOT).as_posix()
        for pattern, reason in UNSAFE_PATTERNS:
            if pattern.search(text):
                failures.append(f"{rel}: {reason}")

    status_led_hw = REPO_ROOT / "tools" / "verify_status_led_hardware.ps1"
    text = status_led_hw.read_text(encoding="utf-8", errors="replace")
    if re.search(r"ser\.dtr\s*=\s*True", text):
        failures.append(
            "tools/verify_status_led_hardware.ps1: DTR must not be asserted during ordinary status LED validation"
        )

    send_capture = REPO_ROOT / "tools" / "send_serial_and_capture.ps1"
    text = send_capture.read_text(encoding="utf-8", errors="replace")
    if "serial_no_reset_capture.py" not in text:
        failures.append(
            "tools/send_serial_and_capture.ps1: shared serial capture must delegate to the Python no-reset helper"
        )

    no_reset_capture = REPO_ROOT / "tools" / "serial_no_reset_capture.py"
    text = no_reset_capture.read_text(encoding="utf-8", errors="replace")
    if "def open_serial_no_reset(" not in text or "ser.dtr = False" not in text or "ser.rts = False" not in text:
        failures.append(
            "tools/serial_no_reset_capture.py: helper must force DTR/RTS low before and after open"
        )
    if (
        "def parse_read_ms_command(" not in text
        or "current_command_read_ms = args.command_read_ms" not in text
        or "command_read_ms=" not in text
    ):
        failures.append(
            "tools/serial_no_reset_capture.py: helper must support READMS:<ms> command-list entries for timing-sensitive no-reset captures"
        )

    camera_cal = REPO_ROOT / "tools" / "status_led_camera_calibration.ps1"
    text = camera_cal.read_text(encoding="utf-8", errors="replace")
    if "def open_serial_no_reset(" not in text or "with open_serial_no_reset(" not in text:
        failures.append(
            "tools/status_led_camera_calibration.ps1: camera calibration must use open_serial_no_reset"
        )

    if failures:
        print("FAIL: serial no-reset static verification failed")
        for failure in failures:
            print(f" - {failure}")
        return 1

    print("PASS: serial tools avoid DTR/RTS reset-prone open patterns.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
