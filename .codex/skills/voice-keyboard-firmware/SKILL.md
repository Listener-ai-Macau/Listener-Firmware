---
name: voice-keyboard-firmware
description: Firmware repo adapter for ESP32-S3 BLE HID keyboard and BLE audio capture. Use for repository work, multi-AI collaboration, plan/fix status, hardware locks, feature docs, and firmware validation.
---

# Voice Keyboard Firmware

## Start

1. Confirm identity from launcher/env (`AI_AGENT_ID`, `TAI_AGENT_ID`) and current branch.
2. Read `!docs/ai_collaboration_protocol.md`.
3. Read shared status: `C:\Users\Billy\Desktop\listener\docs\plans\*_status.json`.
4. If using existing capabilities, read `!docs/features/index.json` first, then only the needed feature card.

## Hard Gates

- Before coding, confirm the step is not occupied, then claim it:
  `powershell -File C:\Users\Billy\Desktop\listener\voice-keyboard-firmware\tools\update_plan_status.ps1 -Plan <plan_id> -StepId <step_id> -Status in_progress -Assignee <you>`
- **Branch workflow**: `master` → `feature/<plan>` → `ai/<agent>-<step>`. Create your step branch FROM the feature branch, merge back after each step, delete step branch. Do not create worktrees.
- Real hardware commands require resource locks immediately before use. Pure docs, search, static checks, compileall, and non-device builds do not need locks.
- Lock each resource separately; `lock_resource.ps1` currently locks one `-Resource` per call:
  `powershell -File C:\Users\Billy\Desktop\listener\voice-keyboard-firmware\tools\lock_resource.ps1 -Resource COM3 -Owner <you>`
  `powershell -File C:\Users\Billy\Desktop\listener\voice-keyboard-firmware\tools\lock_resource.ps1 -Resource BLE -Owner <you>`
- Release every lock you acquired with `unlock_resource.ps1`.
- On completion, run the step validation, merge step branch to feature branch, record evidence in status JSON, mark `completed`, and clear assignee.
- If human action is required, mark `blocked` with `blocked_reason`.

## Plan Rules

- New feature, architecture, hardware, cross-module integration, and important bug work needs a plan in shared `plans/` or `fixes/`.
- Plans must include precedent review: official examples/APIs, platform built-ins, existing repo mechanisms, or mature community tools.
- Steps must include `repo`, `parallel_group`, `depends_on`, `write_paths`, `validation_commands`, and objective acceptance.
- Same-time work avoids overlapping `write_paths`; shared hardware serializes through resource locks.

## Feature Docs

- `features/` is per-repo only; shared `C:\Users\Billy\Desktop\listener\docs\` contains only `plans/` and `fixes/`.
- Firmware completed features live in `!docs/features/`; start from `!docs/features/index.json`.
- A completed feature must have code evidence, primary paths, validation commands/results, invariants, and known limits.
- Do not put PRDs, active status, partial work, long history, or execution logs into `features/`.
- After a plan/fix completes, condense durable facts into a short feature card and archive or clean the active plan/fix.

## Repo Constraints

- Top-level boundaries: `main/`, `components/`, `protocols/`, `drivers/`, `ports/esp32/`; do not add vague top-level `services/`, `platform/`, `common/`, or `misc/`.
- Names use `snake_case`; public functions use module prefixes.
- Key invariants:
  - Do not reinterpret `audio_data` as old `chunk + fragment`.
  - Keep host subscription order `CCCD notify -> ValueChanged`.
  - Keep subscribe-before-connect compatibility.

## Common Validation

```powershell
idf.py build
idf.py flash
python .\tools\capture_audio_ble_wav.py --port COM3 --capture-seconds 5
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --capture-seconds 5 --long-capture-seconds 30 --round-count 3 --fail-on-warning
```
