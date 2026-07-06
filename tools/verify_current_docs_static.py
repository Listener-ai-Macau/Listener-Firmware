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
        "obsolete idle policy: low-power idle now keeps restrained PWR visible and reconnecting/connected/TYPE_READY BLE dark",
    ),
    PatternRule(
        re.compile(r"turns routine status LEDs off", re.IGNORECASE),
        "obsolete idle policy: low-power idle clears active-work/BLE LEDs, not restrained PWR",
    ),
    PatternRule(
        re.compile(r"disables routine status LED output", re.IGNORECASE),
        "obsolete idle policy: low-power rendering still owns restrained PWR while reconnecting/connected/TYPE_READY BLE is dark",
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
        "ambiguous stale wording: current low-power idle keeps restrained PWR visible while reconnecting/connected/TYPE_READY BLE is dark",
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
        re.compile(r"ordinary HID-only BLE connected stays visible as steady blue", re.IGNORECASE),
        "old BLE LED policy: HID-only connected may only use the bounded find-Type double flash until Listener-Type is ready",
    ),
    PatternRule(
        re.compile(r"low-base blue double-flash heartbeat", re.IGNORECASE),
        "old BLE LED policy: HID-only connected no longer has a blue heartbeat",
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
    PatternRule(
        re.compile(r"200 ms pending single-click/custom-key response and recovery double-click window", re.IGNORECASE),
        "stale EC11 recovery prose: EC11 physical double-click recovery must keep the validated 500 ms shared window",
    ),
    PatternRule(
        re.compile(r"450 ms pending single-click/custom-key response and recovery double-click window", re.IGNORECASE),
        "stale EC11 recovery prose: EC11 physical double-click recovery must keep the validated 500 ms shared window",
    ),
    PatternRule(
        re.compile(r"650 ms (?:physical )?double-click decision window", re.IGNORECASE),
        "stale EC11 recovery prose: EC11 and KEY1-KEY4 now share a 500 ms double-click decision window",
    ),
    PatternRule(
        re.compile(r"30 ms stable-press threshold", re.IGNORECASE),
        "stale EC11 push prose: EC11 debounce must match the validated 20 ms KEY1-KEY4 debounce feel",
    ),
    PatternRule(
        re.compile(r"current sense\s+on GPIO10/GPIO9", re.IGNORECASE),
        "stale V2 current-telemetry prose: GPIO10 is BAT_V_ADC and GPIO9 is PWR_HOLD; current sense is not populated",
    ),
    PatternRule(
        re.compile(r"(?m)^idf\.py build\s*$", re.IGNORECASE),
        "stale ESP-IDF shell guidance: firmware builds must use tools/build.ps1",
    ),
]


REQUIRED_TEXT = [
    RequiredText(
        "docs/features/status_led.md",
        re.compile(r"keeps PWR visible and leaves reconnecting/connected/TYPE_READY BLE dark", re.IGNORECASE),
        "status LED feature doc must state the current low-power idle PWR-only/BLE-dark behavior",
    ),
    RequiredText(
        "docs/features/status_led.md",
        re.compile(r"ordinary HID-only `connected` uses a low-floor bounded blue double-flash Type-search cue", re.IGNORECASE),
        "status LED feature doc must state that HID-only connected uses the swapped low-floor bounded find-Type cue instead of steady blue",
    ),
    RequiredText(
        "docs/features/status_led.md",
        re.compile(r"advances the white head at `STATUS_LED_EC11_ROTATION_STEP_MS`", re.IGNORECASE),
        "status LED feature doc must preserve the current EC11 fixed-step directional rotation cue",
    ),
    RequiredText(
        "docs/features/status_led.md",
        re.compile(r"accumulated detent direction", re.IGNORECASE),
        "status LED feature doc must preserve the EC11 accumulated-direction bounce filter",
    ),
    RequiredText(
        "docs/features/status_led.md",
        re.compile(r"EC11 push keeps the same 20 ms debounce model and 500 ms double-click decision window as KEY1-KEY4", re.IGNORECASE),
        "status LED feature doc must state EC11 keeps the validated shared double-click recovery window",
    ),
    RequiredText(
        "docs/features/low_power_wake_policy.md",
        re.compile(r"leaves reconnecting/connected/TYPE_READY BLE dark", re.IGNORECASE),
        "low-power policy doc must state the current low-power idle PWR-only/BLE-dark behavior",
    ),
    RequiredText(
        "docs/features/low_power_wake_policy.md",
        re.compile(r"ordinary HID-only BLE connected uses a low-floor bounded blue double-flash Type-search cue", re.IGNORECASE),
        "low-power policy doc must state that HID-only connected uses the swapped low-floor bounded find-Type cue while active",
    ),
    RequiredText(
        "docs/features/firmware-feature-map.md",
        re.compile(r"keeps restrained PWR visible and reconnecting/connected/TYPE_READY BLE dark in idle", re.IGNORECASE),
        "feature map must summarize current idle LED behavior without implying all-off; keeps restrained PWR/BLE status visible",
    ),
    RequiredText(
        "docs/features/firmware-feature-map.md",
        re.compile(r"keeps quiet ACTIVE HID-only BLE in a low-floor bounded blue double-flash Type-search cue", re.IGNORECASE),
        "feature map must summarize the swapped low-floor active HID-only find-Type BLE behavior",
    ),
    RequiredText(
        "tools/verify_status_led_static.py",
        re.compile(r"sleep all-off path"),
        "status LED static check must distinguish sleep all-off from low-power idle",
    ),
    RequiredText(
        "tools/verify_ble_status_led_connected_sync.ps1",
        re.compile(r"STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT\\s\+14U"),
        "BLE sync static check must use the current Type-ready brightness contract",
    ),
    RequiredText(
        "tools/verify_ble_status_led_connected_sync.ps1",
        re.compile(r"STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT\\s\+14U"),
        "BLE sync static check must use the current battery PWR active-window contract",
    ),
    RequiredText(
        "docs/features/factory_firmware_readiness.md",
        re.compile(r"PWR_HOLD/GPIO9"),
        "factory readiness doc must state the current PWR_HOLD/GPIO9 power latch",
    ),
    RequiredText(
        "docs/features/factory_firmware_readiness.md",
        re.compile(r"BAT_V_ADC/GPIO10"),
        "factory readiness doc must state the current battery ADC GPIO10 mapping",
    ),
    RequiredText(
        "docs/features/factory_firmware_readiness.md",
        re.compile(r"tools\\build\.ps1"),
        "factory readiness doc must use the ESP-IDF wrapper build command",
    ),
    RequiredText(
        "docs/features/coredump_debug_profile.md",
        re.compile(r"tools\\collect_coredump_debug\.ps1"),
        "coredump debug profile must document the Codex crash-triage entrypoint",
    ),
    RequiredText(
        "tools/collect_coredump_debug.ps1",
        re.compile(r"tools\\idf\.ps1"),
        "coredump debug tool must use the repository ESP-IDF wrapper",
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
