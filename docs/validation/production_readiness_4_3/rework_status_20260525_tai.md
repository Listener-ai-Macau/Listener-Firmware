# Production readiness 4.3 rework status

status=BLOCKED
owner=Tai
date=2026-05-25

## Summary

The firmware branch now contains the 4.3 fixes and stronger physical WASD diagnostics, but the step is not ready for review. The remaining acceptance item is physical host-visible WASD from KEY1/KEY2/KEY3/KEY4.

## Validation run

- `git diff --check`: PASS
- `python -m compileall -q tools`: PASS
- `pwsh -NoProfile -File tools\esp_idf_ci.ps1 build`: PASS, bin `0x94200`, app partition 60% free
- `pwsh -NoProfile -File tools\flash.ps1 -Port COM5`: PASS, ESP32-S3 MAC `14:c1:9f:48:fe:70`
- `powershell -ExecutionPolicy Bypass -File tools\ensure_ble_hid_connection.ps1 -DeviceName listener -BluetoothAddress 14C19F48FE72 -DurationSeconds 20 -PollIntervalSeconds 2 -ExitOnReady`: PASS, Connected, GATT Success, services 7, sessions 7
- `pwsh -NoProfile -File tools\verify_physical_wasd_hid.ps1 -Port COM5 -ExpectedText dwas -TimeoutSeconds 120 -OutputPath docs\validation\production_readiness_4_3\physical_wasd_hid_20260525_tai_rawedge.md`: FAIL

## Physical WASD result

The raw-edge build logs GPIO transitions before the HID send path. During the 120 second run, the firmware stayed alive and BLE remained connected, but the log did not contain any raw transition for the expected SPH0645-board WASD pins:

- missing `WASD key raw transition: source=key1.gpio45.d`
- missing `WASD key raw transition: source=key2.gpio48.w`
- missing `WASD key raw transition: source=key3.gpio47.a`
- missing `WASD key raw transition: source=key4.gpio21.s`

Because no raw GPIO edge was observed, this run does not exercise the BLE HID send path and cannot satisfy the review requirement for host-visible `d/w/a/s`.

## Durable artifacts

- `docs/validation/production_readiness_4_3/boot_reflash_20260525_tai_stackfix.log`
- `docs/validation/production_readiness_4_3/boot_reflash_20260525_tai_rawedge.log`
- `docs/validation/production_readiness_4_3/physical_wasd_hid_20260525_tai_rawedge.md`
- Earlier failed WASD attempts: `physical_wasd_hid_20260525_tai.md`, `physical_wasd_hid_20260525_tai_retry.md`

## Unblock condition

Run `tools\verify_physical_wasd_hid.ps1` while a human or jig physically presses KEY1, KEY2, KEY3, and KEY4 on the current SPH0645 board. To unblock 4.3, the artifact must show both:

- raw transitions and queued lines for `key1.gpio45.d`, `key2.gpio48.w`, `key3.gpio47.a`, `key4.gpio21.s`
- host-visible captured suffix `dwas`

If GPIO45 or any other key is not physically connected on this prototype, document that hardware fact explicitly and prove all connected keys with raw transition plus host-visible output.
