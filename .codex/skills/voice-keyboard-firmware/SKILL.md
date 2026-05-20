---
name: voice-keyboard-firmware
description: Firmware repo adapter for ESP32-S3 BLE HID keyboard and BLE audio capture. Use for repository work, hardware validation, feature docs, and Listener firmware constraints.
---

# Voice Keyboard Firmware

## Start

1. Confirm identity from launcher/env (`AI_AGENT_ID`, `TAI_AGENT_ID`) and current branch.
2. Read shared workflow protocol:
   `C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\docs\ai_collaboration_protocol.md`
3. Read shared status when doing coordinated work:
   `C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\docs\plans\*_status.json`
4. If using existing capabilities, read `!docs/features/index.json` first, then only the needed feature card.

## Collaboration

Use the public workflow repo as the implementation source:

```powershell
$wf = "..\ai-collaboration-workflow\scripts"
```

Claim:

```powershell
pwsh -NoProfile -File "$wf\claim_step.ps1" -Plan <task_slug> -StepId <step_id> -Assignee <you> -RepoRoot . -BaseBranch master
```

Complete:

```powershell
pwsh -NoProfile -File "$wf\complete_step.ps1" -Plan <task_slug> -StepId <step_id> -ValidationResult "PASS: ..." -RepoRoot .
```

The repo-local collaboration scripts in `tools\` are compatibility proxies only. Do not edit their workflow logic here.

## Hardware

Real hardware commands require resource locks immediately before use:

```powershell
pwsh -NoProfile -File "$wf\lock_resource.ps1" -Resource COM3 -Owner <you>
pwsh -NoProfile -File "$wf\lock_resource.ps1" -Resource BLE -Owner <you>
pwsh -NoProfile -File "$wf\unlock_resource.ps1" -Resource COM3 -Owner <you>
pwsh -NoProfile -File "$wf\unlock_resource.ps1" -Resource BLE -Owner <you>
```

Pure docs, search, static checks, compile-only builds, and non-device tests do not need locks.

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

## Feature Docs

- Firmware completed features live in `!docs/features/`; start from `!docs/features/index.json`.
- A completed feature must have code evidence, primary paths, validation commands/results, invariants, and known limits.
- Do not put active plan status, partial work, long history, or execution logs into `features/`.
