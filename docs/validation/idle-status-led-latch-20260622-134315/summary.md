# Idle Status LED Latch Fix

Date: 2026-06-22
Agent: oai2

## Change

- Battery low-power idle keeps `LED1=PWR` visible using `battery_display_valid || battery_valid`, with a low amber fallback when live battery sampling is temporarily unavailable.
- Low-power idle treats BLE `TYPE_READY` the same as `CONNECTED`, so `LED2=BLE` remains latched at the restrained idle blue level.
- Connected HID battery/status updates now resynchronize `LED2=BLE` to `connected` or `TYPE_READY` when manual preview/clear commands have cleared the LED semantic state while BLE is still connected.
- `status_led_prepare_sleep()` and explicit output-off paths remain the all-off paths.

## Validation

- PASS: `python .\tools\verify_status_led_static.py`
- PASS: `pwsh -NoProfile -File .\tools\verify_ble_status_led_connected_sync.ps1`
- PASS: `pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1`
- PASS: `pwsh -NoProfile -File .\tools\verify_charging_awake_policy_static.ps1`
- PASS: `git diff --check`
- PASS: `pwsh -NoProfile -File .\tools\build.ps1`
- PASS: COM10 post-flash serial status captured idle PWR/BLE output after `7ec06b7`; `PWR` remained visible and `TYPE_READY`/connected BLE used the low-blue latch.
- DIAG: COM10 post-replug serial status plus bounded diagnostic export captured BLE source-of-truth drift: `~POWER:STATUS` reported `ble_connected=1`, while `~LED:STATUS` reported `ble=disconnected` after preview/clear activity. The follow-up connected-HID resync change covers this regression path.

Build result: `voice-keyboard-firmware.bin` size `0xd2040`; smallest app partition free `0x52dfc0`.

## Evidence

- `post_flash_serial_status_after_peer.txt`
- `post_replug_serial_status.txt`
- `diag/manifest.json`
- `diag/diag_log_ai_bundle.json`
