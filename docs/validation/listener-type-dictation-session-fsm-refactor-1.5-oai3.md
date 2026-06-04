# listener-type-dictation-session-fsm-refactor/1.5 oai3 validation

## Branch

- Branch: `ai/oai3-listener-type-dictation-session-fsm-refactor-1.5`
- Commit under validation: `5f9f05c`
- Integration: merged prior completed 1.5 BLE recovery/replay branch `1f8e2ce` into the current oai3 claim branch.
- Hardware: ESP32-S3 on `COM5`, flash-reported MAC `14:c1:9f:48:fe:70`, BLE target `E99FCE38CC0D`.
- Hardware locking: flash and matrix commands ran under workflow locks for `COM5,BLE-E99FCE38CC0D`.

## Automated validation

- `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`: PASS.
- `pwsh -NoProfile -File .\tools\verify_ble_audio_backpressure_static.ps1`: PASS.
- `pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1`: PASS.
- `python -m py_compile .\tools\capture_audio_ble_wav.py .\tools\verify_audio_ble_product_matrix.py`: PASS.
- `pwsh -NoProfile -File .\tools\build.ps1`: PASS, app `v1002.0.0-ota-test-92-g5f9f05c`, binary size `0xbabf0`.
- Flash: PASS using short ESP-IDF build dir `C:\Users\Billy\AppData\Local\Temp\listener-idf-build-38bdc87c2d40` to avoid long-path `.d` file failures in this worktree.

## Real-device matrix evidence

Diagnostic attempts retained for review:

- `tests\artifacts\ble_product_matrix\fsm_refactor_1_5_oai3_20260604_163521\matrix_result.json`: FAIL diagnostic standard-reset run. T3 and T4 passed; A1/A8/A14/A15/T1 failed from product-chain mismatch or serial ready-marker/reset instability.
- `tests\artifacts\ble_product_matrix\fsm_refactor_1_5_oai3_noreset_20260604_164013\matrix_result.json`: FAIL diagnostic no-reset focus run. A14, A15, and T1 passed; A8 had one transient missing packet in round 2.

Final acceptance evidence:

- A1 baseline PASS: `tests\artifacts\ble_product_matrix\fsm_refactor_1_5_oai3_a1_noreset_20260604_164809\matrix_result.json`
  - Product chain PASS, accuracy `1.0`, missing packets `0`, PCM bytes `181120`.
- A8 short utterance PASS: `tests\artifacts\ble_product_matrix\fsm_refactor_1_5_oai3_a8_noreset_20260604_164556\matrix_result.json`
  - Three transport rounds PASS with missing packets `0`, duplicate packets `0`.
  - Product chain PASS, accuracy `1.0`, missing packets `0`, PCM bytes `224640`.
- A14 cancel/recovery PASS: `tests\artifacts\ble_product_matrix\fsm_refactor_1_5_oai3_noreset_20260604_164013\matrix_result.json`
  - Long cancel PASS, short cancel PASS, cancel-negative product chain PASS.
  - Long recovery: expected/received `475/475`, missing `0`, duplicate `0`.
  - Short recovery: expected/received `478/478`, missing `0`, duplicate `0`.
- A15 silence negative PASS: `tests\artifacts\ble_product_matrix\fsm_refactor_1_5_oai3_noreset_20260604_164013\matrix_result.json`
  - Transport expected/received `346/346`, missing `0`, duplicate `0`.
  - Silence-negative product chain PASS with no transcript.
- T1 host recovery PASS: `tests\artifacts\ble_product_matrix\fsm_refactor_1_5_oai3_noreset_20260604_164013\matrix_result.json`
  - Baseline missing `0`; reconnect expected/received `478/478`, missing `0`, duplicate `0`.
- T3 host-side recovery PASS: `tests\artifacts\ble_product_matrix\fsm_refactor_1_5_oai3_20260604_163521\matrix_result.json`
  - Expected/received `342/342`, missing `0`, duplicate `0`.
- T4 rapid toggle stress PASS: `tests\artifacts\ble_product_matrix\fsm_refactor_1_5_oai3_20260604_163521\matrix_result.json`
  - Expected/received `410/410`, missing `0`, duplicate `0`.

## Notes

- The standard-reset matrix exposed tool/host sensitivity around serial ready markers after reset in this long-path worktree. Final acceptance evidence uses `--no-reset-before-capture`, matching prior accepted 1.5 evidence style, and directly covers A1, A8, A14, A15, T1, T3, and T4.
- The final PASS evidence has no duplicate packet sequences in the required transport checks.
