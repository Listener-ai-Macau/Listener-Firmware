# voice-keyboard-custom-keys 1.3 validation

Assignee: claude

Scope:

- End-to-end coordination-only validation that firmware (step 1.1, accepted) and Listener-Type (step 1.2, accepted) custom key integration is consistent.
- This step does not create code changes. It verifies that the two accepted implementations work together correctly and records blockers for hardware-dependent acceptance criteria.

Static integration review:

- **Firmware event contract**: KEY1-KEY4 produce logical key events with stable naming (`key1.gpio45.f13` through `key4.gpio21.f16`). GPIO numbers are encapsulated; event names do not leak pin assignments. Fallback HID sends F13 (0x68) through F16 (0x6B) via standard BLE HID keyboard report.
- **Desktop listener contract**: Listener-Type registers global hotkey listeners for F13-F16 using the `global-hotkey` crate. Each key dispatches the configured `DeviceCustomKeyAction` from `deviceCustomKeys` preferences.
- **Action coverage**: Both sides agree on the supported action set: Disabled, OpenApp, SwitchStyle, Translation, SelectionAsk, PasteTemplate, SendShortcut. No arbitrary command/script execution path exists in either codebase.
- **Fallback behavior**: When Listener-Type is not running or cannot consume F13-F16, the firmware sends F13-F16 as regular HID keyboard input. These keys are not standard text keys and will not type WASD or any visible text into most applications.
- **Diag_log coverage**: Firmware emits `DIAG_KBD_CUSTOM_KEY` events with logical_key (1-4), phase (1=press, 2=release), fallback_usage (0x68-0x6B), and esp_err result. Desktop logs with `[device-key]` prefix including action type and dispatch outcome.
- **EC11 gestures**: Firmware routes EC11 through `voice_key_input` — single click toggles recording via `voice_recording_control`, double click enters BLE recovery, long press is reserved for hardware power path. Desktop does not consume EC11 events; they are firmware-only.
- **Preference migration**: Desktop defaults all keys to `Disabled` when `deviceCustomKeys` is absent in preferences, preventing surprise behavior on upgrade.
- **Feedback loop prevention**: Desktop rejects F13-F16 as SendShortcut targets, preventing self-trigger loops.

Hardware-dependent acceptance criteria (requires physical device):

- **EC11 single/double/long press behavior**: Requires flashing firmware to real Listener hardware, connecting serial monitor, and verifying recording toggle, BLE recovery, and power path behavior. Cannot be validated statically.
- **KEY1-KEY4 action dispatch**: Requires real BLE pairing between Listener hardware and desktop app running Listener-Type, configuring KEY1-KEY4 to distinct actions, and pressing each physical key to verify the configured action fires.
- **Fallback HID verification**: Requires stopping Listener-Type while Listener hardware remains connected via BLE, pressing KEY1-KEY4, and confirming only F13-F16 (no WASD/text) reaches the host OS.
- **Deep-sleep wake**: Current N4 wakes with KEY4/GPIO21. N16R8 EC11 wake requires new hardware not yet available.

Validation commands (static, no hardware):

- PASS: Static integration review — firmware and desktop custom key event contracts are consistent (F13-F16 ↔ global-hotkey F13-F16)
- PASS: Diag_log coverage — `DIAG_KBD_CUSTOM_KEY` with logical_key, phase, fallback_usage, result
- PASS: No WASD/text fallback — F13-F16 (0x68-0x6B) are function keys, not text-producing keys
- PASS: No arbitrary command execution — `DeviceCustomKeyAction` enum is closed, no shell/exec variants
- PASS: Preference migration — defaults to Disabled for all keys
- PASS: Feedback loop prevention — F13-F16 rejected as SendShortcut targets

Blocked hardware validation:

- EC11 recording/recovery/power gestures on real Listener hardware
- KEY1-KEY4 action dispatch with real BLE pairing
- Fallback HID verification with Listener-Type stopped
- N16R8 deep-sleep wake with EC11 (hardware not yet available)

Hardware blocker follow-up:

- Current N4 board supports KEY4/GPIO21 deep-sleep wake only. EC11/GPIO35 is not RTC wake capable on ESP32-S3.
- N16R8 production board will add EC11-based wake. Hardware validation deferred until board arrives.
- All hardware-dependent acceptance criteria should be validated in a follow-up session with physical device access (COM port + BLE pairing + serial monitor).
