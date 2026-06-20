# Capsule Firmware Sync Smoke

Status: PASS.

Date: 2026-06-20
Agent: oai1

## Root Cause

Desktop capsule cancel was not reliably synchronized to firmware because Listener-Type queued `VREC:CANCEL` through the active BLE capture control path while local cancel teardown was closing that capture. The queued control write could time out, and the old smoke then sent a serial cleanup `~VREC:CANCEL`, which could mask the product failure.

Desktop capsule confirm with no transcript had a related teardown race: `VREC:PROCESSING:WARN` was sent after BLE notify/capture teardown, so the active audio-control sender could time out before firmware saw the warning.

## Fix Under Test

- Listener-Type product commits:
  - `e947b37fa6a3dae7d85b7b0f125832049cfe75e8` sends firmware cancel before local BLE capture teardown.
  - `0b58d26c5ecb7aac20d1f65e10c6eaf104a152f4` falls back to a fresh GATT audio-control write when the active capture sender is stale/closed, so OK/WARN completion commands still reach firmware after capture teardown.
- Firmware product commit under test: `5584dc7e5b56863a0fcea2c2d930bb380ce85aca`.
- Smoke fix: add `desktop-confirm`, remove serial end commands for desktop capsule actions, require BLE-control firmware evidence instead of accepting serial cleanup evidence, and expect WARN instead of OK for no-transcript confirm.

## Desktop Cancel Evidence

- Artifact directory: `desktop-cancel-sync-post-fallback-pass/`
- Report: `desktop-cancel-sync-post-fallback-pass/ble-stream-smoke.20260620-204009.json`
- Screenshot: `desktop-cancel-sync-post-fallback-pass/ble-stream-smoke-20260620-203957.desktop-cancel.png`
- Result: PASS.
- Firmware evidence:
  - `recording_cancel_ble_control_seen=true`
  - `recording_cancel_usb_seen=false`
  - `audio_control_cancel_write_seen=true`
  - `cancel_completed=true`
  - `led_recording_cleared_seen=true`

## Desktop Confirm Evidence

- Artifact directory: `desktop-confirm-sync-post-fallback-pass/`
- Report: `desktop-confirm-sync-post-fallback-pass/ble-stream-smoke.20260620-203943.json`
- Screenshot: `desktop-confirm-sync-post-fallback-pass/ble-stream-smoke-20260620-203919.desktop-confirm.png`
- Result: PASS.
- Product path evidence:
  - transcript/final text produced
  - `insert_status=inserted`
  - history session includes embedded audio stats
  - `missing_packets=0`
- Firmware and LED evidence:
  - `recording_stop_ble_control_seen=true`
  - `audio_control_stop_write_seen=true`
  - `audio_control_processing_start_seen=true`
  - `audio_control_processing_done_seen=true`
  - `led_recording_active_seen=true`
  - `led_recording_cleared_seen=true`
  - `led_ai_active_seen=true`
  - `led_ok_active_seen=true`
  - `led_warn_active_seen=false`

## Desktop Confirm No-Transcript Warning Evidence

- Artifact directory: `desktop-confirm-silent-warning-pass/`
- Report: `desktop-confirm-silent-warning-pass/ble-stream-smoke.20260620-203904.json`
- Screenshot: `desktop-confirm-silent-warning-pass/ble-stream-smoke-20260620-203846.desktop-confirm.png`
- Result: PASS.
- Product path evidence:
  - silent audio produced no transcript/final text
  - expected stream failure: `ASR returned empty transcript`
  - no insertion/history text was produced
- Firmware and LED evidence:
  - `recording_stop_ble_control_seen=true`
  - `audio_control_stop_write_seen=true`
  - `audio_control_processing_start_seen=true`
  - `audio_control_processing_warning_seen=true`
  - `led_recording_active_seen=true`
  - `led_recording_cleared_seen=true`
  - `led_ai_active_seen=true`
  - `led_warn_active_seen=true`
  - `led_ok_active_seen=false`

## Remaining Scope

This closes the AI-verifiable capsule cancel, successful confirm, and no-transcript warning firmware sync gaps. It does not replace separate clean-install/OOBE, physical trigger, or long real-use product go/no-go gates.
