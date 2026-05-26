# firmware-diagnostic-log-coverage/1.1 validation

Date: 2026-05-26
Assignee: oai
Worktree: `voice-keyboard-firmware-wt-oai-firmware-diagnostic-log-coverage-1.1`

## Commands

- `pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1`
  - PASS: diagnostic log coverage checks passed.
- `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
  - PASS: generated `build/voice-keyboard-firmware.bin`.
  - Partition table evidence from build: `otadata=0xf000`, `ota_0=0x20000`, `ota_1=0x1d0000`.
  - App size: `0x9c9e0`; smallest app partition: `0x1b0000`; free: `0x113620`.
- `pwsh -NoProfile -File .\tools\package_factory_firmware.ps1 -OutputRoot .\.cache\factory_firmware_validation`
  - PASS: generated factory package manifest.
  - Manifest flash command uses app offset `0x20000`.
  - Manifest partition evidence includes `otadata`, `ota_0`, and `ota_1`.
- `python -m compileall -q tools`
  - PASS.
- `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
  - PASS: firmware repo feature script is present, concise, and covers ESP32-S3 BLE HID/audio diagnostics.
- `git diff --check`
  - PASS.

## Coverage Notes

- Flash/package tooling now derives the app write offset from the generated ESP-IDF partition table instead of hardcoding `0x10000`.
- OTA runtime logs partition role, subtype, offset, and size for running/boot/update/next partition context.
- BLE audio logs notify state transitions, deferred subscribe state, notify-disabled aborts, and session abort epoch.
- `DIAG_SRC_POWER` is reserved at `0x0C` so it no longer collides with `DIAG_SRC_OTA=0x0B`.
- Current master does not contain the full low-power `power_manager` runtime. This branch adds boot wake/status diag events on master and keeps power event IDs compatible with the existing low-power branch, where sleep entry/reject runtime diagnostics are implemented.
