# Post-Flash Boot PWR_HOLD Stability Evidence

Plan: `voice-keyboard-firmware-full-function-test` step `1.7`

Firmware commit flashed: `178a804 Drive PWR_HOLD low at boot entry`

## Flash

Command:

```powershell
pwsh -NoProfile -File C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COM7 -Run pwsh -NoProfile -File .\tools\flash.ps1 -Port COM7 -NoBuild
```

Result:

- PASS: esptool connected to ESP32-S3 on `COM7`.
- PASS: bootloader, app, partition table, and OTA data writes completed.
- PASS: every flashed region reported `Hash of data verified`.
- PASS: device hard-reset after flash and re-enumerated as `COM7`.

## Boot-Hold Capture

Command:

```powershell
pwsh -NoProfile -File C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COM7 -Run pwsh -NoProfile -File .\tools\verify_charging_awake_policy_hardware.ps1 -Port COM7 -OutputDir docs\validation\voice-keyboard-firmware-full-function-test-1.7\post-flash-boot-hold-oai1-20260615-0828 -ObserveSeconds 10
```

Result:

- PASS: `~BOARD:STATUS` reports `pwr_hold_gpio=11 pwr_hold_level=low pwr_hold_configured=1`.
- PASS: `~POWER:STATUS` reports `state=ACTIVE`, `external_power_present=1`, `usb_power_present=1`, `pwr_hold_level=low`, and `last_shutdown_reason=none`.
- PASS: boot log reached `normal boot survived 30000ms`.
- PASS: `COM7` was still enumerated after the post-flash capture and telemetry run.

## Current Telemetry

Command:

```powershell
pwsh -NoProfile -File C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COM7 -Run pwsh -NoProfile -File .\tools\collect_v2_current_telemetry.ps1 -Port COM7 -ReadSeconds 15 -OutputDir docs\validation\voice-keyboard-firmware-full-function-test-1.7\current-telemetry
```

Result:

- PASS: `docs/validation/voice-keyboard-firmware-full-function-test-1.7/current-telemetry/v2_current_telemetry_20260615-083454.md`
- PASS: `~POWER:STATUS` reports `battery_mv=3794`, `battery_level=66`, `last_shutdown_reason=none`, `pwr_hold_gpio=11`, and `pwr_hold_level=low`.

## Interpretation

The prior boot-off symptom matches a firmware timing bug: `PWR_HOLD/GPIO11` was previously configured after several boot subsystems, so a short human power-key hold could release the latch before firmware took over. Commit `178a804` drives `PWR_HOLD/GPIO11` LOW as the first `app_main()` action. The flashed firmware now stays enumerated and reports runtime PWR_HOLD LOW after boot.
