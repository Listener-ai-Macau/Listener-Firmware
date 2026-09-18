# Listener Firmware

Firmware for the Listener voice keyboard: an ESP32-S3 desktop controller that captures speech, streams it to Listener Type over Bluetooth, and puts recording controls and product status under your hand.

[简体中文](README.zh.md) · [繁體中文](README.zh-TW.md) · [Firmware releases](https://github.com/Listener-ai-Macau/Listener-Firmware/releases) · [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type)

<p align="center">
  <img src="docs/assets/readme/keyboard-front.jpg" alt="Listener voice keyboard with keys, knob, and status lights" width="900" />
</p>

## One Listener product

| Repository | Responsibility |
| --- | --- |
| [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type) | Speech recognition, text cleanup and styles, translation, history, cursor insertion, settings UI, and desktop updates |
| Listener Firmware | Microphone capture, BLE audio/HID, physical controls, LEDs, battery and power behavior, device settings, diagnostics, and over-the-air (OTA) firmware updates |

The firmware does not turn speech into text on its own. Listener Type receives the audio, produces the final text, and inserts it into the focused app.

## Hardware at a glance

| Part | Current V2 profile |
| --- | --- |
| Controller | ESP32-S3-WROOM-1-N16R8, 16 MB flash, 8 MB Octal PSRAM |
| Audio | PDM microphone, 16 kHz capture, framed BLE streaming with retained/replayable packets |
| Controls | Clickable and rotary EC11 knob plus KEY1–KEY4 |
| Feedback | Six status zones: PWR, BLE, REC, AI, OK, WARN |
| Wireless | BLE audio services, BLE HID keyboard, settings, diagnostics, battery service, and OTA |
| Power | Li-ion battery, USB-C charging, battery measurement, low-power idle, wake on controls, and timed shutdown |

## What the firmware does

| Area | Capabilities |
| --- | --- |
| Voice capture | Starts/stops device recording, captures PDM audio, frames and queues PCM, preserves trailing audio, and reports transport state |
| BLE audio | Subscribable audio stream, paced notification transport, retry/backpressure handling, retained packet replay, and session identity |
| Keyboard controls | EC11 click/double-click/long-press/rotation; KEY1–KEY4 single-, double-, and long-press events; safe BLE HID fallbacks |
| Product feedback | Recording, transfer, processing, success, warning, pairing, charging, battery, and low-power LED states |
| Device settings | Persistent BLE name, LED brightness by zone, knob rotation action, plugged/battery idle timers, low-power policy, and shutdown timers |
| Battery and power | Standard BLE battery reporting, charging/full state, low-battery protection, idle microphone suspension, wakeable controls, and hardware shutdown |
| Pairing and recovery | Discoverable pairing, bond reset, reconnect behavior, serial maintenance, and explicit factory reset paths |
| OTA | Dual application slots, package validation, progress reporting, pending verification, rollback, and normal preservation of pairing/settings |
| Diagnostics | Flash-backed event log, health heartbeat, BLE/serial export, source filters, bounded reports, and machine-readable diagnostic bundles |
| Engineering tools | Reproducible setup/build/flash/monitor commands plus static and hardware validation for board, audio, BLE, power, keys, and OTA |

## From power-on to text

1. Charge the keyboard and click the knob to power on.
2. In Listener Type, choose Settings → Device → Start pairing. Select `listener` in Windows Bluetooth, then check the connection in Type.
3. Focus a text field, click the knob, speak, then click again.
4. REC shows capture; AI shows transfer or processing; OK confirms completion. Listener Type puts the result at the cursor.

Voice-triggered recording is configured in Listener Type. The firmware keeps a low-power voice activity path ready and transports the candidate audio; the desktop app validates the wake phrase and optional voiceprint before accepting a dictation session.

## Reading the lights

The keyboard speaks in light: six status LEDs, a ring around the knob, and a light under every key. Learn the vocabulary once, and a glance tells you what the device is doing.

| Light | What it's saying |
| --- | --- |
| **PWR** | Green: healthy battery. Amber: time to charge. Red: low. Red double-blink: nearly empty. On USB-C it turns white — breathing while charging, solid when full |
| **BLE** | Blue pulses: pairing or reconnecting. A dim blue double-blink: connected, but Listener Type isn't running yet. Solid blue: ready |
| **REC** | Gold, rising and falling with your voice. If it's lit, you're being heard |
| **AI** | A purple "da-da-da" beat: your audio arrived and is being transcribed |
| **OK** | Green for two seconds: the text landed. During an OTA update it turns teal and fills with progress, the knob ring filling along with it |
| **WARN** | Red or amber: something needs attention. Every error is reported here, and only here |

Green is good news, gold is listening, purple is thinking, blue is Bluetooth, white is charging, red is trouble.

The keys answer back too: white when your press registers, purple when the action lands — one blink for a click, two for a double-click, solid while you hold.

## The controls

| Gesture | Default behavior |
| --- | --- |
| Click the knob | Start or stop dictation |
| Double-click the knob | Forget the Bluetooth bond and become discoverable again |
| Press and hold the knob | Power off — an amber ring fills around the knob; keep holding until it completes |
| Rotate the knob | System volume; configurable to screen brightness or disabled |
| KEY1–KEY4 | Click, double-click, and long-press actions you assign in Listener Type → Settings → Device. KEY3 ships as Ctrl+V |

Unassigned keys only emit harmless F13–F24 codes and the knob falls back to Shift+F13, so nothing ever types itself into your documents. `Esc` on the computer cancels a recording at any moment.

## Power, briefly

The knob is the only power button. Left alone, the keyboard naps — about a minute on battery, three minutes on USB — and every light but PWR goes dark. All lights out isn't a failure; it's asleep. Any key, click, or twist wakes it. After roughly ten idle minutes on battery it shuts down for real; click the knob to bring it back.

Status lights, key lights, the knob ring, and the edge glow each have their own brightness in Listener Type, plus a few presets. Even with the lighting profile set to off, battery warnings and errors still shine through — important messages are never muted. Windows can read the battery level through the standard Bluetooth Battery Service.

<p align="center">
  <img src="docs/assets/readme/device-settings.png" alt="Listener device settings in Listener Type" width="720" />
</p>

## Update and recovery

Normal users update from Listener Type with a release OTA ZIP. OTA uses two firmware slots and can roll back an unverified image; normal updates preserve Bluetooth bonds and device settings. Double-click the knob for pairing recovery. USB flashing and full erase are engineering/recovery operations and can reset stored state.

The latest tagged firmware is on [Releases](https://github.com/Listener-ai-Macau/Listener-Firmware/releases). Match it with the same Listener Type release where possible, and verify the SHA-256 published on the release page.

## Release history

| Release | Firmware milestone |
| --- | --- |
| 1.0.1 | Established the Listener firmware release package |
| 1.0.3 | Accepted BLE transfer throughput, OTA/boot behavior, and status-light contracts |
| 1.0.4 | Consolidated recording, wake, automatic ending, pairing recovery, power, and product-light behavior |
| 1.0.5 | Matched the current voice keyboard controls and feedback with the Type 1.0.5 daily-use path |

Exact OTA packages and checksums are kept on the [GitHub Releases page](https://github.com/Listener-ai-Macau/Listener-Firmware/releases).

## Build and flash

Windows scripts prepare ESP-IDF `release/v5.5` and keep the environment consistent:

```powershell
pwsh -NoProfile -File .\tools\setup_windows.ps1
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
pwsh -NoProfile -File .\tools\monitor.ps1 -Port COMx
```

Use `tools\idf.ps1` for ad hoc ESP-IDF commands instead of running bare `idf.py`.

## Repository map

- `components/` — portable product logic for controls, power, settings, health, OTA, and diagnostics
- `ports/esp32/` — ESP-IDF bindings for board, audio, BLE, storage, and hardware services
- `protocols/` — shared device protocol definitions and codecs
- `main/` — startup and subsystem wiring
- `tools/` — setup, build, flash, monitor, packaging, diagnostics, and validation
- `docs/features/firmware-feature-map.md` — full implementation and validation map

Read [CONTRIBUTING.md](CONTRIBUTING.md) before contributing and [SECURITY.md](SECURITY.md) for security reports. Listener Firmware is open source under the [Apache-2.0 license](LICENSE).
