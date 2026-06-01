# voice-keyboard-custom-keys 1.1 validation

Assignee: tai2

Scope:

- Firmware logical custom keys now expose `KEY1`, `KEY2`, and `KEY3` events from current N4 GPIO48/GPIO47/GPIO21.
- Safe fallback HID uses F13/F14/F15 instead of typing WASD text.
- `VOICE` remains the recording/recovery key through the existing voice recording control path. On current N4 hardware, this is GPIO45 via the legacy KEY1 board pin define.
- Recording stability logic was not redesigned in this step; changes in the recording path are limited to source labeling and accepting `voice.*` as the same diagnostic source class as the legacy `key1.*` label.

Validation:

- PASS: `pwsh -NoProfile -File .\tools\verify_v2_board_profile_static.ps1`
- PASS: `pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1`
- PASS: `python .\tools\verify_ble_ota_gatt_contract.py`
- PASS: `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
- PASS: `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
- PASS: `python -m compileall -q tools`
- PASS: `pwsh -NoProfile -File .\tools\verify_watchdog_boot_safety_static.ps1`
- PASS: `git diff --check`

Manual/source review:

- No real hardware flash, serial monitor, BLE capture, or key press validation was run in this implementation step.
- The physical fallback verification entry point is `tools/verify_physical_custom_key_hid.ps1`; the old WASD script name remains only as a compatibility wrapper.
- Current N4 deep-sleep wake remains GPIO21, now reported as logical `KEY3/GPIO21`; `VOICE/GPIO45` is still documented as not RTC deep-sleep wake capable.

Hardware-dependent follow-up:

- `voice-keyboard-custom-keys/1.3` owns real-device validation for VOICE press/hold behavior and KEY1-KEY3 fallback/action dispatch with hardware locks.
