# Firmware Low Power Sleep Drain Telemetry

Date: 2026-05-30
Agent: oai1
Worktree: `voice-keyboard-firmware-wt-oai1-firmware-low-power-overnight-drain-1.1`

## Scope

Added first-class sleep drain telemetry to `~POWER:STATUS` so overnight validation can record sleep-entry battery, post-wake battery, RTC-derived sleep duration, and per-hour drain rate without external estimation.

New status fields:

- `sleep_stats_valid`
- `sleep_duration_ms`
- `sleep_entry_battery_mv`
- `sleep_entry_battery_level`
- `sleep_entry_battery_valid`
- `wake_battery_mv`
- `wake_battery_level`
- `wake_battery_valid`
- `sleep_drain_mv`
- `sleep_drain_level`
- `sleep_drain_mv_per_hour`
- `sleep_drain_level_per_hour_x100`

The firmware still reports current `battery_mv` and `battery_level` separately.

## Validation

```powershell
python .\tools\verify_power_manager_static.py
```

Result: PASS - power manager static verification covers states, blockers, idle actions, diagnostics, and sleep path.

```powershell
pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1
```

Result: PASS - wrapper invokes the same static verification successfully.

```powershell
pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check
```

Result: PASS - firmware repo feature script is present, concise, and covers ESP32-S3 BLE HID/audio diagnostics.

```powershell
git diff --check
```

Result: PASS - no whitespace errors.

```powershell
pwsh -NoProfile -File .\tools\build_firmware.ps1
```

Result: PASS - ESP-IDF build completed and generated `build/voice-keyboard-firmware.bin`.

## Remaining Hardware Evidence

The new telemetry is build-validated but not yet flashed and overnight-soak validated. Final 1.1 review evidence should flash this build, capture a pre-sleep `~POWER:STATUS`, let the device enter overnight sleep, wake with KEY4/GPIO21, and capture the first post-wake `~POWER:STATUS` showing the new fields.
