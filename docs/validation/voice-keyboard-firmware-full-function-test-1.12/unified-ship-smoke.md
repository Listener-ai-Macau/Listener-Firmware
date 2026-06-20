# Voice Keyboard Firmware Full Function Test 1.12 Unified Ship Smoke

Status: partial AI evidence collected; final human go/no-go is still pending.

Date: 2026-06-20
Agent: oai1

## Commits

- Firmware product commit under test: `5584dc7e5b56863a0fcea2c2d930bb380ce85aca`.
- This matches the accepted 1.13c disposition source: firmware master only.
- Listener-Type commit used by the passing A1 smoke: `1096b98fe62515da777a8673f68bf1b316ded1a4`.

## Automated Evidence Collected

### Firmware diagnostics

- Command: `aiw with-lock -Resource COM10 -Run pwsh -NoProfile -File .\tools\collect_ai_diagnostics.ps1 -Port COM10 -RecentEventCount 200 -EnableSource keyboard,voice_key -Source keyboard,voice_key -OutputDir .\docs\validation\voice-keyboard-firmware-full-function-test-1.12\collect-ai-diagnostics`
- Artifact directory: `collect-ai-diagnostics/`
- Result: bounded serial diag collection completed on COM10.
- Manifest: `collect-ai-diagnostics/manifest.json`
- Notes: this is support/diag evidence only; it is not the final user-visible golden path.

### A1 generated KEY3 current-commit smoke

- Command source worktree: clean detached firmware worktree at `5584dc7e5b56863a0fcea2c2d930bb380ce85aca`.
- Command: `python .\tools\verify_audio_ble_product_matrix.py --port COM10 --cases A1 --a1-restart-round-count 1 --a1-continuous-round-count 0 --listener-type-repo C:\Users\Billy\Desktop\Denzic\Listener\Listener-Type --matrix-result-json docs\validation\voice-keyboard-firmware-full-function-test-1.12\a1-minimal-pass-matrix-result.json --summary-log docs\validation\voice-keyboard-firmware-full-function-test-1.12\a1-minimal-pass-summary.log --full-chain-timeout-seconds 120 --full-chain-listener-timeout-ms 90000`
- Lock resources: `COM10,BLE-listener`
- Result: PASS.
- Matrix result: `a1-minimal-pass-matrix-result.json`
- Summary log: `a1-minimal-pass-summary.log`
- Product-chain artifacts copied to: `a1-minimal-pass-product-chain/`

Observed pass details:

- Generated KEY3 start/stop acknowledged twice.
- Type capsule/product chain produced transcript `撤销操作。`.
- Insert status: `inserted`.
- Embedded BLE audio `missing_packets=0`.
- Firmware serial report: recording start/stop seen, stream ready true, transport_not_ready false.
- LED evidence after Type smoke parser fix: `led_recording_active_seen=true`, `led_recording_cleared_seen=true`, `led_ai_active_seen=true`.
- LED status sampled 20 `~LED:STATUS` lines, including one recording summary and one post-stop summary.

## Tooling Fix During 1.12

Initial A1 attempts proved the audio/Type path but failed LED evidence parsing:

- Before the fix, the Type smoke parser only checked `detail=summary` lines for REC state.
- Firmware 1.13b+ emits multi-line `~LED:STATUS` details; `detail=state rec_active=1` and `detail=rgb status_rgb=...REC:20,14,1` were present before the summary line entered the old sample window.
- Listener-Type commits `e6139124d1ded483adb9b4353a232946ea057182` and `1096b98fe62515da777a8673f68bf1b316ded1a4` fixed the parser to read all phase-scoped LED status lines and wait for the full summary sample.
- After the fix, A1 passed with the same firmware product commit `5584dc7e5b56863a0fcea2c2d930bb380ce85aca`.

## Still Required Before 1.12 Can Be Accepted

The following items are not proven by the minimal generated KEY3 A1 smoke:

- Clean Windows or non-development setup install/package path.
- Representative physical trigger: physical KEY3 or EC11 Shift+F13, not only generated `~KEY:KEY3:SINGLE`.
- 30 second real recording path with human-observed capsule timing.
- Human-visible OK light on successful capsule exit, and warning light for a failure/no-recognition path.
- Sampled reconnect/re-pair, battery/power, settings persistence, factory-state, and one failure UX case cited from accepted subgates or freshly observed if the operator wants a final spot check.
- Explicit product go/no-go decision and residual issue list.

## Suggested Human Gate Script

1. Use the current Listener-Type build at commit `1096b98fe62515da777a8673f68bf1b316ded1a4` and firmware package/flash based on `5584dc7e5b56863a0fcea2c2d930bb380ce85aca`.
2. Pair the device as a normal user, without serial commands or manual UUID entry.
3. Press physical KEY3 once to start a recording, speak for about 30 seconds, then press KEY3 again to stop.
4. Confirm capsule opens, records, processes, inserts text/history, and exits successfully.
5. Confirm REC lights during recording, AI lights during processing, OK lights only after a successful capsule exit, and WARN lights for a no-recognition/failure case.
6. Confirm there is no visible flicker regression or LED5/LED6 follow behavior.
7. Record PASS/FAIL and any residual issues before marking 1.12 accepted.
