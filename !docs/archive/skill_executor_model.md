---
name: voice-keyboard-firmware
description: Repository adapter for voice-keyboard-firmware. Use with ai-collaboration-workflow when work touches firmware structure, ESP32/S3 commands, COM3/BLE hardware resources, audio/BLE validation, or repo-specific planning and documentation rules.
---

# Voice Keyboard Firmware

Use `$ai-collaboration-workflow` for generic Codex / dual-agent / legacy-executor / pipeline routing. This skill is the voice-keyboard-firmware adapter: it only adds repo-specific rules, commands, resources, and structured result metadata.

## Workflow

- Small Codex-direct fixes use the fast path: inspect narrowly, edit, verify, report.
- Small docs/grep/schema checks stay Codex-direct or native pipeline; do not send them to executor.
- New features, architecture, hardware, timing-sensitive bugs, protocol changes, or cross-module work need a plan in `!docs/plans/` or `!docs/fixes/` before implementation.
- For medium/high-risk firmware work, Codex owns the hard/core path: architecture, BLE/audio/protocol semantics, hardware gates, risk calls, integration, and final validation.
- Use dual-agent work only when the split is clean. Claude should take low-coupling docs, focused surveys, fixtures, small host-side scripts, or tests in a separate worktree; Codex should take firmware/protocol/port integration.
- Legacy executor is not the default reviewer. Keep it for long-log compression, focused failure diagnosis, old evidence workflows, and native pipeline artifacts.
- Execute one planned step at a time. Stop after each major hardware gate or matrix tier and report the structured result before running the next tier, unless the user has explicitly asked for a continuous full gate.
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
- Do not run flash, monitor, BLE capture, or matrix scripts directly from Codex shell when they are part of a gate. Put them in native pipeline with resource locks.
- Native pipeline commands should use `command` + `args` arrays for scripts and tools; reserve shell command strings for short PowerShell snippets.
- Build/size tasks that depend on ignored local state such as `build/` or `sdkconfig` should use `-NoWorktree`.
- BLE/audio/hardware/protocol/cross-module conclusions are high risk: check structured evidence such as `pipeline_steps.json`, `pipeline_policy.json`, `diagnosis.json`, `evidence.md`, or source `file:line`.
- BLE matrix warnings are not PASS. Gate runs should use `--fail-on-warning` and declare `structured_results` rules for `matrix_warning > 0`.
- Build/flash/verify chains must preserve source-firmware consistency. Declare firmware outputs in `artifact_files`, then compare native pipeline source `head`/`diff_hash` and artifact size/sha fields before trusting a hardware result.
- P5 host recovery semantics: host recovery completed -> settle window -> start capture.
- If source `diff_hash` changes between build, flash, and verify steps, treat the hardware result as INCONCLUSIVE until the chain is rerun from build.
- If a pipeline references a matrix result but no structured result artifact copy exists under `pipeline_step_*`, treat the result as INCONCLUSIVE.

## Dual-Agent Boundaries

- Use separate worktrees for Codex and Claude. Do not let both agents edit the same active checkout.
- Codex should own `main/`, `components/`, `protocols/`, `ports/esp32/`, BLE/audio invariants, hardware gate definitions, and final merge decisions unless explicitly reassigned.
- Claude may own Chinese docs, acceptance notes, focused grep/survey artifacts, host-side helper scripts, fixtures, or narrow tests when those files are assigned up front.
- Claude should not change `audio_data` transport semantics, host subscription ordering, `subscribe` compatibility, COM3/BLE resource locking, matrix warning policy, or source-firmware consistency rules unless the task explicitly says so.
- If both agents need the same document, split by section or have Claude return notes for Codex to integrate instead of editing the file concurrently.

## Pipeline Metadata

Use this metadata on build/flash/verify steps that need source-firmware consistency:

```json
"artifact_files": [
  { "label": "app", "path": "build/voice-keyboard-firmware.bin", "required": true },
  { "label": "elf", "path": "build/voice-keyboard-firmware.elf" },
  { "label": "bootloader", "path": "build/bootloader/bootloader.bin" },
  { "label": "partition_table", "path": "build/partition_table/partition-table.bin" }
]
```

Use this metadata on BLE matrix steps:

```json
"structured_results": [
  {
    "name": "ble_matrix",
    "stdout_regex": "^\\s*matrix_result_json=(.+?)\\s*$",
    "summary_fields": ["status", "matrix_total", "matrix_failed", "matrix_warning", "failed_cases", "warning_cases", "summary_log"],
    "copy_path_fields": ["summary_log"],
    "rules": [
      { "field": "status", "equals": "FAIL", "status": "FAIL", "reason": "BLE matrix status=FAIL" },
      { "field": "matrix_warning", "greater_than": 0, "status": "INCONCLUSIVE", "reason": "BLE matrix has warnings" }
    ]
  }
]
```

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
