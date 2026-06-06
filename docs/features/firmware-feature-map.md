# Firmware Feature Map

This document is the human-readable companion to `tools/ai/repo_features.ps1`.
The script remains the workflow entry point for AI context; this file explains the
same product capabilities with concrete source locations.

## Implemented Product Surface

| Feature | What exists | Main paths |
| --- | --- | --- |
| V2 board profile | Active firmware defaults to `ESP32-S3-WROOM-1-N16R8`, 16 MB flash, 8 MB Octal PSRAM, V2 pin map, OTA slots, and diag_log partition layout. | `ports/esp32/board_pins/Kconfig.projbuild`, `sdkconfig.defaults.esp32s3`, `partitions.csv`, `ports/esp32/board_pins/`, `tools/verify_v2_board_profile_static.ps1` |
| Custom BLE HID fallback keys | Logical custom keys emit stable `KEY1`/`KEY2`/`KEY3`/`KEY4` firmware events. Current V2 GPIO38/GPIO39/GPIO40/GPIO41 fall back to safe non-text BLE HID gestures: single-click F13-F16, double-click F17-F20, and long-press F21-F24 until Listener-Type consumes custom key actions. | `components/keyboard/`, `components/hid_keyboard/`, `ports/esp32/ble_hid*`, `ports/esp32/board_pins/` |
| Recording control key | EC11 push/GPIO11 controls recording: single click starts or stops after the double-click window, while USB `VREC:` commands remain available for desktop/product validation. | `components/voice_recording_control/`, `ports/esp32/voice_key_input/` |
| Recovery | EC11 push double-click recovery and serial `VREC:RECOVERY`, `VREC:RESET`, or `VREC:FORGET` clear pairing/session state. | `ports/esp32/voice_key_input/`, `components/voice_recording_control/`, `ports/esp32/ble_hid*` |
| BLE audio transport | VKA1-style 16 kHz microphone sessions are framed as start/audio/stop/cancel/error notifications for Listener-Type, with executable transport invariants for epoch filtering, replay, queue/pool backpressure, stale GATT events, and terminal ownership. | `ports/esp32/audio_capture*`, `ports/esp32/ble_audio_stream*`, `tools/verify_ble_audio_transport_model.py` |
| Diagnostics | `diag_log` records boot, BLE, audio, health, recording-key, self-test, error, and operator-enabled input debug events in flash-backed logs that survive reboot. The default-off `~DIAGLOG:INPUTDBG:ON/OFF/STATUS` mode is for hardware bring-up and can temporarily persist KEY1-KEY4 raw/stable transitions, EC11 encoder dispatch traces, and EC11 push raw/stable states. | `components/diag_log/`, `ports/esp32/diag_log_platform/`, `tools/dump_diag_log.ps1`, `components/keyboard/`, `ports/esp32/voice_key_input/` |
| System health | Runtime heartbeat and resource checks cover heap, BLE state, disconnect conditions, and task health evidence. | `components/system_health/`, `ports/esp32/system_health_platform/` |
| POST and degraded boot | Startup checks report NVS, heap, SPIRAM assumptions, BLE/audio pending state, and degraded boot status. | `main/`, `components/self_test/` |
| Battery and factory readiness | Battery ADC status, USB serial text commands, readiness flags, and capability strings are exposed for production bring-up. | `ports/esp32/ble_hid*`, `ports/esp32/board_pins/`, `tools/package_factory_firmware.ps1` |
| Low power | `power_manager` reports idle thresholds, blockers, sleep-only external-power blockers, V2 EC11/GPIO11 provisional deep-sleep wake policy, USB/charger raw and interpreted power status, battery entry/wake stats, and sleep-drain telemetry. | `components/power_manager/`, `docs/features/low_power_wake_policy.md`, `tools/verify_power_manager_static.py`, `tools/verify_charging_awake_policy_static.ps1` |

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
pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1
pwsh -NoProfile -File .\tools\verify_charging_awake_policy_static.ps1
python .\tools\verify_ble_audio_transport_model.py
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
