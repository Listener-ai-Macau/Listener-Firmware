---
name: voice-keyboard-firmware
description: Apply the naming, structure, and local development conventions for the `voice-keyboard-firmware` repository. Use when working in this repository after the company-wide embedded workflow has already been selected, especially for repo-specific directory rules, naming, platform boundaries, tool commands, ESP32 bring-up details, and current project facts.
---

# Voice Keyboard Firmware

## Start Here

Read `references/start_here.md` first.

That file is the fast-path brief for this repository. It contains:

- the repository-specific facts that sit on top of the company workflow
- new machine bootstrap commands
- current product and platform facts
- required directory and naming rules
- current code reading order
- build, flash, and monitor commands
- when to consult `!docs/product_solutions.md`

Only open more files when the task needs them.

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

## Repository Defaults

- Treat `references/start_here.md` as the quickest recovery path after context loss.
- Follow its repository rules unless the user explicitly asks to override them.
- On a new Windows machine or a machine missing `ESP-IDF`, run `tools/setup_windows.ps1` before build or flash work.
- Treat `idf.py monitor` as an optional human convenience, not the primary verification path for Codex.
- Ask the user to step in only for real physical or OS-level actions that Codex cannot replace, such as USB replug, BOOT or RESET button presses, BLE pairing UI on another host, or observing behavior outside this machine.
- Keep edits aligned with the repository's ESP32-now, STM32-later boundary.
- When the task affects product scope or backend/device assumptions, consult `!docs/product_solutions.md`.
- When the repository has approved plans under `!docs/plans/`, answer "what next" by referencing the current approved plan step.

## Deliverables

When finishing a task under this skill:

- State which repository rule mattered most.
- Mention any doc file that was updated.
- Call out if the change improves or harms future ESP32/STM32 portability.

