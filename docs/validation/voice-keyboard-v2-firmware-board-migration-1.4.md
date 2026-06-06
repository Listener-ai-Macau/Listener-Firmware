# Voice Keyboard V2 Firmware Board Migration 1.4 Validation

Owner: oai2
Date: 2026-06-06
Hardware resources used: COM6, BLE-A4CB8FF459A6
Artifact root: `tests/artifacts/voice-keyboard-v2-firmware-board-migration-1.4-oai2/20260606-182357`

Status: blocked, do not submit from this evidence alone.

## Summary

The automated and non-destructive hardware checks found one real firmware defect and fixed it in this branch:

- `components/status_led/status_led.c` used `mem_block_symbols = 64`, which consumes two ESP32-S3 RMT memory blocks per strip. On V2 this allowed only the status and EC11 strips to initialize; KEY GPIO13 and edge GPIO4 failed with `no free tx channels`.
- The fix uses `SOC_RMT_MEM_WORDS_PER_CHANNEL` and serializes LED frame transmission with a TX mutex.
- `tools/verify_status_led_static.py` now rejects the 64-symbol regression.
- `tools/flash.ps1` now honors `LISTENER_IDF_BUILD_DIR`, which lets the plan's flash validation run from this long worktree path.

Do not mark 1.4 complete yet. Remaining acceptance needs external physical evidence: human visual confirmation of RGBW color/order and LED physical order, physical EC11 and KEY1-KEY4 operation, meter/fixture measurement for 3.3V and VDD_LED, and an OTA/sleep/wake/watchdog/recovery closure decision.

## Validation Commands

Plan placeholders were resolved as:

- `COMx` -> `COM6`
- `BLE-ADDR` -> `BLE-A4CB8FF459A6`

Results:

| Check | Result | Evidence |
|---|---|---|
| Flash with workflow lock | PASS | `flash_after_led_rmt_fix_with_lock.txt` |
| Serial monitor/status capture | PASS with known degraded audio warnings | `boot_after_led_rmt_fix_esptool_reset_with_lock.txt`, `board_led_power_ota_wdt_boot_status_after_fix_with_lock.txt` |
| Diag log dump | PASS, 783 retained events dumped | `dump_diag_log_after_fix_with_lock.txt`, `diag_log_20260606-183521.jsonl` |
| BLE HID script dispatch | PASS | `verify_ble_hid_after_fix_no_reset_encoded_with_lock.txt` |
| V2 current telemetry | PASS | `v2_current_telemetry_20260606-183933.md`, `v2_current_telemetry_20260606-183933.json` |
| OTA GATT discovery after fix | BLOCKED/FAIL on Windows WinRT uncached discovery | `verify_ble_ota_gatt_discovery_after_fix_with_lock.txt`, `verify_ble_ota_gatt_discovery_after_fix_skip_prime_with_lock.txt`, `probe_ble_ota_gatt_after_fix_with_lock.txt` |

Static/build checks run after the fix:

- PASS: `python tools/verify_status_led_static.py`
- PASS: `pwsh -NoProfile -File tools/verify_v2_board_profile_static.ps1`
- PASS: `pwsh -NoProfile -File tools/verify_power_manager_static.ps1`
- PASS: PowerShell parser check for `tools/flash.ps1`
- PASS: `LISTENER_IDF_BUILD_DIR=%TEMP%\listener-idf-build-v2-1-4-oai2; pwsh -NoProfile -File tools/build.ps1 -Target esp32s3`

## Observed PASS Evidence

Flash evidence:

- ESP32-S3 QFN56 rev v0.2, USB-Serial/JTAG.
- Embedded PSRAM 8MB.
- Flash command used `--flash_size 16MB`.
- App binary size after fix: `0xbecc0`; smallest app partition `0x600000`; 88% free.
- Bootloader, app, partition table, and OTA data hash verification passed.

Boot and board identity:

- Bootloader reports SPI flash size 16MB.
- Runtime reports 8MB octal PSRAM and POST SPIRAM `8388608 bytes`.
- `~BOARD:STATUS` reports `profile=voice-keyboard-v2-n16r8`, module `ESP32-S3-WROOM-1-N16R8`, `flash_mb=16`, `psram_mb=8`, `psram_mode=octal`.
- Pin map in boot/status logs matches V2: keys GPIO38/39/40/41, EC11 GPIO42/GPIO2/GPIO18, mic GPIO48/GPIO47, USB detect GPIO7, charger GPIO14/GPIO21, BAT_V_ADC GPIO8, current ADC GPIO10/GPIO9, LED GPIO1/GPIO5/GPIO13/GPIO4, PWR_HOLD GPIO11, reserved MSPI GPIO35/GPIO36/GPIO37.

LED after the RMT fix:

- Boot log confirms all four strips initialize:
  - status: GPIO1, 6 LEDs
  - EC11 ring: GPIO5, 12 LEDs
  - key: GPIO13, 4 LEDs
  - edge: GPIO4, 6 LEDs
- `~LED:STATUS` reports the four strip contracts and GRB order.
- `~LED:TEST:RGBW all` is accepted with mask `0x0f`.
- `~LED:TEST:MAP ec11` is accepted with mask `0x02` and EC11 order `LED7..LED10+LED15..LED16+LED23..LED28`.
- `~LED:OFF` after fix did not emit the previous RMT timeout in the captured run.

Power and telemetry:

- `~POWER:STATUS` reports USB external power present, charging active-low level, battery ADC, provisional EC11 wake policy, and deep-sleep wake disabled until sign-off.
- V2 current telemetry after fix reports both expected sensors present:
  - `TPS63020_input_branch`, GPIO10, ADC calibrated, input current/power valid.
  - `SY7088_input_branch`, GPIO9, ADC calibrated, input current/power valid.

BLE/HID/OTA/watchdog status:

- BLE HID after-fix dispatch consumed `v2hid14fix` over serial and logged `hid_keyboard: send_ascii done` for every character with `connected=yes`.
- `~OTA:STATUS` reports running `ota_0`, update `ota_1`, update slot size 6291456, `pending_verify=0`, `blocker=none`, and readiness/capability strings including OTA ready.
- `~WDT:STATUS` reports task watchdog enabled, initialized, panic enabled, timeout 5s.
- `~BOOT:STATUS` reports reset reason USB, crash count below threshold, safe mode false.

## Remaining Blockers

These items need a human hardware operator or external test setup before 1.4 can be submitted:

- Visual LED validation: RGBW physical color order, brightness, EC11 ring order/direction, edge order, and all four visible zones.
- Physical controls: press EC11 and KEY1-KEY4 and capture firmware/HID logs for the actual switches.
- Meter/fixture measurements: 3.3V rail, VDD_LED rail, charger behavior, and battery-to-3v3 / battery-to-LED branch measurements beyond ADC telemetry.
- Sleep/wake: actual sleep entry/wake and recovery evidence. Firmware currently reports V2 EC11 deep-sleep wake as disabled until power-latch/leakage/pull/false-wake sign-off.
- OTA live GATT after-fix: Windows PnP sees the listener and OTA service, and firmware reports OTA ready, but WinRT uncached discovery/read failed with `0x80070016` after the final flash. Cached service metadata still shows the OTA service and characteristics. This needs a clean BLE reconnect/re-pair or Windows Bluetooth reset before claiming live OTA GATT PASS.
- Watchdog destructive smoke: `~WDT:DEADLOCK` was not run because it intentionally triggers a reset and should be planned with an operator.

## Notes

Known warnings in boot/status evidence are expected for this V2 validation state unless a later plan changes the policy:

- Audio capture is degraded because the V2 microphone interface remains unvalidated.
- Board policy strings mark PWR_HOLD, LED VDD sign-off, current telemetry, and mic validation as provisional where applicable.

This report intentionally records partial PASS plus blockers. No `aiw submit` should be run until the remaining blockers are closed or explicitly accepted by the reviewer/product owner.
