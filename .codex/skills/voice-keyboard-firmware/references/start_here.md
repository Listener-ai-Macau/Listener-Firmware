# Start Here

This file is the fastest way for AI to recover context in `voice-keyboard-firmware`.

## 1. Workflow Layering

- Company-wide process belongs in the parent skill `embedded-ai-workflow`.
- Repository-specific facts belong here.
- When both apply, follow the company workflow first and this repository brief second.

## 2. Default Responsibilities

### AI should default to doing the work

- Default to running build, flash, monitor, log reading, result analysis, and iterative fixes when the environment allows it.
- Default to running setup, build, flash, log capture, serial input injection, result analysis, and iterative fixes without waiting for user approval.
- Prefer command-line verification first.
- Do not push executable steps back to the user when AI can perform them directly.
- Treat `monitor` as optional. Prefer non-interactive verification paths that Codex can run itself.
- If execution is blocked by missing hardware, missing ports, missing permissions, or missing system devices, state the exact blocker clearly.
- Only ask the human to act when the next step requires a real physical or OS-mediated action that Codex cannot complete itself, such as USB replug, BOOT or RESET button presses, BLE pairing on another machine, or checking an external device response.

### Human should default to making decisions

- The human mainly owns requirements, architecture tradeoffs, hardware bring-up coordination, critical issue judgment, and final acceptance.
- Ask the human to step in only when a decision is needed or the environment blocks execution.

One-line operating model:

`AI should do as much hands-on execution as possible; the human should mainly provide decisions and final judgment.`

## 3. What This Repo Is

- Formal repo name: `voice-keyboard-firmware`
- Current bring-up target: `ESP32-S3`
- Build system: `ESP-IDF + CMake`
- Current goal: verify a BLE HID keyboard seed project first
- Future boundary: keep the upper layers portable to `STM32`

One-line summary:

`Treat this as a formal firmware repo with ESP32 as the current implementation and STM32 portability as a design constraint.`

## 4. Top-Level Rules

- Keep `main/` thin. It should only host `app_main()` and hand off to upper layers.
- Put cross-platform product code in `components/`.
- Put protocols, codecs, message structs, and error codes in `protocols/`.
- Put semantic peripheral drivers in `drivers/`.
- Put `ESP-IDF` bindings in `ports/esp32/`.
- Put future `STM32` bindings in `ports/stm32/`.
- Do not create new top-level folders like `services/`, `platform/`, `common/`, or `misc/` unless the user explicitly asks.

## 5. Naming Rules

- Repository names: `product-name-artifact`
- Company-owned directories, files, functions, and variables: `snake_case`
- Public functions need a module prefix, such as:
  - `keyboard_start()`
  - `ble_hid_init()`
- `audio_capture_start()`
- Keep platform names like `esp32` and `stm32` inside platform-layer files and folders when possible.

## 6. Platform Boundary Rules

- Upper layers should avoid direct `ESP-IDF` APIs when practical.
- `ESP-IDF` details belong in `ports/esp32/`.
- Typical `ports/esp32/` contents:
  - `esp_hidd`
  - `nvs_flash`
  - NimBLE / GAP
  - other SDK glue
- If the repo later moves to `STM32`, prefer replacing:
  - `ports/stm32/*`
  - some `drivers/*` if needed
- Try not to change:
  - `components/*`
  - `protocols/*`

## 7. Current Reading Order

Read in this order when modifying the seed project:

1. `CMakeLists.txt`
2. `main/main.c`
3. `components/keyboard/keyboard.c`
4. `components/hid_keyboard/hid_keyboard.c`
5. `components/board/board.c`
6. `ports/esp32/ble_hid/ble_hid.c`
7. `ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c`
8. `tools/build.ps1`

## 8. Current Build Workflow

- Windows default `ESP-IDF` path: `%USERPROFILE%\esp\esp-idf`
- New Windows machine bootstrap: `powershell -ExecutionPolicy Bypass -File .\tools\setup_windows.ps1`
- Build: `idf.py build`
- Flash: `idf.py flash`
- Monitor: `idf.py monitor`
- `idf.py monitor` requires an interactive TTY. In a non-interactive Codex session, use `powershell -ExecutionPolicy Bypass -File .\tools\capture_serial.ps1 -Port COM5 -ResetBeforeRead` for boot logs.
- If the firmware test path consumes console input, use `powershell -ExecutionPolicy Bypass -File .\tools\send_serial.ps1 -Port COM5 -Text "abc123"` instead of asking a human to type into monitor.
- Prefer `powershell -ExecutionPolicy Bypass -File .\tools\verify_ble_hid.ps1 -Port COM5 -Text "abc123"` for a single non-interactive runtime check that resets the board, captures boot logs, injects test bytes, and summarizes the result.

If the environment allows command execution, AI should prefer doing these steps itself instead of pushing them back to the user.

## 9. Plan And Next-Step Rule

- Repository feature / implementation plans live in `!docs/plans/`.
- Current bug fix / bug investigation docs live in `!docs/fixes/`.
- Important completion summaries should live in `!docs/features/` or another relevant `!docs/` file.
- When the user asks what to do next, prefer the current approved plan step over an ad-hoc task list.
- Prefer keeping `!docs/plans/` clean: templates plus current valid feature / implementation plans.
- Prefer keeping `!docs/fixes/` clean: only current bug fix / bug investigation docs.
- After a plan is fully completed and summarized, move the durable knowledge into `!docs/features/` and delete the completed plan unless there is an explicit reason to keep it.

## 10. When To Read More

- Read `!docs/product_solutions.md` only when the task affects product scope, BLE vs Wi-Fi choices, or device-to-backend assumptions.
- Read source files directly when changing implementation details.

## 11. Current Minimal Facts

- The repo currently builds successfully from the command line.
- It is still a seed project, not a production-complete firmware.
- Real key scanning and full audio path are not implemented yet.

One-line operating rule:

`Keep the project buildable first, keep the platform boundary clean second, and only then optimize structure.`
