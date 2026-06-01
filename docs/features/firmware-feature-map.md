# Firmware Feature Map

This document is the human-readable companion to `tools/ai/repo_features.ps1`.
The script remains the workflow entry point for AI context; this file explains the
same product capabilities with concrete source locations.

## Implemented Product Surface

| Feature | What exists | Main paths |
| --- | --- | --- |
| N4 board profile | Active firmware defaults to `ESP32-S3-WROOM-1-N4`, 4 MB flash, no PSRAM, N4 pin map, OTA slots, and diag_log partition layout. | `ports/esp32/board_pins/Kconfig.projbuild`, `sdkconfig.defaults.esp32s3`, `partitions.csv`, `ports/esp32/board_pins/`, `tools/verify_v2_board_profile_static.ps1` |
| Custom BLE HID fallback keys | Logical custom keys emit stable `KEY1`/`KEY2`/`KEY3` firmware events. Current N4 GPIO48/GPIO47/GPIO21 fall back to safe non-text BLE HID usages F13/F14/F15 until Listener-Type consumes custom key actions. | `components/keyboard/`, `components/hid_keyboard/`, `ports/esp32/ble_hid*`, `ports/esp32/board_pins/` |
| Voice recording key | `VOICE` toggles recording start/stop and exposes serial `VREC:` commands for desktop/product validation. On current N4 hardware, VOICE is wired to GPIO45 through the legacy KEY1 pin define. | `components/voice_recording_control/`, `ports/esp32/voice_key_input/` |
| Recovery | Long-hold voice key recovery and serial `VREC:RECOVERY`, `VREC:RESET`, or `VREC:FORGET` clear pairing/session state. | `ports/esp32/voice_key_input/`, `components/voice_recording_control/`, `ports/esp32/ble_hid*` |
| BLE audio transport | VKA1-style 16 kHz microphone sessions are framed as start/audio/stop/cancel/error notifications for Listener-Type. | `ports/esp32/audio_capture*`, `ports/esp32/ble_audio_stream*` |
| Diagnostics | `diag_log` records boot, BLE, audio, health, voice key, self-test, and error events in flash-backed logs that survive reboot. | `components/diag_log/`, `ports/esp32/diag_log_platform/`, `tools/dump_diag_log.ps1` |
| System health | Runtime heartbeat and resource checks cover heap, BLE state, disconnect conditions, and task health evidence. | `components/system_health/`, `ports/esp32/system_health_platform/` |
| POST and degraded boot | Startup checks report NVS, heap, SPIRAM assumptions, BLE/audio pending state, and degraded boot status. | `main/`, `components/self_test/` |
| Battery and factory readiness | Battery ADC status, USB serial text commands, readiness flags, and capability strings are exposed for production bring-up. | `ports/esp32/ble_hid*`, `ports/esp32/board_pins/`, `tools/package_factory_firmware.ps1` |
| Low power | `power_manager` reports idle thresholds, blockers, KEY3/GPIO21 deep-sleep wake policy, VOICE/GPIO45 wake limitation, battery entry/wake stats, and sleep-drain telemetry. | `components/power_manager/`, `docs/features/low_power_wake_policy.md`, `tools/verify_power_manager_static.py` |

## Validation Entrypoints

Use hardware locks before commands that touch a real device, serial port, BLE, or flash.

```powershell
pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check
python -m compileall -q tools
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port <COMx>
pwsh -NoProfile -File .\tools\monitor.ps1 -Port <COMx>
pwsh -NoProfile -File .\tools\dump_diag_log.ps1 -Port <COMx>
pwsh -NoProfile -File .\tools\verify_v2_board_profile_static.ps1
pwsh -NoProfile -File .\tools\verify_ble_hid.ps1
pwsh -NoProfile -File .\tools\verify_ble_hid_end_to_end.ps1
pwsh -NoProfile -File .\tools\verify_physical_custom_key_hid.ps1
pwsh -NoProfile -File .\tools\verify_audio_ble_product_matrix.ps1
git diff --check
```

## Boundaries

- Desktop ASR, text polish, insertion, settings, and user-facing BLE status live in `Listener-Type`.
- Industrial design, CAD, enclosure, renders, and manufacturing mechanical constraints live in `voice-keyboard-design`.
- Workflow plans, claims, review state, and cross-repo orchestration live in `ai-collaboration-workflow`.
