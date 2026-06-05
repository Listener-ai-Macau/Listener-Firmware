# voice-keyboard-extreme-stability-hardening/1.2 validation

Agent: oai3
Date: 2026-06-05
Worktree: `voice-keyboard-firmware-wt-oai3-voice-keyboard-extreme-stability-hardening-1.2`
Branch: `ai/oai3-voice-keyboard-extreme-stability-hardening-1.2`

## Scope

Implemented firmware recording-control FSM hardening in `components/voice_recording_control/voice_recording_control.c`.

## Acceptance evidence

- PASS: `voice_recording_control` now has a checked `VOICE_RECORDING_CONTROL_FSM_ARTIFACT` covering Idle, Recording, Transferring, Recovery, pending start, cancel pending, host cleanup guard, and user-vs-host source classification.
- PASS: `voice_recording_control_decide_transition()` is effect-free; audio capture, BLE recovery, power manager, status LED, key output, diag logging, and recovery reset stay in effect application boundaries.
- PASS: Table/static validation covers rapid next-start while transferring, host cleanup after abort, host cleanup with user pending start, transport-not-ready user start vs host cleanup, cancel while pending, cancel while transferring, recovery during recording, session finished/aborted without active capture, stale stop, and stale cancel.
- PASS: Diagnostics still include source, state, pending flag, cancel flag, session count, result code, and `device_status` user-visible states.
- PASS: Existing `verify_ble_audio_backpressure_static.ps1` was updated to validate the new FSM artifact shape and still passes.

## Validation commands

- PASS: `python tools\verify_voice_recording_control_fsm.py`
  - Result: `PASS: voice_recording_control FSM artifact covers 13 extreme transition cases and keeps decisions effect-free.`
- PASS: `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
  - Result: `Project build complete.`
  - Artifact: `C:\Users\Billy\AppData\Local\Temp\listener-idf-build-71e58258665c\voice-keyboard-firmware.bin`
- PASS: `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
  - Result: `PASS: firmware repo feature script is present, concise, and covers ESP32-S3 BLE HID/audio diagnostics.`
- PASS: `git diff --check`

## Extra regression

- PASS: `pwsh -NoProfile -File .\tools\verify_ble_audio_backpressure_static.ps1`
  - Result: `PASS: BLE audio backpressure static checks passed.`

## Not run

- No flash or real-device BLE/EC11 run was performed in this step. Hardware matrix closure belongs to later locked hardware steps.
