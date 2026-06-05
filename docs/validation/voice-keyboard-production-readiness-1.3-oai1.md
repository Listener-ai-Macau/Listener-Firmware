# voice-keyboard-production-readiness/1.3 oai1 rework validation

Date: 2026-06-05

Agent: oai1

Firmware worktree: `C:\Users\Billy\Desktop\listener\voice-keyboard-firmware-wt-oai1-voice-keyboard-production-readiness-1.3`

Branch: `ai/oai1-voice-keyboard-production-readiness-1.3`

Accepted base used for this rework submit: `origin/master` at `0946dd9814812ba33eeed526401a3b91e8d7e443`

## Result

PASS.

This rework rebuilt step 1.3 from the accepted firmware base. The branch was first reset to the then-current `origin/master`, then rebased when `origin/master` advanced to `0946dd9` during the rework. After the rebase, `git diff --name-status origin/master..HEAD` contains only this validation document and `docs/validation/voice-keyboard-production-readiness-1.3-validation-evidence.json`, so the previously reviewed branch-base pollution is not part of this submission.

The branch does not change firmware source, matrix tooling, or product docs. It records clean validation evidence for the already-accepted base behavior required by step 1.3.

## Acceptance Evidence

Step 1.3 acceptance:

- 60 second BLE recording has no `SessionError` or QueueFull.
- A2 long recording passes.

Observed evidence:

- `with-lock` acquired `COM5` and `BLE-14C19F48FE72`.
- A2 product-chain matrix ran with `--long-capture-seconds 60`, `--full-chain-audio-profile fast`, `--full-chain-long-sentence-count 18`, and `--fail-on-warning`.
- Matrix summary: `matrix_total=1`, `matrix_failed=0`, `matrix_warning=0`, `matrix_skipped=0`.
- A2 fast profile: `full_chain_status=PASS`, `accuracy=0.994169`, `cer=0.005831`, `missing_packets=0`, `pcm_bytes=1989760`.
- A2 timing: `product_chain_random_sentence_count=18`, `record_playback_actual_ms=61195`, `wav_duration_ms=61114`, `recording_window_ms=61914`, `partial_preview_count=185`, `asr_text_update_count=186`.
- Summary log scan: PASS, no `SessionError` or `QueueFull` found.

The plan text still names `COM3`; current available hardware for this station is `COM5` with BLE address `14C19F48FE72`, so validation used that hardware under the required workflow lock.

## Commands

```powershell
git fetch origin master
git reset --hard origin/master
git diff --name-status origin/master..HEAD
```

```powershell
python -m compileall -q tools
```

```powershell
pwsh -NoProfile -File .\tools\verify_ble_audio_backpressure_static.ps1
```

Output:

```text
PASS: BLE audio backpressure static checks passed.
```

```powershell
pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check
```

Output:

```text
PASS: firmware repo feature script is present, concise, and covers ESP32-S3 BLE HID/audio diagnostics.
```

```powershell
python .\tools\verify_audio_ble_product_matrix.py --list-cases
```

Output included A2 in `implemented_cases` and `auto_cases`.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\tools\build.ps1
```

Output:

```text
Project build complete.
voice-keyboard-firmware.bin binary size 0xbabf0 bytes. Smallest app partition is 0x1b0000 bytes. 0xf5410 bytes (57%) free.
```

```powershell
pwsh -NoProfile -File ..\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COM5,BLE-14C19F48FE72 -TimeoutMinutes 45 -Run pwsh -NoProfile -Command "python .\tools\verify_audio_ble_product_matrix.py --port COM5 --cases A2 --capture-seconds 5 --long-capture-seconds 60 --full-chain-audio-profile fast --full-chain-long-sentence-count 18 --fail-on-warning --bluetooth-address 14C19F48FE72 --matrix-result-json tests\artifacts\ble_product_matrix\voice-keyboard-production-readiness-1.3-oai1-a2-60s-result.json --summary-log tests\artifacts\ble_product_matrix\voice-keyboard-production-readiness-1.3-oai1-a2-60s-summary.log"
```

Output:

```text
Locked 'BLE-14C19F48FE72' for 'oai1' (45 min)
Locked 'COM5' for 'oai1' (45 min)
product_chain_audio_profile=fast
product_chain_random_sentence_count=18
product_chain_full_chain_status=PASS
product_chain_accuracy=0.994169
product_chain_cer=0.005831
product_chain_missing_packets=0
product_chain_pcm_bytes=1989760
matrix_total=1
matrix_failed=0
matrix_warning=0
matrix_skipped=0
Released 'COM5' (was locked by 'oai1')
Released 'BLE-14C19F48FE72' (was locked by 'oai1')
```

```powershell
if (Select-String -LiteralPath .\tests\artifacts\ble_product_matrix\voice-keyboard-production-readiness-1.3-oai1-a2-60s-summary.log -Pattern 'SessionError|QueueFull' -Quiet) { 'FAIL: SessionError/QueueFull found' } else { 'PASS: no SessionError or QueueFull found in A2 summary log' }
```

Output:

```text
PASS: no SessionError or QueueFull found in A2 summary log
```

## Artifacts

- `tests\artifacts\ble_product_matrix\voice-keyboard-production-readiness-1.3-oai1-a2-60s-result.json`
  - SHA256: `70E48525E8A98F89606A221A63C3F36B6B0E40760B13732E8C36AD3C37E02D4D`
- `tests\artifacts\ble_product_matrix\voice-keyboard-production-readiness-1.3-oai1-a2-60s-summary.log`
  - SHA256: `463C16D1DF15512B0D7FF6785F3C3AEE87EC1BC87639B85E46ED4EC53D9B5226`
