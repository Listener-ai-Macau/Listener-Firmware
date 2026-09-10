<h1 align="center">Listener Firmware</h1>

<p align="center">
  <strong>The open firmware inside the Listener voice keyboard.</strong><br/>
  Click the knob, speak, watch the lights — the words show up on your computer.
</p>

<p align="center">
  <strong>English</strong> ·
  <a href="README.zh-CN.md">简体中文</a> ·
  <a href="README.zh-TW.md">繁體中文</a>
</p>

<p align="center">
  <a href="https://github.com/Listener-ai-Macau/Listener-Firmware/releases"><img src="https://img.shields.io/github/v/release/Listener-ai-Macau/Listener-Firmware" alt="Release" /></a>
  <img src="https://img.shields.io/badge/ESP--IDF-v5.5-blue" alt="ESP-IDF v5.5" />
</p>

<p align="center">
  <a href="docs/product/features.md">Features</a> ·
  <a href="docs/release/1.0.5.md">Release notes</a> ·
  <a href="https://github.com/Listener-ai-Macau/Listener-Type">Listener Type</a>
</p>

<!-- Hero: add a photo of the keyboard here once one exists. -->

This is the software that ships on the keyboard you buy — not an ESP32 demo. An ESP32-S3, a PDM microphone, an EC11 knob, four keys, six status lights, BLE audio, low power, and over-the-air updates.

Hearing you, lighting up, and sending audio is this repo's job. Turning speech into text at your cursor belongs to [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type), the desktop app.

## Under the hood

- ESP32-S3 (16 MB flash, 8 MB PSRAM)
- PDM digital microphone, 16 kHz capture
- EC11 rotary encoder, four hotkeys, six-LED status strip
- Li-ion battery with USB-C charging

## What it does in your hand

- **Click the knob** — start / stop dictation
- **Double-click** — forget the pairing and become discoverable again
- **Long-press** — power off; **turn the knob** — PC volume (remappable in Type)
- **Four keys** — bind single-, double-, and long-press actions in Type: paste, copy, open the app
- **Six lights** — PWR power, BLE link, REC capturing, AI processing, OK, WARN. You always know which stage you are in; dark LEDs usually mean it is asleep, not broken
- **Wake phrase** — say 「开始录音」 and it starts listening; optional voiceprint enrollment from Type

## 30 seconds to first words

1. Charge, click the knob to power on.
2. In Listener Type, start pairing, then pick `listener` in Windows Bluetooth.
3. Click a text field, click the knob, speak, click again.
4. REC lights up, the Type capsule answers, and text lands at the cursor.

Handbook in the Type repo: [voice keyboard guide](https://github.com/Listener-ai-Macau/Listener-Type/blob/master/docs/quickstart/voice-keyboard-readme.md)

## Who does what

| The keyboard (this repo) | Listener Type (desktop app) |
| --- | --- |
| Hears you: mic, keys, lights, pairing, battery, OTA | Writes for you: transcribe, polish, insert at the cursor |
| Without Type, it is a well-mannered BLE gadget | Without the keyboard, Type works with your PC mic |

## Updating

OTA from inside Listener Type — recommended, and it keeps your settings — or USB-flash a build from this repo. Current release is **1.0.5** ([release notes](docs/release/1.0.5.md)). Known 1.0.5 quirk: the battery reading right after power-off can be wrong.

## Build and flash

The scripts set up ESP-IDF `release/v5.5` for you on Windows:

```powershell
pwsh -NoProfile -File .\tools\setup_windows.ps1
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
```

Do not run bare `idf.py` here — use `tools\idf.ps1` so the IDF environment loads first.

The protocol, LED, and audio paths are all in the open: read them, file issues, send PRs. GPIO maps and bench scripts live in the [firmware feature map](docs/features/firmware-feature-map.md). Read [CONTRIBUTING.md](CONTRIBUTING.md) before sending code; report vulnerabilities via [SECURITY.md](SECURITY.md).
