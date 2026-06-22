# Idle Status LED Latch Fix

Date: 2026-06-22
Agent: oai2

## Change

- Battery low-power idle keeps `LED1=PWR` visible using `battery_display_valid || battery_valid`, with a low amber fallback when live battery sampling is temporarily unavailable.
- Low-power idle treats BLE `TYPE_READY` the same as `CONNECTED`, so `LED2=BLE` remains latched at the restrained idle blue level.
- `status_led_prepare_sleep()` and explicit output-off paths remain the all-off paths.

## Validation

- PASS: `python .\tools\verify_status_led_static.py`
- PASS: `pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1`
- PASS: `pwsh -NoProfile -File .\tools\verify_charging_awake_policy_static.ps1`
- PASS: `git diff --check`
- PASS: `pwsh -NoProfile -File .\tools\build.ps1`

Build result: `voice-keyboard-firmware.bin` size `0xd2000`; smallest app partition free `0x52e000`.
