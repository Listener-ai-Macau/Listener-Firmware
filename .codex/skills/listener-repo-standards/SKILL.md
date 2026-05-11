---
name: listener-repo-standards
description: Apply this repository's naming, structure, and workflow conventions. Use when creating or renaming repositories, modules, files, directories, build scripts, or documentation in this codebase; when deciding where code should live across components, protocols, drivers, and ports; when reviewing changes for repo-level consistency; or when planning ESP32 and STM32 firmware work that must follow the repository's embedded standards.
---

# Listener Repo Standards

## Start Here

Read `references/start_here.md` first.

That file is the fast-path brief for this repository. It contains:

- current product and platform facts
- required directory and naming rules
- current code reading order
- build, flash, and monitor commands
- when to consult `docs/product_solutions.md`

Only open more files when the task needs them.

## Default Behavior

- Treat `references/start_here.md` as the quickest recovery path after context loss.
- Follow its repository rules unless the user explicitly asks to override them.
- Keep edits aligned with the repository's ESP32-now, STM32-later boundary.
- When the task affects product scope or backend/device assumptions, consult `docs/product_solutions.md`.

## Deliverables

When finishing a task under this skill:

- State which repository rule mattered most.
- Mention any doc file that was updated.
- Call out if the change improves or harms future ESP32/STM32 portability.

