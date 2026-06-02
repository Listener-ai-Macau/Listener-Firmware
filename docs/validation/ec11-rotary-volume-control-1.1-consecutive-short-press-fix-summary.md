# EC11 Consecutive Short Press Fix Summary

## Problem Evidence

- Before-fix diag artifact: `docs/validation/ec11-rotary-volume-control-1.1-diag-before-decoded.json`
- The rapid short-press sequence reached `voice_key.vkey_press` with `double_click_recovery` at `t_ms=3181484`.
- Firmware then recorded `voice_rec.vrec_flow` stage `recovery` at `t_ms=3181524`.
- BLE recovery side effects followed, including `ble_gap.gap_bond` and `ble_hid.ble_disconnect`.

## Fix

- Added `VOICE_KEY_INPUT_RECOVERY_IDLE_GUARD_MS=2000`.
- Shortened `VOICE_KEY_INPUT_DOUBLE_CLICK_WINDOW_MS` from 250 ms to 200 ms to reduce accidental recovery recognition.
- Double-click recovery is now allowed only when recording output is not active and has been idle for at least 2 seconds after the last recording output state change.
- Consecutive short clicks during recording or immediately after recording stop are kept as single-click recording toggles instead of recovery.
- Board help now states that EC11 double-click recovery applies after recording has been idle.
- Static verifier now checks the idle guard, guard log string, and help text.

## Validation

- PASS: `pwsh -NoProfile -File .\tools\verify_v2_board_profile_static.ps1`
- PASS: `pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1`
- PASS: `pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1`
- PASS: `python -m compileall -q tools`
- PASS: `pwsh -NoProfile -File .\tools\build.ps1`
- PASS: flashed COM5 under workflow hardware lock with `tools\flash.ps1 -Port COM5`.

## Post-Flash Evidence

- Final flash target: COM5, ESP32-S3 MAC `14:c1:9f:48:fe:70`.
- Post-flash serial artifact: `docs/validation/ec11-rotary-volume-control-1.1-consecutive-short-press-after-reflash.log`.
- Post-flash diag dump: `docs/validation/diag_log_20260602-142109.jsonl`.
- Post-flash decoded diag: `docs/validation/ec11-rotary-volume-control-1.1-diag-after-reflash-decoded.json`.

## Remaining Manual Evidence Gap

The post-flash serial capture window was interrupted by the serial-open reset behavior and did not capture fresh `voice_key` press events. The code path is built and flashed, and the before-fix fault is confirmed, but a clean physical short-press retest should still be captured once the board can be pressed during a stable no-reset monitor window.
