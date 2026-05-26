# Embedded Audio Capture Recovery Fix 1.1 Validation

Date: 2026-05-26

## Summary

The SPH0645 path now captures both I2S slots, extracts the configured active slot
(`left` for V1 defaults), removes slow DC offset, and applies digital gain before
BLE upload. The serial-toggle capture helper also waits for `stream_ready` and
retries once after a firmware `BLE audio transport not ready` rejection.

## Validation

- `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`: PASS.
  - App binary: `0x96d50`; smallest app partition free space: 83%.
- `python -m compileall -q tools\capture_audio_ble_wav.py`: PASS.
- `python tools\capture_audio_ble_wav.py --help`: PASS.
- `git diff --check`: PASS, with expected CRLF conversion warnings only.
- Flashed COM5 with the updated firmware before targeted capture checks.

## Hardware Evidence

Direct TTS BLE capture artifact:

`tests/artifacts/ble_product_matrix/capture_recovery_fix_direct_tts_left_gain_short_20260526-133318/capture_ble_latest_16k_mono.wav`

Measured PCM characteristics:

- Duration: 4.120 s.
- Min/max: -8596 / 9812.
- Mean: 36.0.
- RMS / centered RMS: 1542.9 / 1542.5.
- Centered active ratio above 500: 0.701.
- Transport summary: expected packets 275, received packets 275, queue_full 0.

This replaces the previous near-constant DC captures around mean -3365 with
centered RMS below 100.

## Follow-up Found During Validation

The product smoke wrapper previously reported PASS for a transcript mismatch
(`expected="测试语音输入。"` vs `transcript="哦，是的。"`). That was a
Listener-Type smoke-script false positive and is handled in the companion
Listener-Type script fix.
