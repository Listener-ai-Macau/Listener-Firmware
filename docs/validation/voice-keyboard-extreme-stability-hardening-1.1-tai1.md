# voice-keyboard-extreme-stability-hardening/1.1 validation

Agent: tai1
Date: 2026-06-05
Worktree: `voice-keyboard-firmware-wt-tai1-voice-keyboard-extreme-stability-hardening-1.1`
Branch: `ai/tai1-voice-keyboard-extreme-stability-hardening-1.1`

## Scope

Implemented the firmware-repo extreme-use scenario contract and named matrix harness entry point. This step does not change firmware runtime behavior and does not claim manual/physical/external scenarios as passed.

## Acceptance evidence

- PASS: `!docs/features/extreme_stability_matrix.md` defines the durable V1/N4 extreme stability contract, including scenario IDs, expected user-visible behavior, firmware/desktop observables, failure classification, artifact requirements, non-goals, and baseline rules.
- PASS: `tools/verify_audio_ble_product_matrix.py --list-cases` exposes `case_suites.extreme = A1,A2,A14,T1,T3,L1,H1,H4,A17,A15,D1` without editing the script by hand.
- PASS: A1/A2 remain the automated baseline cases. Existing `auto`, `smoke`, `daily`, and `full` suites still resolve to A1/A2.
- PASS: Manual/external cases are recorded as structured skipped baselines with contract, skip reason, failure classification, and artifact requirements. Skipped cases are not PASS evidence; matrices with skipped cases use `PASS_WITH_SKIPS` and `matrix_incomplete=true`.
- PASS: Matrix result schema now includes firmware/Listener-Type commit metadata, optional Listener package hash, requested suite/cases, case contracts, failure timestamps for failed cases, and standardized A1/A2 product-chain fields.
- PASS: A1/A2 product-chain details standardize `timeline`, `history_session_id`, packet counters, firmware diag/serial refs, and Listener-Type log refs.
- PASS: `!docs/features/ble_audio_test_matrix.md` and `!docs/features/index.json` document the new extreme suite and preserve existing A1/A2 matrix behavior.

## Validation commands

- PASS: `python -m compileall -q tools\verify_audio_ble_product_matrix.py tools\capture_audio_ble_wav.py tools\ble_audio_regression_common.py`
- PASS: `pwsh -NoProfile -File .\tools\verify_ble_audio_backpressure_static.ps1`
  - Result: `PASS: BLE audio backpressure static checks passed.`
- PASS: `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
  - Result: `PASS: firmware repo feature script is present, concise, and covers ESP32-S3 BLE HID/audio diagnostics.`
- PASS: `git diff --check`
- PASS: `python .\tools\verify_audio_ble_product_matrix.py --list-cases | python -c "<catalog assertions>"`
  - Result: `PASS: extreme catalog contract exposes named suite, automated baselines, manual/external baselines, and required result fields`
- PASS: `Get-Content -LiteralPath '.\!docs\features\index.json' -Raw | ConvertFrom-Json`
  - Result: `PASS: feature index JSON parses`

## Not run

- No real-device matrix was run in this step. Hardware execution belongs to later dependent steps and must use workflow `with-lock` for COM/BLE resources.
- Physical EC11/KEY1-KEY4 stress, BLE disconnect/reconnect, stale GATT/cache recovery, Listener-Type restart, idle/sleep resume, silent negative case, and diagnostic export closure remain explicit manual/external baselines in the contract until later automation or locked evidence exists.

## Files changed

- `tools/verify_audio_ble_product_matrix.py`
- `!docs/features/extreme_stability_matrix.md`
- `!docs/features/ble_audio_test_matrix.md`
- `!docs/features/index.json`
- `docs/validation/voice-keyboard-extreme-stability-hardening-1.1-tai1.md`
