# voice-keyboard-extreme-stability-hardening/1.3 validation

Agent: oai1
Date: 2026-06-05
Repo: voice-keyboard-firmware
Branch: ai/oai1-voice-keyboard-extreme-stability-hardening-1.3

## Scope

Firmware BLE audio transport invariants for the current V1/N4 product path.

Implemented:

- Added a BLE audio transport invariant contract directly beside the transport FSM, covering NimBLE/GATT callback boundaries, export-task session ownership, audio-capture backpressure, replay retention/removal, and stale event discard.
- Added replay diagnostics to session transport summaries: retained high-water, stored/replaced/removed packets, resent packets, resend failures, current-packet skips, and pending replay count.
- Added `DIAG_BAUD_REPLAY` diag_log events for replay arm, resend, skip-current, and resend failure evidence.
- Added `tools/verify_ble_audio_transport_model.py`, a pure model plus static source guard for connection epoch, notify readiness, active session ownership, replay, queue/pool backpressure hysteresis, stale GATT events, and bounded retry timeout.
- Updated existing static/diagnostic/repo feature checks so future changes cannot drop the model, replay summary, or replay diagnostic event silently.

## Non-Hardware Boundary

This step did not flash or run a locked real device. The plan validation for 1.3 is model/static/build based; real-device closure remains in later hardware-gated steps.

## Validation

- PASS: `python tools\verify_ble_audio_transport_model.py`
- PASS: `pwsh -NoProfile -File .\tools\verify_ble_audio_backpressure_static.ps1`
- PASS: `pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1`
- PASS: `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
- PASS: `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
- PASS: `git diff --check` (Git printed CRLF normalization warnings only)

Evidence JSON: `docs/validation/voice-keyboard-extreme-stability-hardening-1.3-oai1-validation-evidence.json`.

## Self Review

Likely failure paths checked:

- Replay duplicates: model and static guard require current-packet skip and duplicate sequence replacement instead of double resend.
- Stop during recovery: model requires queued stop to wait for replay drain before terminal stop.
- Cancel during tail drain: model requires cancel to remain session-owned and clear queued/replay tail state.
- Stale GATT callbacks: model requires stale subscribe, MTU, notify_tx, and disconnect events to be counted and ignored after epoch/conn change.
- Backpressure: model and static guard require 95/70 queue/pool hysteresis and avoid link-down-only capture pause.
