# Listener Release Snapshot 2026-06-22 13:28 CST

## Revisions

- Firmware release base: `Listener-Firmware` `master` at `e72019fa269ec6d5b88156c0d362e366bfb5de87`.
- Firmware release base tree: `11c5ac4696e9ad924bda364d49d9579ddf64e13b`.
- Current local `master`: `d7ea9fee2c5db6b686c8d70934673f2436404a0a` (`7ec06b7935b615590749632c3262c2db5d1fd732` firmware code plus post-flash evidence).
- Listener-Type: `main` at `08f557233339b094761651b2cbd79d3cc87a2f70`.

`e72019f` is tree-identical to the validated `37b6948`; it records the low-power LED consolidation branch after the useful fixes were cherry-picked into `master`.

## Integrated Firmware Changes

- GPIO7 USB_DET is disabled/high-Z at runtime; computer USB is inferred from USB Serial/JTAG SOF, and charger/full status comes from BAT_CHG/BAT_STD with retention.
- Status LEDs keep the DMA-backed RMT path, full-frame DMA buffer contract, low idle-drive policy, and restrained idle brightness.
- `recording_active` and `capture_active` preview states now share the device-mic capture path, so recording effect review preserves PWR/BLE baseline LEDs.
- BLE `TYPE_READY` is reported as `type_ready` and is treated as a ready BLE state for status LED rendering.
- Idle PWR/BLE latch follow-up: `7ec06b7` keeps low-power PWR visible using the retained battery display sample with an amber fallback, and treats BLE `TYPE_READY` as connected for the low-blue idle latch.

## Cleanup

- Removed obsolete firmware worktrees and the old acceptance clone after preserving evidence.
- Preserved scratch checkpoints: `9e9cc90` (pwr temp cleanup), `e11f583` (future hw-rev dirty work), `e374e58` (old 1.6 validation dirty work).
- Imported old acceptance-clone scratch branches under `human/scratch/accept-clone/...` before deleting that clone.
- Listener-Type has no extra worktrees.

## Validation

Firmware:

- PASS: `git diff --check`
- PASS: `python tools\verify_status_led_static.py`
- PASS: `python tools\verify_voice_recording_control_fsm.py`
- PASS: `pwsh -NoProfile -File tools\verify_power_manager_static.ps1`
- PASS: `pwsh -NoProfile -File tools\verify_charging_awake_policy_static.ps1`
- PASS: `pwsh -NoProfile -File tools\verify_v2_board_profile_static.ps1`
- PASS: `pwsh -NoProfile -File tools\verify_device_settings_static.ps1`
- PASS: `pwsh -NoProfile -File tools\verify_ble_battery_service_static.ps1`
- PASS: `pwsh -NoProfile -File tools\verify_battery_monitor_static.ps1`
- PASS: `pwsh -NoProfile -File tools\ai\repo_features.ps1 -Check`
- PASS: `pwsh -NoProfile -File tools\test.ps1` (`voice-keyboard-firmware.bin` size `0xd1fc0`, app partition free `0x52e040`, 86%).
- PASS: follow-up `7ec06b7` checks: `git diff --check`, `python tools\verify_status_led_static.py`, `pwsh -NoProfile -File tools\verify_power_manager_static.ps1`, `pwsh -NoProfile -File tools\verify_charging_awake_policy_static.ps1`, and `pwsh -NoProfile -File tools\build.ps1` (`voice-keyboard-firmware.bin` size `0xd2000`, app partition free `0x52e000`, 86%).

Listener-Type:

- PASS: `git diff --check`
- PASS: `npm run build` (non-fatal Vite chunk-size warning)
- PASS: `npm test`
- PASS: `cargo test --manifest-path src-tauri\Cargo.toml device_settings --lib` (9 passed, 2 ignored hardware smoke tests)
- PASS: `cargo test --manifest-path src-tauri\Cargo.toml embedded_ble --lib` (56 passed, 2 ignored hardware smoke tests)
- PASS: `pwsh -NoProfile -File tools\ai\repo_features.ps1 -Check`

## Hardware Status

- PASS: COM10 was flashed through `aiw with-lock` using the current code tree (`e72019f`, same firmware tree as this snapshot before validation-only commits).
- PASS: `tools\verify_status_led_hardware.ps1 -Port COM10 -OutputDir docs\validation\listener-release-snapshot-20260622-132816\hardware-status-led -NoBuild` captured 43 camera frames plus serial status; required serial tokens missing: `0`.
- Evidence: `docs\validation\listener-release-snapshot-20260622-132816\hardware-status-led\hardware-led-validation.md`.
- Additional status capture: `docs\validation\idle-recording-led-debug-20260622-1330\post-flash-status.txt`.
- PASS: COM10 board ledger, post-flash serial status, post-replug serial status, and bounded diagnostic export were updated after flashing `7ec06b7`; evidence: `docs\validation\idle-status-led-latch-20260622-134315\post_flash_serial_status_after_peer.txt`, `docs\validation\idle-status-led-latch-20260622-134315\post_replug_serial_status.txt`, and `docs\validation\idle-status-led-latch-20260622-134315\diag\manifest.json`.
- FAIL/follow-up: `docs\validation\idle-current-guided-e72019f-20260622-1332\unplug_wake_summary.json` detected unplug/replug recovery but found a reset banner after replug (`post_no_reset_banner=false`). Treat this as a low-power wake/replug defect to investigate next; it does not invalidate the status LED hardware PASS.
