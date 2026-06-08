# V2 Status LED Firmware Contract

This firmware pass implements the V2/N16R8 status lighting as a centralized `status_led` component with a separate strip backend. Business code talks to semantic APIs such as `status_led_set_ble_state()` and `status_led_notify_key_event()`; it does not know whether the physical LEDs are WS2812, a future RGB driver, or another strip implementation.

## Hardware Resources

- `LED1`-`LED6`: semantic status rail on `PWM_RGB_Status`, `BOARD_PINS_RGB_STATUS_IO`, GPIO1.
- `LED7`-`LED10`, `LED15`-`LED16`, and `LED23`-`LED28`: EC11 12-LED knob ring on `PWM_RGB_EC11`, `BOARD_PINS_RGB_EC11_IO`, GPIO5.
- `LED11`-`LED14`: local key feedback on `PWM_RGB_KEY`, `BOARD_PINS_RGB_KEY_IO`, GPIO13.
- `LED17`-`LED22`: edge/frame group on `PWM_RGB_Edge`, `BOARD_PINS_RGB_EDGE_IO`, GPIO4.
- `GPIO14` is reserved for `BAT_CHG_IO` in the accepted V2/N16R8 board profile and must not be used for `PWM_RGB_KEY`.
- `VDD_LED` has no firmware-controlled enable pin in the current board contract. Firmware treats the rail as hardware-powered and exposes this as `vdd_led_enable=always_on_assumed` in `~LED:STATUS`.

## Semantic Rail

Default order is fixed until real hardware silkscreen validation proves otherwise:

- `LED1=PWR`
- `LED2=BLE`
- `LED3=REC`
- `LED4=AI`
- `LED5=OK`
- `LED6=WARN`

`PWR` owns battery and charge state. `BLE` owns pairing, reconnect, and connected confidence. `REC` only lights for a real capture/upload source named by firmware. If capture is unavailable, firmware shows `WARN + REC`. `AI` owns transfer, processing, thinking, and OTA progress. `OK` is a short success flash. `WARN` owns retryable and hard errors and pairs with a source LED.

## BLE Connection Source Of Truth

GAP/HID connection state is the source of truth for the BLE semantic LED. Advertising is allowed to drive `pairing` or `reconnecting` only while the GAP layer has no active connection. If a stale advertising-complete or advertising-restart path fires after the host is already connected, firmware skips advertising and refreshes `connected` instead of allowing `pairing` to overwrite the LED state.

After a connected event, the BLE LED uses the 6 second status window plus the bounded 8 second confidence window, with the first out-of-box connection allowed a longer bounded confidence window. Once those windows expire, an awake standard-profile device shows steady low blue on `LED2=BLE`.

Repeated same-state BLE callbacks are idempotent: they do not restart the status window or confidence window. This prevents host subscription noise from making the PWR green status indication look like an irregular post-connect blink.

## Status/Key Mapping Calibration

The camera calibration contract for the first product pass is intentionally limited to the ten status/key LEDs:

- `LED1=PWR`, `LED2=BLE`, `LED3=REC`, `LED4=AI`, `LED5=OK`, `LED6=WARN` on the status strip.
- `LED11=KEY1`, `LED12=KEY2`, `LED13=KEY3`, `LED14=KEY4` on the key strip.

The status and key strips keep separate color-order storage in firmware, both defaulting to `GRB` until camera validation proves a different order for either strip. `~LED:STATUS` exposes `mapping_contract`, `status_physical_map`, `key_physical_map`, and `separate_status_key_color_order=1` so static and serial checks can reject EC11/edge assumptions before camera capture. The manual `TEST:PIXEL` path can also address EC11 and edge/frame LEDs for hardware bring-up, while the first camera contract remains limited to status/key.

## Driver Boundary

`status_led.c` owns the business state machine, semantic LED mapping, current budget, USB commands, and frame rendering. `status_led_strip_backend.c` owns physical strip output: RGB byte order, ESP-IDF RMT at 10 MHz, the WS2812 encoder, and a 300 us reset latch with 4020-compatible bit timing. This keeps a future LED hardware change below the backend boundary instead of leaking into BLE, keyboard, OTA, or recording logic.

The status LED task refreshes every 50 ms and sends at most four short strip frames, so BLE, OTA, keyboard scan, power manager, and diagnostics are not blocked by software bit-banging.

## Profiles And Budget

Profiles are persisted in NVS through `~LED:PROFILE <off|low|standard|ambient|factory>`.

- `standard` is the product default. Routine status/key effects are capped at 85% and use brighter, saturated primary colors for daily readability.
- `low` caps routine effects at 35% for dark-room or low-power behavior.
- `ambient` caps routine effects at 65% and permits restrained accent behavior when the edge chain is available.
- `off` turns routine non-safety output off; safety/error/test paths can still light.
- `factory` keeps the full 100% path for calibration, manufacturing, and hardware bring-up.

Current is still estimated per frame with 20 mA per RGB channel at full scale. `standard`, `low`, and `ambient` have product budgets so a broad effect is dimmed instead of becoming a flashlight. `factory` and explicit safety/test paths retain the full bring-up budget.

## Product Effect Language

- Idle connected state is readable but not dominant: PWR/BLE confidence remains visible without using factory brightness.
- Pairing and reconnect use recognizable blue pulses without turning the whole status rail into an animation surface.
- Recording is a gold breathing semantic state and also marks the voice key locally without using warning red.
- Processing uses a saturated purple breath on `AI`; long processing settles to a calmer breath.
- Key LEDs are local transient feedback only: white on press/release, purple while processing, and green only during success confirmation.
- Warnings pair `WARN` with the source LED; critical battery and hard errors are allowed to be much brighter than normal routine states.
- The product palette favors clear semantic colors on the current diffuser: green for power/OK, blue for BLE, gold for recording, red for hard warning, amber for retryable warning, purple for AI, and white for local key feedback.
- RGBW, map, chase, and pixel test commands remain calibration tools and can drive full brightness independent of the product profile.

## Validation Commands

- `~LED:STATUS`
- `~LED:BUDGET`
- `~LED:PRIVACY`
- `~LED:TEST:RGBW <status|ec11|knob|ring|key|edge|all>`
- `~LED:TEST:MAP <status|ec11|knob|ring|key|edge|all>`
- `~LED:CHASE [status,key|status|key|ec11|edge|all] [step_ms]`
- `~LED:TEST:PIXEL <status|ec11|knob|ring|key|edge> <LEDn|index> <red|green|blue|white|off> [percent]`
- `~LED:PREVIEW <ready|pairing|reconnect|capture|desktop_mic|rec_not_available|processing|ok|low_battery|critical_battery|charging|full|sleep|clear>`
- `~LED:ERROR <ble|recording|ai|ota|power|system> <retryable|hard>`
- `~LED:PROFILE <off|low|standard|ambient|factory>`
- `~LED:OFF` clears test/status output immediately and stays dark until the next status/key event. It is not the sleep path; `status_led_prepare_sleep()` remains the sleep-only full output disable.
- `~LED:WAKE`

When camera capture is not trustworthy, `tools/status_led_manual_calibration.ps1` drives one status/key LED RGBW/off command at a time at 100% brightness and records human-eye feedback under the step validation directory. For EC11/edge bring-up, use direct serial `~LED:TEST:PIXEL ec11 LED7 white 100`, `~LED:TEST:PIXEL edge LED17 white 100`, or full-strip `~LED:TEST:RGBW ec11 edge`/`~LED:CHASE ec11,edge`.

Real V2 hardware validation still has to record the actual RGB color order and physical LED order with photos or video. The firmware default color order is `GRB` for all four strips. Status/key use the 3528 WS2812-style part, while EC11/edge use the 4020 WS2812B part; both datasheets use 24-bit GRB data, but the 4020 part needs the longer reset latch and stricter low-time timing used by the backend.

During 2026-06-08 hardware bring-up, status/key LEDs responded on the current board but EC11/edge LEDs did not light after the firmware was flashed with the 4020-compatible timing above. Treat EC11/edge darkness as a separate hardware pinout or continuity investigation before changing firmware color order or WS2812 timing again.
