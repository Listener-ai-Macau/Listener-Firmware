# voice-keyboard-low-power-wake-policy / 1.1 validation

Date: 2026-05-27
Agent: Tai
Repo: voice-keyboard-firmware
Branch: ai/tai-voice-keyboard-low-power-wake-policy-1.1

## Automated checks

- PASS: `pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1`
- PASS: `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
- PASS: `python -m compileall -q tools`
- PASS: `git diff --check`
- PASS: `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
- PASS: `pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1`
- PASS: `pwsh -NoProfile -File ..\ai-collaboration-workflow\scripts\aiw.ps1 validate -Plan voice-keyboard-low-power-wake-policy`

Build output:

- `build\voice-keyboard-firmware.bin`
- App binary size: `0xa31a0`
- Smallest app partition: `0x1b0000`
- Free app partition space: `0x10ce60` (62%)

## Acceptance notes

- Firmware now centralizes the V1 wake policy in `components/power_manager`: `wake_policy=key4_only`, deep-sleep wake mask is KEY4/GPIO21, and GPIO35 voice key is reported as not deep-sleep wake capable.
- `~POWER:STATUS` reports wake policy, wake-capable keys, voice-key GPIO/capability, voice-key deep-sleep wake state, user wake action, thresholds, blockers, battery, and last sleep/wake state.
- Sleep entry and sleep rejection paths log `DIAG_POWER_WAKE_POLICY` next to existing power events so exported diag logs can distinguish current-board wake limitations from firmware failures.
- Defaults preserve the low-power behavior from the preceding power-manager work: audio idle power save after 5 seconds, connected/disconnected idle after 30 seconds, and overnight deep sleep after 15 minutes when blockers are clear.

## Hardware note

Hardware serial reads were not run in this step. Plan step 1.3 owns locked COM/BLE validation for live `~POWER:STATUS`, `~DIAGLOG:LAST`, KEY4 wake, and post-wake BLE/audio/HID regression.
