# Listener Release Snapshot 2026-06-22 13:28 CST

## Revisions

- Firmware: `Listener-Firmware` `master` at `e72019fa269ec6d5b88156c0d362e366bfb5de87`.
- Firmware tree: `11c5ac4696e9ad924bda364d49d9579ddf64e13b`.
- Listener-Type: `main` at `08f557233339b094761651b2cbd79d3cc87a2f70`.

`e72019f` is tree-identical to the validated `37b6948`; it records the low-power LED consolidation branch after the useful fixes were cherry-picked into `master`.

## Integrated Firmware Changes

- GPIO7 USB_DET is disabled/high-Z at runtime; computer USB is inferred from USB Serial/JTAG SOF, and charger/full status comes from BAT_CHG/BAT_STD with retention.
- Status LEDs keep the DMA-backed RMT path, full-frame DMA buffer contract, low idle-drive policy, and restrained idle brightness.
- `recording_active` and `capture_active` preview states now share the device-mic capture path, so recording effect review preserves PWR/BLE baseline LEDs.
- BLE `TYPE_READY` is reported as `type_ready` and is treated as a ready BLE state for status LED rendering.

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

Listener-Type:

- PASS: `git diff --check`
- PASS: `npm run build` (non-fatal Vite chunk-size warning)
- PASS: `npm test`
- PASS: `cargo test --manifest-path src-tauri\Cargo.toml device_settings --lib` (9 passed, 2 ignored hardware smoke tests)
- PASS: `cargo test --manifest-path src-tauri\Cargo.toml embedded_ble --lib` (56 passed, 2 ignored hardware smoke tests)
- PASS: `pwsh -NoProfile -File tools\ai\repo_features.ps1 -Check`

## Hardware Status

No flash was performed for this snapshot. The workflow board ledger still reports COM10 at firmware `183f9557c960`, so the physical board may be behind this `master` snapshot until the next locked flash.
