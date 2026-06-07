# V2 Status LED Firmware Contract

This firmware pass implements the V2/N16R8 WS2812 status lighting as a centralized `status_led` component.

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

## Driver

WS2812 output uses ESP-IDF RMT at 10 MHz with an 800 kHz WS2812 encoder and a 50 us reset latch. The status LED task refreshes every 50 ms and sends at most four short strip frames, so BLE, OTA, keyboard scan, power manager, and diagnostics are not blocked by software bit-banging.

## Profiles And Budget

Profiles are persisted in NVS through `~LED:PROFILE <off|low|standard|ambient|factory>`.

- `standard` is the shipping default. It targets 40 mA sustained before `VDD_LED` hardware sign-off and allows only quiet awake `PWR`/`BLE` baseline indicators.
- `low` suppresses ordinary idle lighting and targets 25 mA sustained.
- `off` turns routine output off but still permits minimum critical/privacy indications.
- `ambient` allows a restrained edge accent.
- `factory` is for explicit LED validation only.

Current is estimated per frame with 20 mA per RGB channel at full scale and clamped to the active profile budget.

## Validation Commands

- `~LED:STATUS`
- `~LED:BUDGET`
- `~LED:PRIVACY`
- `~LED:TEST:RGBW <status|ec11|knob|ring|key|edge|all>`
- `~LED:TEST:MAP <status|ec11|knob|ring|key|edge|all>`
- `~LED:TEST:PIXEL <status|key> <LEDn|index> <red|green|blue|white|off> [percent]`
- `~LED:PREVIEW <ready|pairing|reconnect|capture|desktop_mic|rec_not_available|processing|ok|low_battery|critical_battery|charging|full|sleep|clear>`
- `~LED:ERROR <ble|recording|ai|ota|power|system> <retryable|hard>`
- `~LED:PROFILE <off|low|standard|ambient|factory>`
- `~LED:OFF`
- `~LED:WAKE`

Real V2 hardware validation still has to record the actual RGB color order and physical LED order with photos or video. The firmware default color order is `GRB` for all four strips.
The `TEST:PIXEL` path is intentionally limited to `status` and `key` for camera calibration so EC11 ring and edge/frame LEDs are not driven during the first status/key bring-up pass.
