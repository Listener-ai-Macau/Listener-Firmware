# PWR_HOLD New Board Recheck - 2026-06-16

## Result

FAIL for real hardware power-off. Firmware now uses the latest physical board contract, but the new board still does not let firmware drive `PWR_HOLD/GPIO9` high.

## What Changed

- Restored the latest V2 pin contract in firmware: `BAT_V_ADC/GPIO10`, `PWR_HOLD/GPIO9`, and absent TPS63020/SY7088 current telemetry (`present=0 gpio=-1`).
- Added `flash.ps1` `erase-otadata` before normal development flashing so the board boots the just-flashed `ota_0` image instead of a stale OTA slot.
- Added focused serial helpers for real PWR_HOLD validation and battery-only shutdown evidence.
- Updated stale visible text, self-tests, and docs from GPIO11/GPIO8/current-sense populated assumptions.

## Evidence

- `final-post-flash-status-20260616-1209/status.txt`: final flashed firmware reports `pwr_hold_gpio=9`, `battery_gpio=10`, both current telemetry rows `present=0 gpio=-1`, and `auto_shutdown_ms=1800000`.
- `pwr-hold-new-board-gpio9-direct-20260616-1152/pwr_hold_high_transcript.txt`: direct `~BOARD:PWR-HOLD:HIGH` on the GPIO9 firmware returns `ESP_ERR_INVALID_STATE`; GPIO9 remains low and firmware restores runtime low.
- `battery-only-auto-shutdown-resume-20260616-1202/diag_log_ai_bundle.json`: battery-only idle reaches `power_sleep_entry` after about 61 seconds with `shutdown_reason=long_idle`, but `power_hold_state action=shutdown_drive_high` records `level=unknown`, then `shutdown_failed_restore`, `power_sleep_blocked`, and shutdown-failure backoff.
- `pwr-hold-new-board-20260616-113854/` shows the pre-fix stale GPIO11 behavior.
- `pwr-hold-new-board-20260616-114418/` shows the GPIO9 firmware still failing `PWR_HOLD` high readback while USB is attached.

## Conclusion

The prior "rollback" symptom had two causes:

- the GPIO9/GPIO10/no-current-sense pin-map fix existed only on an adhoc branch and had not been merged into current `master`;
- development flashing could leave the board booting a stale OTA slot until `otadata` was erased.

Those software causes are fixed here. The remaining blocker is physical/electrical: with USB removed and battery-only auto shutdown active, firmware enters the shutdown path but cannot make `PWR_HOLD/GPIO9` read high. The next hardware check should measure the ESP32-S3 GPIO9 pad and the PWR_HOLD latch net during the shutdown attempt, including any external pull/load that keeps the line low.
