# Status LED 1.9 Flicker Guard And Shutdown Confirm

Date: 2026-06-17
Agent: oai1
Hardware: COM10, board MAC a4:cb:8f:f4:59:d4

## Scope

- Reduce intermittent status-tail flicker where `LED5=OK` / `LED6=WARN` can appear to mirror `LED3=REC` / `LED4=AI`.
- Keep status, key, EC11 knob, and edge/frame zones independent except explicit error and shutdown-confirm effects.
- Add a long-press shutdown confirmation effect.

## Design Reference

Mature-product pattern used for this pass:

- Google Nest / Home status lights keep setup and problem states as simple color/pattern semantics.
- Apple AirPods / HomePod use short amber/white/green status-light confirmations for charge, reset, setup, and update states.
- Corsair iCUE and Razer Chroma keep decorative RGB effects configurable and separate from core status meaning.

This maps to Listener as: semantic status rail stays clear and low-noise; EC11 and edge/frame provide restrained accents; long-press shutdown gets a warm amber confirmation cue.

## Evidence

- `python .\tools\verify_status_led_static.py`: PASS.
- `pwsh -NoProfile -File .\tools\verify_ble_status_led_connected_sync.ps1`: PASS.
- `pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1`: PASS.
- `git diff --check`: PASS.
- `pwsh -NoProfile -File .\tools\build.ps1`: PASS.
- `aiw with-lock COM10` + `tools\flash.ps1 -Port COM10 -NoBuild`: PASS.
- `docs/validation/status-led-1.9-flicker-guard-shutdown-confirm/serial-preview-20260617-oai1.txt`: hardware serial preview evidence.
- `docs/validation/status-led-1.9-flicker-guard-shutdown-confirm/diag_log_20260617-123855.jsonl`: bounded flash diag tail, 120 events.
- `docs/validation/status-led-1.9-flicker-guard-shutdown-confirm/diag_log_20260617-123855.decoded.json`: decoded flash diag bundle.

## Key Serial Results

`~LED:PREVIEW recording_processing` followed by `~LED:STATUS` reported:

```text
status_rgb=PWR:0,59,0;BLE:0,0,61;REC:48,32,0;AI:41,0,66;OK:0,0,0;WARN:0,0,0
active_flags=PWR:1,BLE:1,REC:1,AI:1,OK:0,WARN:0,EC11:1,KEY:0,EDGE:1
```

This proves firmware state and transmitted status-frame data keep `LED5=OK` and `LED6=WARN` off during the REC+AI overlap preview.

`~LED:PREVIEW shutdown_confirm` reported a pending amber cue:

```text
status_rgb=PWR:59,59,0;BLE:0,0,61;REC:0,0,0;AI:0,0,0;OK:0,0,0;WARN:0,0,0
ec11_rgb=px1:23,9,0;px2:23,9,0;px3:23,9,0;px4:23,9,0;px5:23,9,0;px6:0,0,0;...
edge_rgb=px1:23,9,0;px2:0,0,0;px3:23,9,0;px4:23,9,0;px5:0,0,0;px6:23,9,0
```

`~LED:PREVIEW shutdown_final` reported the final amber cue:

```text
status_rgb=PWR:92,59,0;BLE:0,0,61;REC:0,0,0;AI:0,0,0;OK:0,0,0;WARN:0,0,0
ec11_rgb=px1:46,17,0;px2:46,17,0;...;px12:46,17,0
edge_rgb=px1:38,14,0;px2:38,14,0;...;px6:38,14,0
```

## Residual Risk

No human visual confirmation was performed in this pass. If a camera or eye still sees LED5/LED6 flashing while `~LED:STATUS` reports `OK:0,0,0;WARN:0,0,0`, the remaining cause is below semantic state: WS2812 signal integrity, physical strip tail behavior, or capture exposure/rolling-shutter artifact.
