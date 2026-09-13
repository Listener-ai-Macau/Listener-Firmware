# Listener Firmware

This is the firmware that runs on the Listener voice keyboard. It captures the microphone, sends audio to Listener Type over Bluetooth, and keeps the knob, keys, lights, battery, and power behavior working as one device.

[简体中文](README.zh.md) · [繁體中文](README.zh-TW.md) · [Firmware releases](https://github.com/Listener-ai-Macau/Listener-Firmware/releases) · [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type)

<p align="center">
  <img src="docs/assets/readme/keyboard-front.jpg" alt="Listener voice keyboard" width="900" />
</p>

## How it fits into Listener

The keyboard and desktop app are two halves of the same product. Firmware captures and transports audio; [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type) recognizes the speech, prepares the text, and inserts it into the app at the cursor.

That boundary is deliberate. Firmware owns device timing, transport, controls, and recovery. The desktop app owns wake acceptance, optional voiceprint checks, automatic ending, transcript choice, writing style, and final output.

## What runs on the keyboard

- **Voice capture and BLE audio.** PDM audio is recorded at 16 kHz, framed, queued, and sent with session identity, retry/backpressure handling, retained-packet replay, and trailing-audio protection.
- **Knob and keys.** The EC11 knob supports click, double-click, long-press, and rotation. KEY1–KEY4 support configurable single-, double-, and long-press actions, with safe BLE HID fallbacks when Type is unavailable.
- **Useful feedback.** Six light zones show power, Bluetooth, recording, processing, success, warnings, charging, and firmware update progress.
- **Battery and power.** The device reports battery level over BLE, handles charging and low-battery protection, suspends idle audio work, wakes from physical controls, and supports configurable sleep and shutdown timers.
- **Settings and recovery.** The BLE name, light brightness, knob action, and power timers persist across normal restarts and OTA updates. Pairing reset and wired recovery remain available when a normal reconnect is not enough.
- **Diagnostics.** A flash-backed event log survives reboot and can be exported over BLE or serial. Factory and engineering tools cover board bring-up, audio, BLE, controls, power, OTA, and diagnostic bundles.

The [firmware feature map](docs/features/firmware-feature-map.md) lists the detailed implementation and validation paths.

## Everyday use

1. Turn the keyboard on and pair it from Listener Type under Settings → Device.
2. Click the knob to start dictating and click again to stop. Listener Type can also end the session after you finish speaking.
3. Watch REC while the microphone is active, AI while the desktop app is processing, and OK when the result is ready.

Rotate the knob for system volume or screen brightness. Double-click it to clear the Bluetooth bond and reopen pairing; long-press it to power off. KEY1–KEY4 actions are configured in Listener Type.

All lights going dark usually means the keyboard has entered low-power idle. The next supported control press wakes it.

<p align="center">
  <img src="docs/assets/readme/device-settings.png" alt="Listener device settings" width="720" />
</p>

## Hardware

The current V2 profile targets an ESP32-S3-WROOM-1-N16R8 with 16 MB flash and 8 MB Octal PSRAM. It uses a PDM microphone, an EC11 rotary encoder with push switch, four additional keys, six status-light zones, a Li-ion battery, USB-C charging, and a hardware power-hold circuit.

BLE services cover audio transport, HID, device settings, diagnostics, battery reporting, and OTA. Speech recognition does not run on the ESP32-S3.

## Updates and recovery

Normal users install a release OTA ZIP from Listener Type. The firmware uses two application slots, validates the package, reports progress, and can roll back an image that never reaches confirmed healthy boot. A normal update preserves pairing and device settings.

USB flashing, full erase, serial maintenance, and factory packages are intended for development, production, or recovery. The exact release files and SHA-256 checksums are kept on the [Releases page](https://github.com/Listener-ai-Macau/Listener-Firmware/releases).

## Build and flash

The Windows scripts set up the expected ESP-IDF 5.5 environment and keep routine commands consistent:

```powershell
pwsh -NoProfile -File .\tools\setup_windows.ps1
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
pwsh -NoProfile -File .\tools\monitor.ps1 -Port COMx
```

Use `tools\idf.ps1` for other ESP-IDF commands.

Most reusable product logic is under `components/`; ESP32-specific bindings live under `ports/esp32/`; shared device messages are in `protocols/`; startup wiring is in `main/`; build, flash, packaging, diagnostics, and validation commands live in `tools/`.

Read [CONTRIBUTING.md](CONTRIBUTING.md) before changing firmware, [SUPPORT.md](SUPPORT.md) before reporting a device problem, and [SECURITY.md](SECURITY.md) for private security reports.

This repository does not yet include a `LICENSE`, so viewing the source does not grant permission to redistribute or modify it.
