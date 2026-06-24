from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
import sys


REPO_ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class PatternRule:
    pattern: re.Pattern[str]
    reason: str


@dataclass(frozen=True)
class RequiredText:
    path: str
    pattern: re.Pattern[str]
    reason: str


STALE_PATTERNS = [
    PatternRule(
        re.compile(r"routine status LEDs are turned off", re.IGNORECASE),
        "obsolete idle policy: low-power idle now keeps restrained PWR visible and connected/TYPE_READY BLE dark",
    ),
    PatternRule(
        re.compile(r"turns routine status LEDs off", re.IGNORECASE),
        "obsolete idle policy: low-power idle clears active-work/BLE LEDs, not restrained PWR",
    ),
    PatternRule(
        re.compile(r"disables routine status LED output", re.IGNORECASE),
        "obsolete idle policy: low-power rendering still owns restrained PWR while connected/TYPE_READY BLE is dark",
    ),
    PatternRule(
        re.compile(r"status LEDs went dark", re.IGNORECASE),
        "stale low-power validation prose: current idle should not imply full LED darkness",
    ),
    PatternRule(
        re.compile(r"low-power off path", re.IGNORECASE),
        "ambiguous stale wording: current low-power idle differs from sleep all-off",
    ),
    PatternRule(
        re.compile(r"low-power/LED-off", re.IGNORECASE),
        "ambiguous stale wording: current low-power idle keeps restrained PWR visible while connected/TYPE_READY BLE is dark",
    ),
    PatternRule(
        re.compile(r"turn off after idle status window", re.IGNORECASE),
        "old active-rendering wording should be expressed as handoff to low-power rendering",
    ),
    PatternRule(
        re.compile(r"STATUS_LED_BLE_CONNECTED_GENERIC_PERCENT\s+38U"),
        "old connected BLE brightness; current generic connected percent is lower",
    ),
    PatternRule(
        re.compile(r"\?\s*46U\s*:\s*0U"),
        "old battery PWR active-window brightness literal; use the current constant",
    ),
    PatternRule(
        re.compile(r"connected_ready\s*\?\s*30U"),
        "old battery PWR fallback tied to BLE connected state",
    ),
    PatternRule(
        re.compile(r"Continuous rotation is detent-step driven", re.IGNORECASE),
        "stale EC11 rotation prose: current cue is direction-driven and fixed-step rendered",
    ),
    PatternRule(
        re.compile(r"No time-based auto-spin", re.IGNORECASE),
        "stale EC11 rotation prose: current cue intentionally advances at STATUS_LED_EC11_ROTATION_STEP_MS",
    ),
    PatternRule(
        re.compile(r"head only advances on completed detents", re.IGNORECASE),
        "stale EC11 rotation prose: current cue keeps moving during the hold window",
    ),
]


REQUIRED_TEXT = [
    RequiredText(
        "docs/features/status_led.md",
        re.compile(r"keeps PWR visible and leaves connected/TYPE_READY BLE dark", re.IGNORECASE),
        "status LED feature doc must state the current low-power idle PWR-only/BLE-dark behavior",
    ),
    RequiredText(
        "docs/features/status_led.md",
        re.compile(r"advances the white head at `STATUS_LED_EC11_ROTATION_STEP_MS`", re.IGNORECASE),
        "status LED feature doc must preserve the current EC11 fixed-step directional rotation cue",
    ),
    RequiredText(
        "docs/features/low_power_wake_policy.md",
        re.compile(r"leaves connected/TYPE_READY BLE dark", re.IGNORECASE),
        "low-power policy doc must state the current low-power idle PWR-only/BLE-dark behavior",
    ),
    RequiredText(
        "docs/features/firmware-feature-map.md",
        re.compile(r"keeps restrained PWR visible and connected/TYPE_READY BLE dark only after idle is entered", re.IGNORECASE),
        "feature map must summarize current idle LED behavior without implying all-off; keeps restrained PWR/BLE status visible",
    ),
    RequiredText(
        "tools/verify_status_led_static.py",
        re.compile(r"sleep all-off path"),
        "status LED static check must distinguish sleep all-off from low-power idle",
    ),
    RequiredText(
        "tools/verify_ble_status_led_connected_sync.ps1",
        re.compile(r"STATUS_LED_BLE_CONNECTED_GENERIC_PERCENT\\s\+14U"),
        "BLE sync static check must use the current generic connected brightness contract",
    ),
    RequiredText(
        "tools/verify_ble_status_led_connected_sync.ps1",
        re.compile(r"STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT\\s\+14U"),
        "BLE sync static check must use the current battery PWR active-window contract",
    ),
]


def iter_current_text_files() -> list[Path]:
    candidates: list[Path] = []
    docs_features = REPO_ROOT / "docs" / "features"
    if docs_features.exists():
        candidates.extend(path for path in docs_features.rglob("*") if path.is_file())

    tools_dir = REPO_ROOT / "tools"
    if tools_dir.exists():
        candidates.extend(
            path
            for path in tools_dir.iterdir()
            if path.is_file()
            and path.name.startswith("verify_")
            and path.suffix.lower() in {".py", ".ps1"}
            and path.name != Path(__file__).name
        )
        repo_features = tools_dir / "ai" / "repo_features.ps1"
        if repo_features.exists():
            candidates.append(repo_features)

    return sorted(set(candidates))


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def main() -> int:
    failures: list[str] = []

    for path in iter_current_text_files():
        text = read_text(path)
        rel = path.relative_to(REPO_ROOT).as_posix()
        for rule in STALE_PATTERNS:
            match = rule.pattern.search(text)
            if match:
                failures.append(f"{rel}:{line_number(text, match.start())}: {rule.reason}")

    for requirement in REQUIRED_TEXT:
        path = REPO_ROOT / requirement.path
        if not path.exists():
            failures.append(f"{requirement.path}: missing file for current-doc check")
            continue
        text = read_text(path)
        if not requirement.pattern.search(text):
            failures.append(f"{requirement.path}: {requirement.reason}")

    if failures:
        print("FAIL: current docs static verification failed")
        for failure in failures:
            print(f" - {failure}")
        return 1

    print(
        "PASS: current docs/tools avoid stale low-power LED rollback cues "
        "and state the current PWR-only/BLE-dark idle contract."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
