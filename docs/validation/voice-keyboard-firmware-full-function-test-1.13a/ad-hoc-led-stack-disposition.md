# Ad-hoc LED Stack Disposition

Date: 2026-06-19T15:27:51+08:00
Agent: oai1
Worktree: `C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware-wt-adhoc-oai1-led-flicker-guard-shutdown-confirm`
Scratch checkpoint before final staging: `human/scratch/listener-firmware/led-effect-1-13a-before-final-commit-20260619-152751` (`2ca77a4`)

## Audited Branches

| Branch | Disposition | Evidence |
|---|---|---|
| `adhoc/oai1/led-flicker-guard-shutdown-confirm` | Included as the active 1.13a normalization base. The branch is `ahead 3` from `origin/master` before the current working-tree edits. | `git log --oneline origin/master..HEAD` shows `8ee6812 Harden status LED tail and restore recording OK`, `29403fd Add flicker-safe LED zone brightness controls`, and `52c3041 Merge branch 'master' into adhoc/oai1/led-flicker-guard-shutdown-confirm`. The 2026-06-19 working-tree edits complete the product-effect pass on top of that stack. |
| `adhoc/oai1/status-led-recording-effect` | Included through ancestry; no separate cherry-pick remains. | `git merge-base --is-ancestor adhoc/oai1/status-led-recording-effect HEAD` returned success; `git log --oneline HEAD..adhoc/oai1/status-led-recording-effect` returned no commits. |
| `adhoc/oai1/led-true-state-accents` | Included through ancestry; no separate cherry-pick remains. | `git merge-base --is-ancestor adhoc/oai1/led-true-state-accents HEAD` returned success; `git log --oneline HEAD..adhoc/oai1/led-true-state-accents` returned no commits. |

## Current Scope

The active branch plus working-tree edits cover the LED-only 1.13a scope: status-tail anti-flicker guard, per-zone LED brightness caps, deterministic low-load REC/AI status behavior, slow EC11 and edge/frame dynamic primitives, BLE re-pair cue, EC11 press/rotate feedback, long-press shutdown confirmation, human review tooling, static checks, and status LED documentation.

Unrelated ad-hoc stacks remain excluded from 1.13a: power-hold shutdown failsafe, hardware-revision/current-sense changes, low-power battery sampling, BLE recovery capsule work, and Type desktop settings readback. Those require their own workflow disposition if they are still product intent.

## Validation Snapshot

- `python .\tools\verify_status_led_static.py`: PASS
- `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tools\verify_device_settings_static.ps1`: PASS
- `python -m compileall -q tools`: PASS
- `git diff --check`: PASS
- `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tools\build.ps1`: PASS, ESP32-S3 build completed in `C:\Users\Billy\AppData\Local\Temp\listener-idf-build-67018ce2a915`
- `pwsh -NoProfile -File C:\Users\Billy\Desktop\Denzic\ai-collaboration-workflow\scripts\aiw.ps1 doctor -RepoRoot .`: PASS for inspection; reports this ad-hoc stack as manual scratch risk and requires an explicit merge-through-workflow or keep-local disposition before release.

## Remaining Gate

The final visual outcome still needs the 1.13b hardware/human review after flashing this working tree or its workflow branch. Latest human feedback before this note asked for clockwise REC/AI flow, visible BLE re-pair EC11 confirmation, and EC11 rotation feedback that keeps all ring LEDs present instead of resetting to an off/dot pattern.
