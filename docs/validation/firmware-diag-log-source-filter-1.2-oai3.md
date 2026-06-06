# firmware-diag-log-source-filter/1.2 validation

Date: 2026-06-07
Agent: oai3
Worktree: `C:\Users\Billy\Desktop\listener\voice-keyboard-firmware-wt-oai3-firmware-diag-log-source-filter-1.2`
Branch: `ai/oai3-firmware-diag-log-source-filter-1.2`

## Result

PASS.

The first hardware smoke exposed a real bounded export defect: `~DIAGLOG:LAST:200:KEYBOARD` held the diag log mutex long enough to trigger Task WDT. The fix changes flash-ring export to use short per-sector snapshots, feeds/yields during long scans, and keeps `LAST:N[:source]` to a single reverse scan before chronological output.

## Commands

- `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3 -BuildDir .\build` PASS
- `git diff --check` PASS
- `pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1` PASS
- `pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1` PASS
- `pwsh -NoProfile -File .\tools\verify_v2_board_profile_static.ps1` PASS
- `python -m compileall -q tools` PASS
- `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check` PASS
- `pwsh -NoProfile -File C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COMx -Run pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx` PASS, `COMx` resolved to `COM6`
- `pwsh -NoProfile -File C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COMx -Run pwsh -NoProfile -Command "& { .\tools\collect_ai_diagnostics.ps1 -Port COMx -RecentEventCount 200 -ReadSeconds 8 -OutputDir '.\tests\artifacts\diag_log_source_filter' -EnableSource @('KEYBOARD','VOICE_KEY') -Source @('KEYBOARD','VOICE_KEY') }"` PASS, `COMx` resolved to `COM6`

## Hardware Evidence

Artifact directory: `tests/artifacts/diag_log_source_filter`

Manifest:

- `source_mode`: `serial`
- `source`: `COM6`
- `bounded_export`: `true`
- `command_path`: `~DIAGLOG:SOURCES + ~DIAGLOG:ENABLE/DISABLE + ~DIAGLOG:LAST:N[:source]`
- `recent_event_count`: `200`
- `source_filters`: `KEYBOARD`, `VOICE_KEY`
- `temporary_enabled_sources`: `KEYBOARD`, `VOICE_KEY`
- `event_count`: `209`
- `warning_error_count`: `0`
- `boot_segment_count`: `7`

Source transcript:

- Initial defaults: `keyboard`, `voice_key`, `audio`, `voice_rec`, and `ble_audio` had `info_enabled=0`; `system`, `ble_hid`, `ble_gap`, `self_test`, `health`, `ota`, `power`, `board`, and `status_led` had `info_enabled=1`.
- `~DIAGLOG:ENABLE KEYBOARD` changed `keyboard` to `info_enabled=1`.
- `~DIAGLOG:ENABLE VOICE_KEY` changed `voice_key` to `info_enabled=1`.
- `~DIAGLOG:LAST:200:KEYBOARD` completed with `dumped 200 of 2856 matching events`.
- `~DIAGLOG:LAST:200:VOICE_KEY` completed with `dumped 9 of 9 matching events`.
- `~DIAGLOG:DISABLE KEYBOARD` and `~DIAGLOG:DISABLE VOICE_KEY` completed.
- Final `~DIAGLOG:SOURCES` showed `keyboard info_enabled=0` and `voice_key info_enabled=0`.

No-WDT check:

- `Select-String` over `serial_transcript.txt` for `Task WDT`, `task_wdt:`, `watchdog got triggered`, `RTC_SW_CPU_RST`, `panic`, and `Aborting.` returned no matches.

Physical key presses were not required. Existing retained input events were sufficient to verify source-filtered bounded export on a non-empty ring.
