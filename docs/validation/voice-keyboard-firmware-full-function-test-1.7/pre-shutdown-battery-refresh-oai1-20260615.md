# Pre-Shutdown Battery Refresh Evidence

Plan: `voice-keyboard-firmware-full-function-test` step `1.7`

Purpose: verify the firmware side of the BLE battery feedback path before hardware shutdown. The device cannot send any hardware feedback after PWR_HOLD removes power, so firmware now forces one final HID Battery Service update before preparing BLE disconnect.

## Firmware Change

- `ports/esp32/ble_hid/ble_hid.c` exposes `ble_hid_battery_force_refresh(reason)`, which samples `battery_monitor_read()` and calls `esp_hidd_dev_battery_set()` with `force_notify=true` while BLE is connected.
- `components/power_manager/power_manager.c` calls `ble_hid_battery_force_refresh("pre_shutdown")` before `ble_hid_gap_prepare_shutdown_disconnect()`, then waits `100 ms` to let the notification leave before shutdown preparation continues.

## Validation

- PASS: `pwsh -NoProfile -File .\tools\verify_battery_monitor_static.ps1`
- PASS: `pwsh -NoProfile -File .\tools\verify_ble_battery_service_static.ps1`
- PASS: `pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1`
- PASS: `pwsh -NoProfile -File .\tools\verify_charging_awake_policy_static.ps1`
- PASS: `git diff --check`
- PASS: `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`

## Hardware Status

Windows did not enumerate an ESP32 serial port during this check (`Get-CimInstance Win32_SerialPort` returned no rows), so flashing and real BLE Battery Service notification capture were not run in this pass.
