# PWR_HOLD Shutdown Rerun - oai1 - 2026-06-15

## Result

Rerun confirms the same hardware-gated failure after commit `f32ddb2`.

## Validation Commands

- `pwsh -NoProfile -File .\tools\verify_battery_monitor_static.ps1` - PASS
- `pwsh -NoProfile -File .\tools\verify_ble_battery_service_static.ps1` - PASS
- `pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1` - PASS
- `pwsh -NoProfile -File .\tools\verify_charging_awake_policy_static.ps1` - PASS
- `pwsh -NoProfile -File .\tools\collect_v2_current_telemetry.ps1 -Port COM7 -ReadSeconds 15 -OutputDir docs/validation/voice-keyboard-firmware-full-function-test-1.7/current-telemetry` - PASS
- `git diff --check` - PASS

## Hardware Evidence

- Current unique ESP32 serial device: `COM7`.
- Current telemetry report: `docs/validation/voice-keyboard-firmware-full-function-test-1.7/current-telemetry/v2_current_telemetry_20260615-075231.md`.
- Telemetry reported `battery_mv=3824`, `battery_level=69`, `external_power_present=1`, `usb_power_present=1`, `shutdown_blockers=0x00000080`, and `pwr_hold_gpio=11 pwr_hold_level=low`.
- Manual shutdown probe: `docs/validation/voice-keyboard-firmware-full-function-test-1.7/shutdown-rerun-oai1-20260615-0753/shutdown_serial_transcript.txt`.
- `~POWER:SHUTDOWN` entered manual hardware shutdown, but `PWR_HOLD/GPIO11` did not settle high within 500 ms.
- Firmware restored runtime-low PWR_HOLD and requested BLE reconnect instead of staying silent.
- COM7 remained present while USB/external power was connected, so this rerun still cannot serve as battery-only observable power-off proof.

## Blocker

The remaining blocker is hardware confirmation: prove whether this physical board routes `PWR_HOLD` to GPIO11 or an older GPIO46 mapping, and run the destructive power-off check under battery-only conditions where COM disappearance/cold boot can be meaningfully observed.
