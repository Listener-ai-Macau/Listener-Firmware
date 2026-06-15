# PWR_HOLD Hardware Limitation Evidence

Step 1.7 does not claim real hardware power-off on this board.

## Evidence

- Firmware pin routing is `BOARD_PINS_PWR_HOLD_IO = GPIO_NUM_11` in `ports/esp32/board_pins/include/board_pins.h`; source search did not find a GPIO46 PWR_HOLD route.
- Fresh 2800 mV firmware probe artifact:
  `docs/validation/voice-keyboard-firmware-full-function-test-1.7/shutdown-drive-high-after-2800-oai1-20260615-161949/`
- `~POWER:STATUS` and `~BOARD:STATUS` both reported `pwr_hold_gpio=11`, `pwr_hold_level=low`, and `pwr_hold_policy=v2_gpio11_power_latch_runtime_low_drive_high_for_hardware_shutdown`.
- `~BOARD:STATUS` reported `battery_policy="product_empty_2800mv_full_4200mv_absolute_min_2700mv"`, proving the flashed firmware contains the 2800 mV empty threshold.
- Manual `~POWER:SHUTDOWN` entered the shutdown path and requested PWR_HOLD/GPIO11 high, but the board logged `PWR_HOLD/GPIO11 driven high for hardware shutdown did not settle high within 500 ms` and `hardware shutdown failed: PWR_HOLD/GPIO11 drive-high ret=ESP_ERR_INVALID_STATE`.
- The post-shutdown port poll kept reporting `ports=COM7` for all 24 polls, so there was no observable USB serial disappearance or cold-boot recovery event.
- Firmware restored/guarded runtime low after the failed shutdown path and requested BLE reconnect for debugging, rather than silently treating the failed hardware power-off as success.

## Operator Next Step

Before 1.7 can claim real power-off, verify the board-level latch/routing between ESP32-S3 GPIO11 and the PWR_HOLD net with a meter or scope, including external pull/load behavior. If the intended PWR_HOLD net is not on GPIO11, update the board pin mapping and rerun the shutdown probe. If GPIO11 is correct but cannot drive the latch high, repair the latch/load path before accepting the real power-off route.

For this 1.7 submission, the acceptable route is therefore the documented root-cause/logging path: two independent USB-unplug/no-serial flash diagnostic captures with `verify_unplugged_flash_diag_bundle.py` PASS, plus this known-issue artifact. That route proves the unplugged light-cycle behavior from flash logs without claiming successful hardware power removal.
