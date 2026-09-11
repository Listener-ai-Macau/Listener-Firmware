# Listener Firmware

Firmware for the Listener voice keyboard. The keyboard is a small ESP32-S3 desk device (16 MB flash, 8 MB PSRAM) with a PDM microphone (16 kHz capture), a clickable EC11 knob, four keys and six status LEDs, powered by a Li-ion battery with USB-C charging. The firmware handles audio capture, buttons, lights, Bluetooth pairing, power saving and OTA updates; turning speech into text is [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type)'s job on the computer.

[中文](README.zh.md) · [繁體中文](README.zh-TW.md) · [1.0.5 release notes](docs/release/1.0.5.md)

## From power-on to the first words

1. Charge the keyboard, click the knob to power on.
2. Open Listener Type on the computer, click "Start pairing", pick `listener` in Windows Bluetooth, come back and click "Check connection".
3. Click into Notepad, click the knob, speak, click again.

REC lit means it's really capturing; getting the words to the cursor is Type's job. The full handbook lives in the Type repo: [voice keyboard guide](https://github.com/Listener-ai-Macau/Listener-Type/blob/master/docs/quickstart/voice-keyboard-readme.md).

## What your hand does

- Click the knob to start / stop, long-press to power off, turn for PC volume — all remappable in Type.
- Pairing messed up? Double-click the knob: Bluetooth resets and the keyboard becomes discoverable again.
- KEY1–KEY4 take single-, double- and long-press actions (Settings → Device in Type). Keys you haven't configured fall back to inert HID keys and can't type anything by accident.
- Don't want to reach for it: turn on "start on voice" in Type, and the device waits for a wake phrase (default: 「开始录音」) before recording.

The lights are status, not decoration: PWR is power and battery, BLE steady blue means the desktop app is ready, REC lit means audio is actually being captured, AI lit means transferring or processing, OK is success (or an upgrade in progress), WARN is an error that needs attention. All dark usually means it's asleep saving power, not broken. LED brightness and the low-power timers are adjustable in Type (Settings → Device), and the battery level is reported over the standard Bluetooth Battery Service, so Windows shows it directly.

## Updating

OTA from Listener Type is the way: two firmware slots, automatic rollback on failure, and pairing and device settings survive. To flash your own build, use this repo's scripts over USB. Current release is 1.0.5, with one known issue: the battery reading right after power-off can be wrong.

## Flashing your own build

Three commands on Windows; the scripts install ESP-IDF `release/v5.5` for you:

```powershell
pwsh -NoProfile -File .\tools\setup_windows.ps1
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
```

Don't run bare `idf.py` — go through `tools\idf.ps1`, which loads the IDF environment first.

## Reading the code

`components/` holds the product logic, deliberately kept platform-independent; ESP-IDF bindings are quarantined in `ports/esp32/`; `protocols/` defines the codecs and the audio protocol; `tools/` has the build, flash and monitor scripts. The GPIO map and validation tooling are documented in [docs/features/firmware-feature-map.md](docs/features/firmware-feature-map.md).

Contributions: [CONTRIBUTING.md](CONTRIBUTING.md). Security reports: [SECURITY.md](SECURITY.md).
