---
name: voice-keyboard-firmware
description: Apply the naming, structure, and local development conventions for the `voice-keyboard-firmware` repository. Use when working in this repository after the company-wide embedded workflow has already been selected, especially for repo-specific directory rules, naming, platform boundaries, tool commands, ESP32 bring-up details, and current project facts.
---

# Voice Keyboard Firmware

## Fast-Path Brief

This skill is the fast recovery entrypoint for `voice-keyboard-firmware`.

Repository-specific facts live directly in this `SKILL.md`; do not create or depend on a separate `references/` folder for the fast-path brief.

## Role

This is a repository child skill, not the company workflow skill.

Use the company workflow skill for:

- plan-first execution
- step gating
- approval checkpoints
- completion-summary rules

Use this repository skill for:

- local structure and naming
- local platform boundaries
- local commands and tooling
- current board and bring-up facts
- repository-specific reading order

## Operating Model

AI should default to doing hands-on work when the environment allows it:

- Run setup, build, flash, non-interactive log capture, serial input injection, result analysis, and iterative fixes directly when possible.
- Prefer command-line verification before asking the human to perform a manual step.
- Treat `idf.py monitor` as optional human convenience; prefer this repository's non-interactive verification scripts.
- If execution is blocked by missing hardware, missing ports, missing permissions, or missing system devices, state the exact blocker.
- Ask the human to act only when the next step requires a real physical or OS-mediated action Codex cannot complete, such as pressing `KEY1`, USB replug, BOOT/RESET button presses, BLE pairing on another machine, or checking an external device response.

The human mainly owns requirements, architecture tradeoffs, hardware bring-up coordination, critical issue judgment, and final acceptance.

## Current Project Facts

- Formal repo name: `voice-keyboard-firmware`
- Current firmware platform: `ESP32-S3`
- Future platform boundary: keep upper layers portable to `STM32`
- Build system: `ESP-IDF + CMake`
- Current product line: `BLE HID keyboard + voice capture upload`
- Current audio upload path: `BLE session notify -> Windows host reassembly -> wav`
- Current upper-layer constraint: keep `components/` and `protocols/` as free of direct `ESP-IDF` coupling as practical.

## Repository Defaults

- Treat this `SKILL.md` as the quickest recovery path after context loss.
- Follow these repository rules unless the user explicitly asks to override them.
- On a new Windows machine or a machine missing `ESP-IDF`, run `tools/setup_windows.ps1` before build or flash work.
- Treat `idf.py monitor` as an optional human convenience; prefer this repository's non-interactive verification scripts when possible.
- Keep edits aligned with the repository's ESP32-now, STM32-later boundary.
- When the task affects product scope or backend/device assumptions, consult `!docs/product_solutions.md`.
- When the repository has approved plans under `!docs/plans/`, answer "what next" by referencing the current approved plan step.
- For simple low-risk bug fixes, use the embedded workflow `fast` path by default: fix directly, verify narrowly, and skip new `!docs/fixes/` documents unless the investigation becomes ambiguous or risky.

## Directory Boundaries

- `main/`: only thin `app_main()` and initialization handoff.
- `components/`: cross-platform product logic.
- `protocols/`: protocols, messages, structs, codecs, and error contracts.
- `drivers/`: semantic peripheral drivers.
- `ports/esp32/`: `ESP-IDF` bindings and SDK glue.
- `ports/stm32/`: future platform binding placeholder.

Do not create generic top-level folders such as `services/`, `platform/`, `common/`, or `misc/` unless the user explicitly asks.

## Naming Rules

- Repository names use `product-name-artifact`.
- Company-owned directories, files, functions, and variables use `snake_case`.
- Public functions need a module prefix, such as `keyboard_start()`, `ble_hid_init()`, or `audio_capture_start()`.
- Keep platform names like `esp32` and `stm32` inside platform-layer files and folders when possible.

## Current Reading Order

Use the smallest set needed for the task. Preferred order:

1. `CLAUDE.md`
2. `README.md`
3. `!docs/README.md`
4. `!docs/plans/voice_shortcut_keyboard_plan.md` for v1 product boundary or next-step questions
5. `!docs/features/audio_capture_ble_upload.md` for voice upload work
6. `!docs/features/voice_input_backend_contract.md` for backend integration boundary
7. `!docs/product_solutions.md` for product scope, BLE/Wi-Fi choices, or device-to-backend assumptions
8. Source files directly when changing implementation details

For old seed-project keyboard paths, useful implementation files include:

- `CMakeLists.txt`
- `main/main.c`
- `components/keyboard/keyboard.c`
- `components/hid_keyboard/hid_keyboard.c`
- `components/board/board.c`
- `ports/esp32/ble_hid/ble_hid.c`
- `ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c`
- `tools/build.ps1`

## Common Commands

```powershell
# New Windows machine / missing ESP-IDF
powershell -ExecutionPolicy Bypass -File .\tools\setup_windows.ps1

# Build
idf.py build

# Flash
powershell -ExecutionPolicy Bypass -File .\tools\flash.ps1 -Port COM3

# Non-interactive serial capture
powershell -ExecutionPolicy Bypass -File .\tools\capture_serial.ps1 -Port COM3 -ResetBeforeRead

# Send serial input without monitor
powershell -ExecutionPolicy Bypass -File .\tools\send_serial.ps1 -Port COM3 -Text "abc123"

# BLE audio capture
python .\tools\capture_audio_ble_wav.py --port COM3 --capture-seconds 5

# Physical KEY1 audio capture
python .\tools\capture_audio_ble_wav.py --port COM3 --capture-seconds 5 --trigger-mode physical-key --no-reset-before-capture

# Product-surface BLE audio matrix
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --capture-seconds 5 --long-capture-seconds 30 --round-count 3 --idle-seconds 30 --soak-round-count 5

# More realistic continuous-use matrix
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --realistic-usage-profile --random-seed 20260525

# P1 standard BLE audio regression
python .\tools\verify_audio_ble_upload_end_to_end.py --port COM3 --capture-seconds 5 --no-reset-before-capture
```

## Current Key Constraints

- Do not reinterpret current `audio_data` back into the old `chunk + fragment` model.
- Do not change the host primary subscription order: `CCCD notify -> ValueChanged`.
- Do not remove the device compatibility for `subscribe` arriving before `connect`.
- Keep docs in Chinese by default.
- If there is an active related plan or fix under `!docs/plans/` or `!docs/fixes/`, follow it before inventing an ad-hoc next step. Unrelated simple local bug fixes may still use the fast path.

## Planning And Cleanup Rules

- Feature and implementation plans live in `!docs/plans/`.
- Current bug fix and investigation docs live in `!docs/fixes/`.
- Simple low-risk bug fixes do not need a new `!docs/fixes/` document by default; direct fix plus focused verification is enough.
- Durable completion summaries belong in `!docs/features/` or another relevant `!docs/` file.
- Prefer keeping `!docs/plans/` clean: templates plus current valid plans.
- Prefer keeping `!docs/fixes/` clean: only current bug fix or investigation docs.
- After a plan is fully completed and summarized, move durable knowledge into `!docs/features/` and delete the completed plan unless there is an explicit reason to keep it.

## Deliverables

When finishing a task under this skill:

- State which repository rule mattered most.
- Mention any doc file that was updated.
- Call out if the change improves or harms future ESP32/STM32 portability.

