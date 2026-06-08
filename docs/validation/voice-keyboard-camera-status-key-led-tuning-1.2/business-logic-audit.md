# Status/key LED business logic audit

Plan: `voice-keyboard-camera-status-key-led-tuning`
Step: `1.2`

## Source Audit

The status LED business layer is separated from the physical strip backend:

- `components/status_led/status_led.c` owns semantic state, preview commands, USB command parsing, current estimation, key/status frame rendering, and business API entry points.
- `components/status_led/status_led_strip_backend.c` owns RMT/WS2812 transmission, pixel byte packing, RGB/GRB color order, and output failure diagnostics.

Business callers use semantic APIs and do not write strip pixels directly:

- BLE connection and recovery paths call `status_led_set_ble_state()`, `status_led_set_error()`, and `status_led_clear_error()`.
- Voice recording calls `status_led_set_recording()`, `status_led_set_processing()`, `status_led_notify_success()`, and recording-domain errors.
- OTA calls `status_led_set_processing()`, `status_led_notify_success()`, and OTA-domain errors.
- Custom key handling calls `status_led_notify_key_event()`.

## Scheme Match

The source matches the accepted status rail scheme:

- `LED1=PWR`: battery, charging, full, low, and critical battery are rendered through `status_led_render_power_locked()`.
- `LED2=BLE`: pairing, reconnecting, connected confidence, and BLE errors are rendered through `status_led_render_ble_locked()` and source-paired error rendering.
- `LED3=REC`: active device/desktop recording lights REC; unavailable capture sets recording-domain WARN plus REC.
- `LED4=AI`: processing/thinking/OTA activity lights AI.
- `LED5=OK`: success is a short OK flash and can pair with transient key feedback.
- `LED6=WARN`: retryable/hard errors light WARN and pair with the source semantic LED.
- `LED11..LED14=KEY1..KEY4`: key events drive local transient feedback only; they are not idle backlights.

The current product-effect policy intentionally separates calibration brightness from routine product brightness. `factory` and explicit test/safety commands can still drive full brightness, while the default `standard` profile restores per-effect brightness, breathing, short-flash, and warning intensity levels. `docs/features/status_led.md` and `~LED:BUDGET` report this as `product_v1`.

## Scheme Review

The high-level scheme is reasonable for this board because it keeps the six always-visible status meanings stable and avoids using KEY LEDs as ambient/status substitutes. WARN pairing with a source LED is useful: it tells the user both severity and subsystem without inventing many colors.

Two constraints should remain explicit:

- Full brightness remains appropriate for visual bring-up and factory tests. Routine product behavior now uses profile-capped brightness so idle, status, key feedback, success, recording, and warning states have distinct visual weight.
- EC11 ring and edge/frame LEDs are separate product surfaces. They should get their own visual mapping/effect pass instead of being silently accepted under the status/key step.

## Remaining Hardware Scope

`COM6` is currently locked by `oai1` for this LED tuning session so other agents do not overwrite the flashed LED firmware while the hardware evidence is being captured.

EC11 ring and edge/frame LEDs were tested on the v4 firmware and did not light. That residual is no longer treated as a status/key business-logic blocker: `hardware-4020-pinout-audit.md` records the likely 4020 symbol/footprint pinout mismatch and the continuity checks needed to close the hardware question.
