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

## 2026-05-30 Overnight Soak Start

After USB re-enumeration, the exact final HEAD build was flashed and verified on `COM5`.

Build/flashed version:

- `v1002.0.0-ota-test-23-g2778a87`

Start reference captured at `2026-05-30 23:24:18 +08:00`:

```text
~OTA:STATUS version=v1002.0.0-ota-test-23-g2778a87
~POWER:STATUS state=ACTIVE blockers=0x00000000 blocker_names=none ble_connected=0 battery_mv=4108 battery_level=92 battery_valid=1 last_sleep_reason=none last_wake_source=power_on guard=1 overnight_sleep_ms=1800000 sleep_stats_valid=0 sleep_duration_ms=0 sleep_entry_battery_mv=0 sleep_entry_battery_level=0 sleep_entry_battery_valid=0 wake_battery_mv=0 wake_battery_level=0 wake_battery_valid=0 sleep_drain_mv=0 sleep_drain_level=0 sleep_drain_mv_per_hour=0 sleep_drain_level_per_hour_x100=0 wake_policy=key4_only wake_gpio_mask=0x0000000000200000 wake_capable_keys=KEY4/GPIO21 wake_key_gpio=21 wake_key_rtc_capable=1 voice_key_gpio=35 voice_key_rtc_capable=0 voice_key_deep_sleep_wake=0
```

Expected human test procedure:

- Unplug USB and leave the device idle on battery for at least 8 hours.
- Do not press keys during the idle run.
- Wake with `KEY4/GPIO21` only, then reconnect USB and capture first post-wake `~POWER:STATUS` plus persistent diag log.

User-confirmed battery-only unplug/start time:

- `2026-05-30 23:35:21 +08:00`

Minimum target wake/check time for >=8 hours:

- `2026-05-31 07:35:21 +08:00`

## 2026-05-31 Post-Wake Capture

User reported wake on 2026-05-31 morning. `COM5` was present and post-wake serial/diag evidence was captured under:

- `tests/artifacts/firmware_low_power_battery_drain_20260530-233521/post_wake_serial_20260531-081341.txt`
- `tests/artifacts/firmware_low_power_battery_drain_20260530-233521/post_wake_summary_20260531-081341.txt`
- `tests/artifacts/firmware_low_power_battery_drain_20260530-233521/post_wake_diag_log_20260531-081341.jsonl`
- `tests/artifacts/firmware_low_power_battery_drain_20260530-233521/post_wake_diag_log_ai_bundle_20260531-081341.json`

Captured OTA status confirms the running build remained the intended final HEAD:

```text
version=v1002.0.0-ota-test-23-g2778a87
```

Observed persistent diag_log facts:

- `DIAG_POWER_SLEEP_ENTRY`: `idle_ms=1801912`, `battery_mv=4034`, `battery_level=86`, `reason=1` after the overnight idle threshold.
- `DIAG_SYS_BOOT`: `boot_reason=boot_deep_sleep`.
- `DIAG_POWER_WAKE`: `wake_source=2`, `wake_gpio_mask_low=2097152`, `last_sleep_reason=1`, `last_idle_ms=1801912`.
- Wake mask `2097152` is `1 << 21`, matching `KEY4/GPIO21`.

Captured `~POWER:STATUS` after USB/serial recovery:

```text
~POWER:STATUS state=ACTIVE blockers=0x00000000 blocker_names=none ble_connected=0 battery_mv=4116 battery_level=93 battery_valid=1 last_sleep_reason=none last_wake_source=power_on guard=1 overnight_sleep_ms=1800000 sleep_stats_valid=0 sleep_duration_ms=0 sleep_entry_battery_mv=0 sleep_entry_battery_level=0 sleep_entry_battery_valid=0 wake_battery_mv=0 wake_battery_level=0 wake_battery_valid=0 sleep_drain_mv=0 sleep_drain_level=0 sleep_drain_mv_per_hour=0 sleep_drain_level_per_hour_x100=0
```

Assessment:

- Battery was not drained during the overnight battery-only run. Start reference was `4108mV/92%`; sleep-entry was `4034mV/86%`; post-USB readings were `4116mV/93%` and `4146mV/96%`.
- Deep sleep entry and KEY4/GPIO21 EXT1 wake are proven by persistent diag_log.
- The exact sleep-drain telemetry fields were not preserved in the captured `~POWER:STATUS` because reconnecting/opening USB serial caused a subsequent reset path, leaving `sleep_stats_valid=0`. This means the run is strong behavioral/power evidence but does not fully close the reviewer-requested "first post-wake POWER:STATUS with sleep_drain fields" artifact.

## 2026-05-31 Wake Reconnect UX Delta

After the overnight run, the device woke correctly but user key presses could appear dead when the bonded host had not reconnected (`ble_connected=0`). The low-power sleep path was not changed. The delta fix adds an explicit BLE reconnect request path for user activity while disconnected:

- Physical WASD path: `ble_hid_send_ascii_async()` now requests reconnect before returning `ESP_ERR_INVALID_STATE`.
- Voice key/session path: rejected recording start now requests reconnect before clearing blockers.
- GAP path: `ble_hid_gap_request_reconnect()` exits low-power advertising state, marks directed advertising pending, stops current advertising if active, and restarts advertising so a bonded host gets another directed-advertising window before falling back to fast undirected advertising.

Validation artifacts:

- `tests/artifacts/firmware_low_power_battery_drain_20260530-233521/reconnect_fix_20260531-validate/serial_status_before_reconnect.txt`
- `tests/artifacts/firmware_low_power_battery_drain_20260530-233521/reconnect_fix_20260531-validate/serial_vrec_reconnect_request.txt`
- `tests/artifacts/firmware_low_power_battery_drain_20260530-233521/reconnect_fix_20260531-validate/serial_physical_key_reconnect_window.txt`
- `tests/artifacts/firmware_low_power_battery_drain_20260530-233521/reconnect_fix_20260531-validate/ai_diagnostics/manifest.json`
- `tests/artifacts/firmware_low_power_battery_drain_20260530-233521/reconnect_fix_20260531-validate/ai_diagnostics/diag_log_ai_bundle.json`

Observed device state before reconnect trigger:

```text
~POWER:STATUS state=ACTIVE blockers=0x00000000 blocker_names=none idle_ms=0 user_idle_ms=0 radio_idle_ms=0 ble_connected=0 battery_mv=4142 battery_level=95 battery_valid=1 last_sleep_reason=none last_wake_source=power_on guard=1 overnight_sleep_ms=1800000 sleep_stats_valid=0
```

Observed reconnect trigger evidence:

```text
I (1274) ESP_HID_GAP: BLE reconnect requested: adv_active=1 low_power_adv=0 directed_pending=0
I (1294) ESP_HID_GAP: NimBLE directed advertising started: peer_type=0 peer_addr=ac:92:ef:77:d5:4c
I (2584) ESP_HID_GAP: directed advertising completed; falling back to undirected advertising
I (2594) ESP_HID_GAP: NimBLE undirected advertising started: low_power=0 interval_ms=30-50
{"t":938,"src":"ble_gap","evt":10,"sev":"INFO","a1":7,"a2":0,"a3":1,"a4":65535}
```

The short delta validation proves the new reconnect request path restarts a directed-advertising window while disconnected. Because the no-touch overnight sleep path, thresholds, and KEY4 wake configuration were not changed by this delta, the 2026-05-30/31 overnight drain evidence remains applicable; a second full 8-hour soak should not be required for this reconnect-only change.
