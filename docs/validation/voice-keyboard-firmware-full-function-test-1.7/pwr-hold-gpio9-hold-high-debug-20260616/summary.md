# GPIO9 Hold-High Debug Evidence

Date: 2026-06-16
Port: COM9

This directory captures the earlier temporary debug firmware that tried to drive `PWR_HOLD/GPIO9` high after boot and then repeatedly kept it high. The source was restored before the final commit; these logs are evidence only.

Relevant observations:

- `gpio_config(GPIO9)` returned `ESP_OK`
- `gpio_set_level(GPIO9, 1)` returned `ESP_OK`
- `gpio_get_level(GPIO9)` repeatedly read back `0`
- The normal firmware later reproduced the same failure through `~POWER:TEST:SHUTDOWN`

Conclusion: the observed failure is not caused by the 10-second debug timer. Even push-pull GPIO9 high drive is read back low, which points to board wiring, latch load, or another external low source on the PWR_HOLD net.
