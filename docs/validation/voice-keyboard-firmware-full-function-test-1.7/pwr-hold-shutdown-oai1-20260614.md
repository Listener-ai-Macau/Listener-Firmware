# PWR_HOLD Shutdown Validation - oai1 - 2026-06-14

## Result

Static firmware checks, build, flash, and current telemetry passed, but the destructive hardware-shutdown acceptance remains failed on the current COM7/USB-powered setup.

## Evidence

- Firmware build passed after adding a bounded `PWR_HOLD/GPIO11` release-high settle window.
- Flash to COM7 passed with esptool hash verification.
- `~POWER:STATUS` after flash reported `external_power_present=1`, `usb_power_present=1`, `battery_mv=3896`, `battery_level=75`, `pwr_hold_gpio=11`, and `pwr_hold_level=low`.
- `~POWER:SHUTDOWN` entered `manual_command` shutdown and released `PWR_HOLD/GPIO11`, but GPIO11 did not settle high within 500 ms.
- Firmware restored runtime-low PWR_HOLD and requested BLE reconnect instead of staying in a silent shutdown wait.
- COM7 remained present while USB power was connected, so COM disappearance cannot be used as battery-only power-off proof in this run.

## Artifacts

- `docs/validation/voice-keyboard-firmware-full-function-test-1.7/shutdown-release-high-settle-oai1-20260614-2310/shutdown_serial_transcript.txt`
- `docs/validation/voice-keyboard-firmware-full-function-test-1.7/current-telemetry/v2_current_telemetry_20260614-231329.md`
- `docs/validation/voice-keyboard-firmware-full-function-test-1.7/gpio-scan-oai1-20260614-2258.txt`

## Hardware Context

Current `voice-keyboard-hardware/Voice Keyboard V2.0/MCU.SchDoc` coordinates place `PWR_HOLD` beside `IO11` and `EC11-KEY_IO` beside `IO18`. Older review exports still document the previous mapping `PWR_HOLD=IO46` and `EC11-KEY=IO11`, and the current board behavior is consistent with either board-version drift or GPIO11 being externally held low.

Battery voltage is currently healthy enough for this validation. The remaining blocker is proving battery-only latch power-off with the actual hardware pin/net.
