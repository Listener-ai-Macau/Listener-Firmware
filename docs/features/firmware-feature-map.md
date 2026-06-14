# Firmware Feature Map

This document is the human-readable companion to `tools/ai/repo_features.ps1`.
The script remains the workflow entry point for AI context; this file explains the
same product capabilities with concrete source locations.

## Implemented Product Surface

| Feature | What exists | Main paths |
| --- | --- | --- |
| V2 board profile | Active firmware defaults to `ESP32-S3-WROOM-1-N16R8`, 16 MB flash, 8 MB Octal PSRAM, V2 pin map, OTA slots, and diag_log partition layout. | `ports/esp32/board_pins/Kconfig.projbuild`, `sdkconfig.defaults.esp32s3`, `partitions.csv`, `ports/esp32/board_pins/`, `tools/verify_v2_board_profile_static.ps1` |
| Custom BLE HID fallback keys | Logical custom keys emit stable `KEY1`/`KEY2`/`KEY3`/`KEY4` firmware events. Current V2 GPIO38/GPIO39/GPIO40/GPIO41 fall back to safe non-text BLE HID gestures: single-click F13-F16, double-click F17-F20, and long-press F21-F24 until Listener-Type consumes custom key actions. F13-F24 is the supported bare function-key fallback range; do not allocate F25 or above for product controls because host HID/hotkey handling beyond F24 is not part of the contract. Recording is a configurable Listener-Type custom-key action; `~KEY:KEY3:SINGLE` is a diagnostic-only generated press/release that feeds the same custom-key timing path for automated A1/A2 evidence. | `components/keyboard/`, `components/hid_keyboard/`, `ports/esp32/ble_hid*`, `ports/esp32/board_pins/` |
| EC11 push key | EC11 push/GPIO18 no longer controls recording. The same physical switch is the power-on key while off; after boot, single click sends the safe EC11 custom-key fallback `Shift+F13` after the double-click window. This modifier combination is the reserved EC11 runtime custom-key entry and intentionally avoids both the KEY1-KEY4 bare F13-F24 slots and unsupported F25+ keys. `~KEY:EC11:SINGLE` is a diagnostic-only generated press/release for that runtime EC11 custom-key path. | `ports/esp32/voice_key_input/`, `components/hid_keyboard/`, `ports/esp32/ble_hid*` |
| Recovery | EC11 push double-click recovery and serial `VREC:RECOVERY`, `VREC:RESET`, or `VREC:FORGET` clear pairing/session state. USB `VREC:` commands remain available only for low-level recovery/diagnostics and are not A1/A2 acceptance evidence. | `ports/esp32/voice_key_input/`, `components/voice_recording_control/`, `ports/esp32/ble_hid*` |
| BLE audio transport | VKA1-style 16 kHz microphone sessions are framed as start/audio/stop/cancel/error notifications for Listener-Type. The active N16R8 validation build captures SPH0655 PDM audio through ESP-IDF PDM2PCM, with executable transport invariants for epoch filtering, replay, queue/pool backpressure, stale GATT events, and terminal ownership. | `ports/esp32/audio_capture*`, `ports/esp32/ble_audio_stream*`, `tools/verify_ble_audio_transport_model.py` |
| Diagnostics | `diag_log` records boot, BLE HID/GAP, OTA, health, power, board, status LED, self-test, WARN, and ERROR events by default in flash-backed logs that survive reboot. High-rate INFO sources are runtime-controlled with `~DIAGLOG:ENABLE <source>` / `~DIAGLOG:DISABLE <source>`. The default-off `~DIAGLOG:INPUTDBG:ON/OFF/STATUS` mode is for hardware bring-up and can temporarily persist KEY1-KEY4 raw/stable transitions, EC11 encoder dispatch traces, and EC11 push raw/stable states. AI bundles include `summary.param_highlights` for first-pass triage while retaining raw event refs. | `components/diag_log/`, `ports/esp32/diag_log_platform/`, `tools/dump_diag_log.ps1`, `tools/collect_ai_diagnostics.ps1`, `components/keyboard/`, `ports/esp32/voice_key_input/` |
| System health | Runtime heartbeat and resource checks cover heap, BLE state, disconnect conditions, and task health evidence. | `components/system_health/`, `ports/esp32/system_health_platform/` |
| POST and degraded boot | Startup checks report NVS, heap, SPIRAM assumptions, BLE/audio pending state, and degraded boot status. | `main/`, `components/self_test/` |
| Battery and factory readiness | Battery ADC status uses the protected product range `3000mV=0%` and `4200mV=100%`, with `2700mV` reserved as an absolute danger marker. BLE HID Battery Service reports the live ADC-derived level, periodically refreshes the host value, and clamps to `100%` only when USB power is present and the charger full pin is asserted. USB serial text commands, readiness flags, capability strings, and a verifiable factory firmware package are exposed for production bring-up. | `components/battery_monitor/`, `ports/esp32/ble_hid*`, `ports/esp32/board_pins/`, `tools/package_factory_firmware.ps1`, `tools/verify_factory_firmware_package.ps1`, `tools/verify_ble_battery_service_static.ps1`, `docs/features/factory_firmware_readiness.md` |
| Device settings | `device_settings` persists Type-facing board settings: plugged brightness, battery brightness, low-power idle timeout, battery-only auto-shutdown timeout, and BLE name. Fresh-device defaults are plugged brightness 80%, battery brightness 50%, low-power idle 1 minute, auto-shutdown 30 minutes, and BLE name `listener`; user/NVS settings override those defaults. `~DEVICE:SETTINGS` reports the effective settings and `~DEVICE:SET ...` updates them with validation. The legacy `~LED:BRIGHTNESS` command remains compatible by writing both brightness profiles. BLE name changes are persisted and marked pending until the next safe advertising/reconnect/reboot path. | `components/device_settings/`, `protocols/listener_device/`, `components/status_led/`, `components/power_manager/`, `tools/verify_device_settings_static.ps1` |
| Low power | `power_manager` preserves connected/disconnected idle power reduction through the device-settings low-power timeout, treats USB/VBUS as the automatic long-idle external-power blocker, lets the idle microphone/I2S path sleep even while plugged after the audio idle threshold, turns routine status LEDs off in connected/disconnected idle, keeps BLE disconnect/reconnect churn from reopening a high-power ACTIVE window once user idle has elapsed, uses the device-settings battery-only shutdown timeout, persists the final shutdown trace before PWR_HOLD/GPIO11 drive-high, actively drives board-level `PWR_HOLD/GPIO11` LOW during boot/runtime, guards it back to LOW outside hardware shutdown, and drives it HIGH only for hardware shutdown. USB/VBUS blocks long-idle automatic shutdown; USB/VBUS, charging, or charge-full status blocks critical-low-battery automatic shutdown. `~POWER:STATUS` reports shutdown blockers, `audio_idle_power_save`, audio-idle blockers, effective `low_power_idle_ms` and `hardware_shutdown_ms`, last shutdown persistence/battery/power flags, reset/cold-boot-oriented diagnostics, PWR_HOLD state, and `DIAG_POWER_USB_DETECT` / `DIAG_POWER_CHARGE_STATE` / `DIAG_POWER_HOLD_STATE` transitions; real power-off proof belongs to the hardware validation gate. | `components/power_manager/`, `components/device_settings/`, `docs/features/low_power_wake_policy.md`, `tools/with_auto_shutdown_guard.ps1`, `tools/verify_power_manager_static.py`, `tools/verify_charging_awake_policy_static.ps1`, `tools/verify_device_settings_static.ps1` |

## Diagnostic Source Controls

`~DIAGLOG:SOURCES` reports every source with `info_enabled`, `info_default`, and the active mask. `~DIAGLOG:ENABLE <source>` and `~DIAGLOG:DISABLE <source>` toggle INFO retention without reflashing; WARN and ERROR events always pass the mask. `~DIAGLOG:LAST:N` emits a bounded recent tail, and `~DIAGLOG:LAST:N:<source>` emits a bounded recent tail for one source.

Default INFO-on sources are `system`, `self_test`, `ble_hid`, `ble_gap`, `ota`, `health`, `power`, `board`, and `status_led`. Default INFO-off high-rate sources are `keyboard`, `voice_key`, `audio`, `voice_rec`, and `ble_audio`.

Normal AI diagnostic collection uses `tools/collect_ai_diagnostics.ps1` or bounded `tools/dump_diag_log.ps1 -Count N`. Full retained export is still available only as the explicit `tools/dump_diag_log.ps1 -Full` path for exceptional manual use.

`diag_log_ai_bundle.json` includes `summary.param_highlights` so AI triage can inspect key BLE GAP connection/subscription params, BLE audio notify/backpressure/replay state, audio or voice recording reject reasons, power shutdown/external-power state, board rail/profile values, and keyboard or voice-key input counts without reading the whole event list. Highlights are enough for first-pass triage when the needed section is present and each finding can cite its `event_ref`. Use raw `events[*]` when checking complete timelines, fields outside the highlighted sections, unknown schema behavior, or missing expected signals. A1/A2 product acceptance is a button/capsule/session timing gate and must include generated KEY3 custom-key recording evidence; ASR accuracy is recorded there but gated separately by A3.

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
pwsh -NoProfile -File .\tools\read_key_status.ps1 -OutputPath .\tests\artifacts\key_status\key-status-static.md -OutputJson .\tests\artifacts\key_status\key-status-static.json
pwsh -NoProfile -File .\tools\verify_v2_board_profile_static.ps1
pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1
pwsh -NoProfile -File .\tools\verify_device_settings_static.ps1
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
