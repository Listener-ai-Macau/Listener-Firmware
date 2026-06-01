# voice-keyboard-custom-keys 1.1 validation

Assignee: tai2

Scope:

- Firmware logical custom keys now expose `KEY1`, `KEY2`, `KEY3`, and `KEY4` events from current N4 GPIO45/GPIO48/GPIO47/GPIO21.
- Safe fallback HID uses F13/F14/F15/F16 instead of typing WASD text.
- EC11 push is now the recording/recovery input through the existing voice recording control path: single click toggles recording, double-click clears pairing/session state, and long press is reserved for hardware power control.

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
- Current N4 deep-sleep wake remains GPIO21, reported as `KEY4/GPIO21`; `EC11-KEY/GPIO35` is documented as not RTC deep-sleep wake capable on N4.

Hardware-dependent follow-up:

- `voice-keyboard-custom-keys/1.3` owns real-device validation for EC11 single/double-click behavior and KEY1-KEY4 fallback/action dispatch with hardware locks.
