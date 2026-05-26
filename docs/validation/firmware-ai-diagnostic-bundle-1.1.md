# Firmware AI diagnostic bundle 1.1 validation

## Sample input

Saved `~DIAGLOG` JSONL from `tests/artifacts/embedded_diag_log_1_rework/diag_log_20260525-200027.jsonl`:

```jsonl
{"t":126411,"src":"audio","evt":2,"sev":"INFO","a1":1,"a2":1,"a3":0,"a4":0}
{"t":126426,"src":"ble_audio","evt":1,"sev":"INFO","a1":4,"a2":5,"a3":3548265880,"a4":1}
{"t":126429,"src":"voice_rec","evt":1,"sev":"INFO","a1":1,"a2":2,"a3":1,"a4":0}
{"t":128527,"src":"ble_audio","evt":1,"sev":"INFO","a1":5,"a2":6,"a3":572995118,"a4":1}
{"t":128545,"src":"voice_rec","evt":1,"sev":"INFO","a1":3,"a2":2,"a3":1,"a4":0}
```

## Validation run

- `python -m compileall -q tools` -> PASS.
- PowerShell parser check for `tools/collect_ai_diagnostics.ps1` -> PASS.
- `python .\tools\decode_diag_log.py --input .\tests\artifacts\embedded_diag_log_1_rework\diag_log_20260525-200027.jsonl --output .\tests\artifacts\ai_diagnostics_validation\embedded_diag_decoded.json` -> PASS, 8 events.
- `python .\tools\decode_diag_log.py --input .\tests\artifacts\ota_update_1_1\final_pending_verify_mark_valid_com5.log --output .\tests\artifacts\ai_diagnostics_validation\ota_log_decoded.json` -> PASS, 30 JSON events parsed from mixed serial log, 2 boot segments detected.
- `pwsh -NoProfile -File .\tools\collect_ai_diagnostics.ps1 -InputJsonl .\tests\artifacts\embedded_diag_log_1_rework\diag_log_20260525-200027.jsonl -OutputDir .\tests\artifacts\ai_diagnostics_validation\wrapper_smoke` -> PASS, wrote `diag_log_raw.jsonl`, `diag_log_ai_bundle.json`, and `manifest.json`.
- `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check` -> PASS.
- `git diff --check` -> PASS.

## Machine-readable output spot check

The decoded bundle preserves the raw event and adds stable fields for AI agents:

```json
{
  "schema_id": "listener.firmware.diag_log.ai_bundle.v1",
  "summary": {
    "event_count": 8,
    "counts_by_source": {
      "audio": 1,
      "ble_audio": 5,
      "voice_rec": 2
    }
  },
  "first_event": {
    "source": {"name": "audio", "macro": "DIAG_SRC_AUDIO"},
    "event": {"name": "audio_session", "macro": "DIAG_AUDIO_SESSION"},
    "args_raw": {"a1": 1, "a2": 1, "a3": 0, "a4": 0},
    "args_named": {"type": 1, "session_id": 1, "duration_ms": 0, "frame_count": 0},
    "args_decoded": {"type": "start"},
    "raw_event": {"t": 126411, "src": "audio", "evt": 2, "sev": "INFO", "a1": 1, "a2": 1, "a3": 0, "a4": 0}
  }
}
```
