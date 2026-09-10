# Listener Firmware

Firmware for the Listener voice keyboard — the code that actually runs on the device. ESP32-S3, a PDM microphone, a clickable knob, four keys, six lights, Bluetooth audio, OTA updates.

The keyboard only listens and lights up. Turning speech into text happens on the computer, in [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type).

[中文](README.zh.md) · [繁體中文](README.zh-TW.md) · [Release notes](docs/release/1.0.5.md)

## Using the keyboard

- Click the knob to start and stop dictation.
- Double-click to reset pairing; long-press to power off; turn it to change the PC volume (remappable in Type).
- The four keys are yours — bind them in Type: paste, copy, open the app, whatever you like.
- The lights tell you what's going on: power, Bluetooth, recording, processing. All dark usually means it's asleep, not dead.
- Or skip the knob entirely: say 「开始录音」 and it starts listening on its own.

First time: charge it, click the knob to power on, start pairing inside Type, then pick `listener` in Windows Bluetooth. Full walkthrough: [voice keyboard guide](https://github.com/Listener-ai-Macau/Listener-Type/blob/master/docs/quickstart/voice-keyboard-readme.md).

## Updating

The easy way is OTA from Listener Type — your settings survive. The current version is 1.0.5 ([notes](docs/release/1.0.5.md)). One known bug: the battery reading right after power-off can be wrong.

## Building and flashing

The scripts are for Windows and install ESP-IDF v5.5 for you:

```powershell
pwsh -NoProfile -File .\tools\setup_windows.ps1
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
```

Don't run bare `idf.py` — use `tools\idf.ps1`, which loads the IDF environment first.

The interesting parts are all readable: the audio path, the LED logic, the BLE protocol. GPIO details live in [docs/features/firmware-feature-map.md](docs/features/firmware-feature-map.md). Bugs and PRs are welcome — see [CONTRIBUTING.md](CONTRIBUTING.md); security issues go to [SECURITY.md](SECURITY.md).
