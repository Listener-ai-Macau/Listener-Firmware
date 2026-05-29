# firmware-low-power-overnight-drain / 1.1 oai1 rework

Date: 2026-05-29
Agent: oai1
Branch: ai/oai1-firmware-low-power-overnight-drain-1.1
Firmware HEAD: cc5164c
Status: blocked on hardware gate, not review-ready

## Scope

This rework picks up reviewer feedback for step 1.1. The previous review rejected the step because the final review branch left hardware soak evidence as follow-up. The branch must prove the final code does not timer self-wake, wakes by KEY4/GPIO21 EXT1, and preserves HID, voice, BLE audio, and disconnect/reconnect behavior after sleep/wake.

## Code state

- Reapplied `c7ac4cc` on the oai1 step branch:
  - split `power_manager` idle clocks into `user_idle_ms` and `radio_idle_ms`;
  - overnight sleep uses `user_idle_ms`;
  - BLE connected/disconnected idle uses `radio_idle_ms`;
  - BLE connect/disconnect no longer calls `power_manager_record_activity()`;
  - `~POWER:STATUS` now prints `user_idle_ms` and `radio_idle_ms`.
- Reapplied `0b2b06f` on the oai1 step branch:
  - maps ESP-IDF wake causes into `power_manager_wake_source_t` before writing power wake diagnostics;
  - preserves EXT1/GPIO21 evidence instead of relying on raw ESP wake enum values.
- Current branch diff versus the prior codex rework branch is empty after these fixes.

## Automated validation

- PASS: `python tools\verify_power_manager_static.py`
- PASS: `pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1`
- PASS: `pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1`
- PASS: `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
- PASS: `python -m compileall -q tools`
- PASS: `git diff --check`
- PASS: `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
  - app version: `v1002.0.0-ota-test-20-gcc5164c`
  - app binary size: `0xa5ab0`
  - smallest app partition: `0x1b0000`
  - free app partition space: `0x10a550` (62%)

## Hardware gate

Not run in this oai1 pass. Workflow hardware locks show the required device is actively owned by `tai1`:

- `COM5` locked by `tai1`
- `14C19F48FE72` locked by `tai1`
- lock observed: 2026-05-29 10:18 local

Because the step acceptance explicitly requires final-branch hardware evidence, this branch must not be submitted for review yet.

## Required next run

When COM5 and BLE `14C19F48FE72` are free, rerun under a workflow hardware lock:

1. Flash this branch/build to COM5.
2. Capture clean boot and `~POWER:STATUS`.
3. Run a no-touch soak for at least 30 minutes with BLE host churn allowed.
4. Verify COM5 disappears for deep sleep and does not return by timer.
5. Press KEY4/GPIO21 and verify COM5 returns.
6. Dump persistent diag_log and prove wake source is EXT1/GPIO21, not timer.
7. Rerun or attach valid fresh artifacts for HID key path, voice recording start/stop/cancel, BLE audio session, and disconnect/reconnect recovery.

Only after those hardware results pass should this step be submitted for review.
