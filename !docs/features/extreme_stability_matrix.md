# Extreme stability matrix contract

## Status

- status: `contract-and-harness`
- scope: current V1/N4 voice keyboard stability validation across firmware BLE audio, Listener-Type product chain, physical keys, recovery, and diagnostics.
- source_of_truth: `tools/verify_audio_ble_product_matrix.py` `EXTREME_CASES`, `CASE_CONTRACTS`, and `MANUAL_OR_EXTERNAL_CASES`.

This contract is the baseline for later firmware and Listener-Type hardening. It names the scenarios, expected user-visible behavior, observables, failure classes, and required artifacts before changing firmware runtime logic.

## Non-goals

- This step does not rewrite `voice_recording_control` or `ble_audio_stream`.
- This step does not claim physical EC11/KEY1-KEY4, BLE disconnect/reconnect, idle/sleep, or diagnostic-export PASS without a locked real-device run.
- This step does not remove the current A1/A2 smoke and daily matrix entry points.

## Suites

| Suite | Cases | Purpose |
|---|---|---|
| `auto`, `smoke`, `daily`, `full` | A1,A2 | Existing low-cost product-chain baseline. |
| `extreme` | A1,A2,A14,T1,T3,L1,H1,H4,A17,A15,D1 | Full stability contract. A1/A2 are automated; the remaining cases are structured skipped/manual baselines until their real runners or hardware evidence exist. |

Recommended locked release-closure command shape:

```powershell
pwsh -NoProfile -File ..\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COMx,BLE-<address> -Run python .\tools\verify_audio_ble_product_matrix.py --port COMx --cases extreme --a1-round-count 20 --inter-session-gap-min-seconds 0 --inter-session-gap-max-seconds 1 --long-capture-seconds 60 --full-chain-audio-profile fast --fail-on-warning --continue-on-failure
```

The command above intentionally records skipped cases for manual/external scenarios until dedicated automation exists.

## Scenarios

| ID | Scenario | Expected user-visible behavior | Automation |
|---|---|---|---|
| A1 | Rapid short recordings with 0-1s next-start gap after previous text/history. | Each legal short recording shows a fresh capsule quickly, inserts its own text, and does not lose a round silently. | automated |
| A2 | About 60s fast long-form dictation in the extreme suite. | Capsule stays visible, final text inserts once, and user can continue after completion. | automated |
| A14 | Cancel during active/pending capture, then retry. | Cancel does not insert stale text; retry starts a fresh session. | manual/external |
| T1 | BLE disconnect/reconnect. | Recovery reaches ready or shows actionable reconnect error; no legal press is swallowed without explanation. | manual/external |
| T3 | Notify disabled or Windows stale GATT/cache recovery. | Notify readiness is repaired or the app gives a clear next step; no false successful recording without audio. | manual/external |
| L1 | Listener-Type restart while firmware stays powered. | App rediscovers/recovers device and next recording behaves like a fresh session. | manual/external |
| H1 | Physical EC11 start/stop/recovery stress. | Physical presses map to one session action each without stuck recording/transferring UI. | manual hardware |
| H4 | KEY1-KEY4 custom/fallback stress. | Key actions deliver or explain failure without disrupting legal voice recordings. | manual hardware |
| A17 | Idle or sleep/resume before first recording. | Next legal press starts or explains recovery instead of being swallowed. | manual/external |
| A15 | Silent or accidental trigger negative case. | No meaningful text is inserted; any visible error explains no speech. | manual/external |
| D1 | Diagnostic export after an extreme or failed scenario. | Support package aligns firmware, BLE, desktop, and UI timeline with package identity. | manual/external |

## Required observables

Matrix JSON must retain these fields when a case produces them:

- `text_to_next_capsule_latencies`
- capsule source timestamps from Listener-Type timeline
- `history_session_id`
- expected/received/missing/duplicate packet counts
- firmware diag or serial references
- Listener-Type stdout/stderr/report/log references
- firmware and Listener-Type commit metadata
- optional Listener package path and SHA256
- exact `failure_timestamp_utc` for failed cases

Skipped manual/external cases still record the contract, skip reason, failure classification, and artifact requirements in the matrix JSON. They are not PASS evidence; a matrix containing skipped cases reports `PASS_WITH_SKIPS` and `matrix_incomplete=true` unless a real failure is present.

## Artifact paths

- Matrix result JSON: `tests\artifacts\ble_product_matrix\matrix_result.json` unless overridden.
- Summary log: `tests\artifacts\ble_product_matrix\summary.log` unless overridden.
- Per-case artifacts: `tests\artifacts\ble_product_matrix\<case-id>\`.
- Validation summary for workflow review: `docs\validation\voice-keyboard-extreme-stability-hardening-1.1-tai1.md`.

## Invariants

- Existing A1/A2 targeted debugging remains available.
- `--cases extreme` is a named contract set and does not require editing the script by hand.
- The harness must not turn missing physical or external evidence into PASS.
- UX latency is measured from same-process product-chain timeline fields such as previous history completion to next capsule visibility; product-chain startup/package identity is recorded separately.
- Later refactors should compare against this baseline rather than redefining the scenarios opportunistically.
