# Human Authorization: 2800 mV Battery Empty Threshold

This artifact records the product/spec authorization needed after the second review of step 1.7.

## Authorization

After `zai1` returned step 1.7 to `pending` on 2026-06-15 because the battery empty threshold had been changed from the previously reviewed `3000 mV=0%` gate to `2800 mV=0%`, the human operator explicitly authorized continuing with the 2800 mV route in this chat.

This authorization covers:

- `BATTERY_MONITOR_EMPTY_MV = 2800U`
- `POWER_MANAGER_LOW_BATTERY_SHUTDOWN_MAX_MV = 2800U`
- static verifier expectations for `2800 mV=0%`
- step 1.7 acceptance item 1 using `2800 mV=0%`

This authorization does not claim real PWR_HOLD power-off success. The PWR_HOLD route remains a documented hardware known issue until the board-level latch/routing is repaired or verified with a meter/scope.

## Rationale

The 2800 mV threshold is a human-approved product decision for this firmware branch, separate from the PWR_HOLD hardware limitation. The 5000 ms low-battery confirmation debounce remains part of the robustness change so short ADC or power-source transients cannot immediately shut the board down.
