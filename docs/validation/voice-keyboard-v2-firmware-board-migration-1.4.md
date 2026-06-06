# voice-keyboard-v2-firmware-board-migration/1.4 Partial Validation

Agent: oai1
Date: 2026-06-06
Hardware: COM6, BLE A4CB8FF459A6, ESP32-S3-WROOM-1-N16R8

## Result

PARTIAL PASS only. The N16R8 firmware boots, exposes the expected V2 board profile, connects over BLE, dispatches HID, exposes OTA GATT, and initializes all four WS2812 LED zones after the RMT channel fix in this worktree.

Do not mark step 1.4 accepted from this evidence alone. Physical KEY1-KEY4/EC11 transitions, LED visual color/order, VDD_LED/SY7088 rail sign-off, sleep/wake behavior, destructive watchdog recovery, and no-tool recovery remain unvalidated or require an explicit human waiver.

## Real Defect Fixed

The pre-fix build initialized only the status and EC11 WS2812 strips. KEY and edge strips failed with:

- `rmt_tx_register_to_group: no free tx channels`
- `status_led: strip key RMT channel init failed gpio=13: ESP_ERR_NOT_FOUND`
- `status_led: strip edge RMT channel init failed gpio=4: ESP_ERR_NOT_FOUND`

Root cause: each strip requested `.mem_block_symbols = 64`, which consumes two ESP32-S3 RMT memory blocks per TX channel and prevents four independent strip TX channels from being allocated.

Fix:

- `components/status_led/status_led.c` now uses `SOC_RMT_MEM_WORDS_PER_CHANNEL` for strip TX channel allocation.
- LED frame transmission is serialized with a dedicated TX mutex to keep four strip updates ordered.
- `tools/verify_status_led_static.py` now rejects the stale `mem_block_symbols = 64` pattern and requires the SoC-cap based allocation.

Rollback: revert the LED RMT allocation/mutex change if it causes regressions. Expected rollback risk is low, but rollback would reintroduce the known four-zone V2 LED initialization failure.

## Evidence

Artifacts are under:

`tests/artifacts/voice-keyboard-v2-firmware-board-migration-1.4-oai1/20260606-continue/`

### Firmware Checks

Passed after the LED RMT fix:

- `python .\tools\verify_status_led_static.py`
- `pwsh -NoProfile -File .\tools\verify_v2_board_profile_static.ps1`
- `pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1`
- `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
- `git diff --check`

### Flash and Boot Identity

`flash_status_led_rmt_fix_with_lock.txt` captured a successful COM6 flash with image hash verification.

`serial_boot_board_power_led_ota_wdt_after_led_fix_with_lock.txt` captured boot diagnostics:

- ESP32-S3 with 16MB flash and 8MB Octal PSRAM.
- Board profile `voice-keyboard-v2-n16r8`.
- V2 pin map: KEY1..KEY4 GPIO38/GPIO39/GPIO40/GPIO41, EC11 GPIO42/GPIO2/GPIO18, mic CLK/DOUT GPIO48/GPIO47, USB detect GPIO7, charger GPIO14/GPIO21, BAT_V_ADC GPIO8, current ADC GPIO10/GPIO9, RGB GPIO1/GPIO5/GPIO13/GPIO4, PWR_HOLD GPIO11.
- Traceability caveat: the boot app version string remained `v1002.0.0-ota-test-120-g3eb0e13`; it did not encode the local LED RMT fix commit.

### LED Bring-up

Post-fix serial evidence shows all four WS2812 strips initialize:

- `status_led: strip status ready: gpio=1 leds=6`
- `status_led: strip ec11 ready: gpio=5 leds=12`
- `status_led: strip key ready: gpio=13 leds=4`
- `status_led: strip edge ready: gpio=4 leds=6`

`~LED:STATUS` reported the four-zone policy and GRB strip order. `~LED:TEST:RGBW all` and `~LED:TEST:MAP status/ec11/key/edge` were accepted and logged masks `0x0f`, `0x01`, `0x02`, `0x04`, and `0x08`.

Remaining LED gap: no human visual confirmation of physical LED color/order/brightness was captured, and VDD_LED/SY7088 current sign-off is still open.

### BLE, HID, OTA, Diagnostics

Passed:

- `ensure_ble_hid_connection_windows_powershell_with_lock.txt`: connected to BLE device and opened active GATT sessions.
- `verify_ble_hid_with_lock.txt`: HID dispatch completed for text `v2hid14oai1`.
- `verify_ble_ota_gatt_discovery_windows_powershell_with_lock.txt`: OTA service found; control characteristic supports read/write and data characteristic supports write/write-without-response.
- `dump_diag_log_with_lock.txt`: dumped 722 diagnostic events.

PowerShell 7 caveat: `ensure_ble_hid_connection_with_lock.txt` failed due to WinRT type loading in PowerShell 7, while the same check passed under Windows PowerShell.

### Power and Current

`~BOARD:STATUS` and `~POWER:STATUS` reported USB present, charging active, valid battery telemetry around 4.0V, external power sleep blocker, and V2 EC11/GPIO18 wake candidate with deep-sleep wake disabled pending power sign-off.

`v2_current_telemetry_20260606-195157.md` reported both expected current telemetry branches present and calibrated:

- `TPS63020_input_branch` on GPIO10
- `SY7088_input_branch` on GPIO9

Remaining current gap: captured raw ADC/current values were zero for both branches during this sample, so this evidence does not prove VDD_LED/SY7088 rail load under visible LED activity.

## Open Acceptance Gaps

- Physical KEY1-KEY4 actuation evidence was not captured.
- Physical EC11 rotation/press evidence was not captured.
- LED visual color/order and VDD_LED/SY7088 rail behavior still need human evidence or waiver.
- Sleep entry/wake return was not run because external power was present and EC11 deep-sleep wake remains disabled until sign-off.
- Destructive watchdog deadlock/recovery and no-tool recovery were not run.
- Boot version traceability should be improved or waived if acceptance requires commit-level identity from serial logs.
