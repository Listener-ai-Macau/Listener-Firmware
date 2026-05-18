---
name: voice-keyboard-firmware
description: Repository adapter for voice-keyboard-firmware. Use with ai-collaboration-workflow when work touches firmware structure, ESP32/S3 commands, COM3/BLE hardware resources, audio/BLE validation, or repo-specific planning and documentation rules.
---

# Voice Keyboard Firmware

Use `$ai-collaboration-workflow` for generic Codex/executor routing. This skill only adds repo-specific rules.

## Workflow

- Small local fixes use the fast path: inspect narrowly, edit, verify, report.
- New features, architecture, hardware, timing-sensitive bugs, protocol changes, or cross-module work need a plan in `!docs/plans/` or `!docs/fixes/` before implementation.
- Execute one planned step at a time. Run that step's acceptance check before moving on.
- Write human-facing docs in Chinese by default.

## Repo Facts

- Platform now: `ESP32-S3` with `ESP-IDF + CMake`.
- Future boundary: keep upper layers portable to `STM32`.
- Product line: `BLE HID keyboard + voice capture upload`.
- Audio path: `BLE session notify -> Windows host reassembly -> wav`.
- Keep `components/` and `protocols/` as free of direct `ESP-IDF` coupling as practical.

## Boundaries

- `main/`: thin `app_main()` and initialization handoff.
- `components/`: cross-platform product logic.
- `protocols/`: protocol structs, codecs, and error contracts.
- `ports/esp32/`: ESP-IDF bindings and SDK glue.
- Do not create generic top-level folders such as `services/`, `platform/`, `common`, or `misc` unless explicitly requested.

## Executor Adapter

- COM3, BLE, flash, monitor, and verify chains are exclusive/critical. Use native pipeline plus `-Resource COM3,BLE`.
- Build/size tasks that depend on ignored local state such as `build/` or `sdkconfig` should use `-NoWorktree`.
- BLE/audio/hardware/protocol/cross-module conclusions are high risk: check structured evidence such as `pipeline_steps.json`, `matrix_result.json`, `diagnosis.json`, `evidence.md`, or source `file:line`.
- BLE matrix warnings are not PASS. Gate runs should use `--fail-on-warning` or native pipeline `inconclusive_on_stdout_regex`.
- P5 host recovery semantics: host recovery completed -> settle window -> start capture.

## Commands

```powershell
# Build
idf.py build

# Flash
pwsh -File .\tools\flash.ps1 -Port COM3

# Serial capture
pwsh -File .\tools\capture_serial.ps1 -Port COM3 -ResetBeforeRead

# BLE audio capture
python .\tools\capture_audio_ble_wav.py --port COM3 --capture-seconds 5

# Product matrix gate
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --capture-seconds 5 --long-capture-seconds 30 --round-count 3 --idle-seconds 30 --soak-round-count 5 --fail-on-warning
```

## Key Constraints

- Do not reinterpret current `audio_data` back into the old `chunk + fragment` model.
- Do not change host primary subscription order: `CCCD notify -> ValueChanged`.
- Do not remove compatibility for `subscribe` arriving before `connect`.
- When product scope or backend/device assumptions matter, consult `!docs/product_solutions.md`.
