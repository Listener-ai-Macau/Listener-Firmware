# Firmware Feature Map

This document is the human-readable companion to `tools/ai/repo_features.ps1`.
The script remains the workflow entry point for AI context; this file explains the
same product capabilities with concrete source locations.

## Implemented Product Surface

| Feature | What exists | Main paths |
| --- | --- | --- |
| V2 board profile | Active firmware defaults to `ESP32-S3-WROOM-1-N16R8`, 16 MB flash, 8 MB Octal PSRAM, V2 pin map, OTA slots, and diag_log partition layout. | `ports/esp32/board_pins/Kconfig.projbuild`, `sdkconfig.defaults.esp32s3`, `partitions.csv`, `ports/esp32/board_pins/`, `tools/verify_v2_board_profile_static.ps1` |
| Custom BLE HID fallback keys | Logical custom keys emit stable `KEY1`/`KEY2`/`KEY3`/`KEY4` firmware events. Current V2 GPIO38/GPIO39/GPIO40/GPIO41 fall back to safe non-text BLE HID gestures: single-click F13-F16, double-click F17-F20, and long-press F21-F24 until Listener-Type consumes custom key actions. | `components/keyboard/`, `components/hid_keyboard/`, `ports/esp32/ble_hid*`, `ports/esp32/board_pins/` |
| Recording control key | EC11 push/GPIO18 controls recording: single click starts or stops after the double-click window, while USB `VREC:` commands remain available for desktop/product validation. | `components/voice_recording_control/`, `ports/esp32/voice_key_input/` |
| Recovery | EC11 push double-click recovery and serial `VREC:RECOVERY`, `VREC:RESET`, or `VREC:FORGET` clear pairing/session state. | `ports/esp32/voice_key_input/`, `components/voice_recording_control/`, `ports/esp32/ble_hid*` |
| BLE audio transport | VKA1-style 16 kHz microphone sessions are framed as start/audio/stop/cancel/error notifications for Listener-Type. The active N16R8 validation build captures SPH0655 PDM audio through ESP-IDF PDM2PCM, with executable transport invariants for epoch filtering, replay, queue/pool backpressure, stale GATT events, and terminal ownership. | `ports/esp32/audio_capture*`, `ports/esp32/ble_audio_stream*`, `tools/verify_ble_audio_transport_model.py` |
| Diagnostics | `diag_log` records boot, BLE HID/GAP, OTA, health, power, board, status LED, self-test, WARN, and ERROR events by default in flash-backed logs that survive reboot. High-rate INFO sources are runtime-controlled with `~DIAGLOG:ENABLE <source>` / `~DIAGLOG:DISABLE <source>`. The default-off `~DIAGLOG:INPUTDBG:ON/OFF/STATUS` mode is for hardware bring-up and can temporarily persist KEY1-KEY4 raw/stable transitions, EC11 encoder dispatch traces, and EC11 push raw/stable states. AI bundles include `summary.param_highlights` for first-pass triage while retaining raw event refs. | `components/diag_log/`, `ports/esp32/diag_log_platform/`, `tools/dump_diag_log.ps1`, `tools/collect_ai_diagnostics.ps1`, `components/keyboard/`, `ports/esp32/voice_key_input/` |
| System health | Runtime heartbeat and resource checks cover heap, BLE state, disconnect conditions, and task health evidence. | `components/system_health/`, `ports/esp32/system_health_platform/` |
| POST and degraded boot | Startup checks report NVS, heap, SPIRAM assumptions, BLE/audio pending state, and degraded boot status. | `main/`, `components/self_test/` |
| Battery and factory readiness | Battery ADC status uses the protected product range `3000mV=0%` and `4200mV=100%`, with `2700mV` reserved as an absolute danger marker. USB serial text commands, readiness flags, capability strings, and a verifiable factory firmware package are exposed for production bring-up. | `components/battery_monitor/`, `ports/esp32/ble_hid*`, `ports/esp32/board_pins/`, `tools/package_factory_firmware.ps1`, `tools/verify_factory_firmware_package.ps1`, `!docs/features/factory_firmware_readiness.md` |
| Low power | `power_manager` preserves battery-only connected/disconnected idle power reduction, treats USB/VBUS as the automatic-shutdown external-power blocker, and keeps board-level `PWR_HOLD/GPIO11` LOW during boot/runtime, releasing it HIGH for long-idle hardware shutdown. This 2026-06-09 hardware-observed low-active contract supersedes the earlier active-high document wording. `~POWER:STATUS` reports shutdown blockers, reset/cold-boot-oriented diagnostics, PWR_HOLD state, and `DIAG_POWER_USB_DETECT` / `DIAG_POWER_CHARGE_STATE` / `DIAG_POWER_HOLD_STATE` transitions; real power-off proof belongs to the hardware validation gate. | `components/power_manager/`, `docs/features/low_power_wake_policy.md`, `tools/verify_power_manager_static.py`, `tools/verify_charging_awake_policy_static.ps1` |

## Diagnostic Source Controls

`~DIAGLOG:SOURCES` reports every source with `info_enabled`, `info_default`, and the active mask. `~DIAGLOG:ENABLE <source>` and `~DIAGLOG:DISABLE <source>` toggle INFO retention without reflashing; WARN and ERROR events always pass the mask. `~DIAGLOG:LAST:N` emits a bounded recent tail, and `~DIAGLOG:LAST:N:<source>` emits a bounded recent tail for one source.

Default INFO-on sources are `system`, `self_test`, `ble_hid`, `ble_gap`, `ota`, `health`, `power`, `board`, and `status_led`. Default INFO-off high-rate sources are `keyboard`, `voice_key`, `audio`, `voice_rec`, and `ble_audio`.

Normal AI diagnostic collection uses `tools/collect_ai_diagnostics.ps1` or bounded `tools/dump_diag_log.ps1 -Count N`. Full retained export is still available only as the explicit `tools/dump_diag_log.ps1 -Full` path for exceptional manual use.

`diag_log_ai_bundle.json` includes `summary.param_highlights` so AI triage can inspect key BLE GAP connection/subscription params, BLE audio notify/backpressure/replay state, audio or voice recording reject reasons, power shutdown/external-power state, board rail/profile values, and keyboard or voice-key input counts without reading the whole event list. Highlights are enough for first-pass triage when the needed section is present and each finding can cite its `event_ref`. Use raw `events[*]` when checking complete timelines, fields outside the highlighted sections, unknown schema behavior, or missing expected signals. A1/A2 product acceptance still needs the hardware matrix evidence for real BLE audio, recording, button feel, LED, power, and host behavior.

## Validation Entrypoints

Use hardware locks before commands that touch a real device, serial port, BLE, or flash.

```powershell
pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check
python -m compileall -q tools
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port <COMx>
pwsh -NoProfile -File .\tools\monitor.ps1 -Port <COMx>
pwsh -NoProfile -File .\tools\dump_diag_log.ps1 -Port <COMx> -Count 200
pwsh -NoProfile -File .\tools\collect_ai_diagnostics.ps1 -Port <COMx> -RecentEventCount 200 -EnableSource keyboard,voice_key -Source keyboard,voice_key -OutputDir .\tests\artifacts\ai_diagnostics
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
