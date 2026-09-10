# Listener Firmware

<p align="center">
  <strong>Open firmware for the Listener voice keyboard.</strong><br/>
  Press, speak, watch the lights. Listener Type puts the words at your cursor.
</p>

<p align="center">
  <a href="README.zh.md">中文</a> ·
  <a href="docs/product/features.md">Features</a> ·
  <a href="https://github.com/Listener-ai-Macau/Listener-Type">Listener Type</a>
</p>

This is not an ESP32 sample. It is the software on the keyboard you buy: microphone, EC11, four keys, status LEDs, BLE audio, pairing recovery, low power, OTA.

Speech-to-text and cursor insertion live in the desktop app. Use this repo with [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type).

## Why buy the keyboard

Type works with a computer mic. The keyboard is the **desk device**:

- Click the knob to start/stop — no hunting for a hotkey
- REC / AI / BLE lights, so you are not guessing whether it heard you
- Wake phrase “开始录音”, optional voiceprint
- Four keys you can bind (paste, copy, open Type)
- Open firmware, OTA from Type, double-click the knob to re-pair

1.0.5 is the daily-driver firmware baseline. The product is keyboard plus Type.

## 30 seconds

1. Charge, click EC11 to power on.
2. Open Listener Type, start pairing, pick `listener` in Windows Bluetooth.
3. Click a notepad field, click EC11, speak, click again.
4. Watch REC and the Type capsule; text should hit the cursor.

Handbook in the Type repo: [voice keyboard](https://github.com/Listener-ai-Macau/Listener-Type/blob/master/docs/quickstart/voice-keyboard-readme.md)

## What the firmware owns

| Capability | How you use it |
| --- | --- |
| Record | EC11 click (remap in Type) |
| Re-pair | EC11 double-click, then choose `listener` |
| Lights | PWR power, BLE link, REC capture, AI processing |
| Wake | Device waits for the wake phrase |
| Update | OTA in Type, or USB flash from this repo |
| Battery | Plugged/battery idle; dark LEDs usually mean sleep |

GPIO, HID fallbacks, and bench scripts stay in the [firmware feature map](docs/features/firmware-feature-map.md). User-facing catalog: [product features](docs/product/features.md).

## Open source

Read the protocol, LED, and audio paths. File issues and PRs.
Flash a current 1.0.5 build — do not treat an old OTA zip as “latest.”

```powershell
pwsh -NoProfile -File .\tools\setup_windows.ps1
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
```

Do not run bare `idf.py` outside the IDF env this repo loads.

Repo: [Listener-ai-Macau/Listener-Firmware](https://github.com/Listener-ai-Macau/Listener-Firmware)

## Layout

- `main/` — thin `app_main()`
- `components/` — product logic
- `protocols/` — codecs and protocol
- `ports/` — ESP-IDF bindings
- `tools/` — build, flash, monitor, diagnostics
- `docs/` — product and maintenance docs
