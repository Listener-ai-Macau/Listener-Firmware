# Manual Shutdown Command Bring-Up

Date: 2026-06-16
Port: COM9, resolved through `aiw with-lock -Resource COMx`

## Firmware Under Test

Built and flashed normal firmware from this worktree after adding:

- `~DEVICE:SET auto_shutdown_minutes=off` / `auto_shutdown_ms=0`
- `~POWER:TEST:SHUTDOWN` as a bring-up alias for `~POWER:SHUTDOWN`
- `~POWER:STATUS automatic_shutdown_enabled=...`

## Evidence

- `disable_auto_shutdown_status.txt`
  - `~DEVICE:SET auto_shutdown_minutes=off` returned `result=OK`
  - `~DEVICE:SETTINGS` reported `auto_shutdown_ms=0 auto_shutdown_enabled=0 auto_shutdown_mode=disabled`
  - `~POWER:STATUS` reported `automatic_shutdown_enabled=0 automatic_shutdown_blocked_by_external_power=0 hardware_shutdown_ms=0 pwr_hold_level=low`
- `manual_test_shutdown.txt`
  - `~POWER:TEST:SHUTDOWN` returned `result=accepted trigger=serial_manual command=TEST:SHUTDOWN`
  - GPIO9 still did not settle high within 500 ms
  - Firmware restored runtime low and kept serial interaction alive for debugging

## Result

PASS for the firmware control path: the board can run normal firmware without timed auto-shutdown, and shutdown testing is now serial-triggered.

FAIL for the current hardware power-off path: `PWR_HOLD/GPIO9` remains externally held low or otherwise cannot be driven/read back high on this board.
