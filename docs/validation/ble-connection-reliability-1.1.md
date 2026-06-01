# ble-connection-reliability 1.1 validation

Scope: firmware BLE HID Battery Service live reporting from `battery_monitor` ADC status.

## Results

- PASS: `pwsh -NoProfile -File .\tools\verify_battery_monitor_static.ps1`
- PASS: `pwsh -NoProfile -File .\tools\verify_ble_hid.ps1`
- PASS: `pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1`
- PASS: `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
- PASS: `python -m compileall -q tools`
- PASS: `pwsh -NoProfile -File .\tools\build.ps1`
- PASS: `git diff --check`

## Notes

- BLE HID now samples `battery_monitor_read()` every 5 seconds and updates HID Battery Service only when the battery percentage changes by more than 1%, except forced startup/reconnect refreshes.
- Startup, HID start, and reconnect paths force a battery service refresh so a reconnected host receives the current level.
- Battery diagnostics log `DIAG_BLE_BATTERY_LEVEL` with percent, battery mV, raw ADC, and ADC mV.
- No hardware flash or live BLE host validation was run in this step.
