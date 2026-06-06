# firmware-diag-log-source-filter/1.1 synthetic decoder evidence

Date: 2026-06-07

Scope: offline evidence for the Step 1.1 acceptance item requiring decoder/tool summaries to expose source names plus KEY1-KEY4 and EC11 bring-up counts when those events exist in a log.

Input:

- Synthetic JSONL path used during validation: `tests/artifacts/diag_log_source_filter_synthetic/synthetic_diag_log.jsonl`
- Decoder output path used during validation: `tests/artifacts/diag_log_source_filter_synthetic/offline_bundle/diag_log_ai_bundle.json`
- Collection manifest path used during validation: `tests/artifacts/diag_log_source_filter_synthetic/offline_bundle/manifest.json`

Observed summary from `tools/decode_diag_log.py` through `tools/collect_ai_diagnostics.ps1 -InputJsonl`:

- `summary.event_count`: 6
- `summary.counts_by_source`: `keyboard=4`, `power=1`, `voice_key=1`
- `summary.counts_by_event`: `keyboard.kbd_custom_key=2`, `keyboard.kbd_ec11_detent=2`, `power.power_usb_detect=1`, `voice_key.vkey_press=1`
- `summary.input_debug_summary.custom_keys.KEY1.press`: 1
- `summary.input_debug_summary.custom_keys.KEY1.release`: 1
- `summary.input_debug_summary.ec11.clockwise`: 1
- `summary.input_debug_summary.ec11.counterclockwise`: 1
- `summary.input_debug_summary.ec11.press`: 1
- `summary.input_debug_summary.ec11.last_detent_count`: 8

Formal validation report:

- `docs/validation/firmware-diag-log-source-filter-1.1-validation-evidence.json`
- Result: PASS for all seven Step 1.1 validation commands.

This is synthetic/offline evidence only. Live source toggling and serial bounded capture remain assigned to Step 1.2's hardware smoke gate.
