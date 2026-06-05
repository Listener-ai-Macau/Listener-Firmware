# voice-keyboard-v2-firmware-board-migration/1.3 Validation

Agent: oai3

## Scope

PASS: Step 1.3 keeps the accepted V2/N16R8 board baseline and closes the remaining diagnostics/static-check gaps for V2 peripherals, sleep/wake, LED, current telemetry, OTA, and repo feature context.

## Implementation Notes

- Physical key and EC11 diagnostics remain on the accepted V2 pins: KEY1..KEY4 GPIO38/GPIO39/GPIO40/GPIO41, EC11 A/B/key GPIO42/GPIO2/GPIO18.
- Power manager diagnostics report the V2 EC11/GPIO18 provisional wake policy, disabled deep-sleep wake by default, wake GPIO mask, wake user action, USB/charger state, sleep blockers, and drain fields.
- Board and battery diagnostics expose BAT_V_ADC/GPIO8 68K/68K reconstruction, USB_Det/GPIO7 R37/R32 10K/10K policy, BAT_CHG/GPIO14, BAT_STD/GPIO21, PWR_HOLD/GPIO11 policy, and both battery-side input-current branches.
- `tools/collect_v2_current_telemetry.ps1` now parses the firmware's `~BOARD:POWER branch=` output for `TPS63020_input_branch` and `SY7088_input_branch`, records `battery_side_mv`, and uses a V2/GPIO18 wake self-test sample.
- `tools/verify_v2_board_profile_static.ps1` now fails if the current telemetry collector regresses to stale rail names or EC11/GPIO11 wake diagnostics.
- `tools/ai/repo_features.ps1 -Check` now validates the active N16R8/8MB Octal PSRAM context instead of the old N4/no-PSRAM context.

## Required Validation Commands

PASS evidence is captured in `docs/validation/voice-keyboard-v2-firmware-board-migration-1.3-validation-evidence.json`.

Commands covered:

- `pwsh -NoProfile -File .\tools\verify_v2_board_profile_static.ps1`
- `pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1`
- `python .\tools\verify_status_led_static.py`
- `python .\tools\verify_ble_ota_gatt_contract.py`
- `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
- `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
- `git diff --check`

Additional focused check:

- `pwsh -NoProfile -File .\tools\collect_v2_current_telemetry.ps1 -SelfTest`

## Hardware Notes

No flash, serial monitor, BLE, or physical V2 board validation was performed in this step. Real hardware bring-up remains deferred to step 1.4, which requires a workflow hardware lock.
