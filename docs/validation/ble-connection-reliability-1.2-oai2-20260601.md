# ble-connection-reliability 1.2 validation

Assignee: oai2
Date: 2026-06-01
Branch: `ai/tai2-ble-connection-reliability-1.2`

## Summary

Implemented firmware-side BLE audio backpressure using BLE notify queue depth and audio pool usage. Capture now pauses while BLE transport pressure is high or link readiness is lost, resumes after hysteresis, and bypasses the pause when stop/cancel is requested so sessions can finish cleanly.

Diagnostics added:

- `DIAG_AUDIO_BACKPRESSURE` for capture pause/resume state changes.
- `DIAG_BAUD_BACKPRESSURE` for BLE stream pause/resume state changes.
- `DIAG_BAUD_WATERMARK` for queue/pool/pressure watermarks in exported diagnostics.

## Validation

Commands run from `C:\Users\Billy\Desktop\listener\voice-keyboard-firmware-wt-tai2-ble-connection-reliability-1.2`.

| Check | Result |
| --- | --- |
| `pwsh -NoProfile -File .\tools\verify_ble_audio_backpressure_static.ps1` | PASS: BLE audio backpressure static checks passed. |
| `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check` | PASS: firmware repo feature script is present, concise, and covers ESP32-S3 BLE HID/audio diagnostics. |
| `python -m compileall -q tools` | PASS |
| `git diff --check` | PASS |
| `pwsh -NoProfile -File .\tools\build.ps1` | PASS: ESP-IDF 5.5 build completed, app size `0xaa310`, 61% app partition free. |
| `pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1` | PASS: diagnostic log coverage checks passed. |

Hardware validation used workflow locks for `COM5` and `BLE-E80B41BCC9A1`.

| Check | Result |
| --- | --- |
| Flash current build with `idf.py -B C:\Users\Billy\AppData\Local\Temp\listener-idf-build-8550edf87f43 -p COM5 flash` | PASS |
| `pwsh -NoProfile -File .\tools\verify_audio_capture_session_end_to_end.ps1 -Port COM5 -CaptureSeconds 30 -NoResetBeforeCapture` | PASS: `expected_packet_count=2012`, `received_packet_count=2012`, `missing_packet_count=0`, `packet_loss_ratio=0.0000`, `duration_seconds=30.180`. |
| `pwsh -NoProfile -File .\tools\collect_ai_diagnostics.ps1 -Port COM5 -EventCount 120 -ReadSeconds 8 -OutputDir tests\artifacts\ble_connection_reliability_1_2_diag` | PASS: 120 events decoded; `counts_by_event` includes `ble_audio.baud_watermark=1`. |

Important runtime evidence:

- Transport summary: `notify_failed=0`, `audio_failed=0`, `queue_jobs_purged=0`, `pool_high_water=17`, `pool_capacity=36`, `pool_alloc_failed=0`, `queue_full=0`, `last_error=0`.
- Diagnostic export event: `ble_audio.baud_watermark` at `t_ms=118709`, `session_id=1`, `queue_depth=0`, `pool_in_use=0`, `pressure_percent=0`.

Artifacts:

- `tests\artifacts\audio\verify_audio_capture_session_latest.log`
- `tests\artifacts\audio\capture_ble_latest_16k_mono.wav`
- `tests\artifacts\audio\source_ble_session_reference_latest_16k_mono.wav`
- `tests\artifacts\ble_connection_reliability_1_2_diag\manifest.json`
- `tests\artifacts\ble_connection_reliability_1_2_diag\diag_log_ai_bundle.json`
- `tests\artifacts\ble_connection_reliability_1_2_diag\diag_log_raw.jsonl`
