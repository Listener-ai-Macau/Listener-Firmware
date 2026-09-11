# Listener Firmware

This is the firmware that runs on the Listener voice keyboard. It handles audio capture, the buttons and lights, Bluetooth pairing, power management and OTA updates.

Everything that turns speech into text — recognition, rewriting, typing into the focused field — happens on the computer, in [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type). Without that app the keyboard is just a well-behaved BLE peripheral; without the keyboard, the app works with any microphone.

[中文](README.zh.md) · [繁體中文](README.zh-TW.md) · [1.0.5 release notes](docs/release/1.0.5.md)

### Hardware

- ESP32-S3 module (ESP32-S3-WROOM-1-N16R8: 16 MB flash, 8 MB PSRAM)
- PDM digital microphone, captured at 16 kHz
- EC11 rotary encoder with push button
- Four keys (KEY1–KEY4) and six status LEDs
- Li-ion battery, USB-C charging

### Controls

The defaults in 1.0.5:

- Clicking the knob starts and stops recording. Double-clicking resets Bluetooth pairing; long-pressing powers the device off; turning it adjusts the PC volume. All of this can be remapped in the app.
- KEY1–KEY4 take custom single-, double- and long-press actions (Settings → Device in Type). Keys you haven't configured fall back to inert HID keys, so they can't type anything by accident.
- With "start on voice" enabled in Type, the keyboard waits for a wake phrase (default: 「开始录音」) and starts recording when it hears it.

### Lights

PWR is power and battery. BLE goes steady blue once the desktop app is ready. REC means the device is actually capturing audio. AI means audio is transferring or the host is processing. OK confirms success (or an upgrade in progress); WARN is an error that needs attention. When everything is dark, the device is usually asleep to save power — not broken.

### Pairing, recovery, updates

Pairing is done from the app: click "Start pairing", choose `listener` in Windows Bluetooth, then "Check connection". If pairing gets wedged, Settings → About → Device recovery sorts it out — no serial cable involved.

Updates arrive as OTA from the app. There are two firmware slots with automatic rollback, and pairing and device settings survive the update. USB flashing from this repo works too. The current release is 1.0.5 ([notes](docs/release/1.0.5.md)); its known bug is a wrong battery reading right after power-off.

### Building and flashing

The scripts are Windows-only and install ESP-IDF `release/v5.5` for you:

```powershell
pwsh -NoProfile -File .\tools\setup_windows.ps1
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
```

Don't run bare `idf.py` — go through `tools\idf.ps1`, which loads the IDF environment first.

### Reading the code

`components/` holds the product logic and is kept platform-independent; ESP-IDF specifics are quarantined in `ports/esp32/`; `protocols/` defines the codecs and the audio protocol; `tools/` has the build, flash and monitor scripts. The GPIO map and the validation tooling are documented in [docs/features/firmware-feature-map.md](docs/features/firmware-feature-map.md).

Contributions: [CONTRIBUTING.md](CONTRIBUTING.md). Security reports: [SECURITY.md](SECURITY.md).
