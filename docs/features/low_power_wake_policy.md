# Low-Power Wake Policy

Current V1 firmware uses `components/power_manager` for idle power reduction and overnight deep sleep. The default product policy accepts KEY4/GPIO21 as the deep-sleep wake path for this board and explicitly reports that the EC11 voice key on GPIO35 is not RTC deep-sleep wake capable.

Observable firmware contract:

- `~POWER:STATUS` reports `wake_policy=key4_only`, `wake_capable_keys=KEY4/GPIO21`, `wake_gpio_mask`, `wake_key_gpio`, `voice_key_gpio`, `voice_key_rtc_capable`, `voice_key_deep_sleep_wake`, `voice_key_limitation`, `wake_user_action`, thresholds, blockers, current battery, the last sleep/wake state, and sleep drain telemetry (`sleep_entry_battery_*`, `wake_battery_*`, `sleep_duration_ms`, `sleep_drain_*`).
- Sleep entry and rejected sleep attempts log power diagnostics with blocker or reason fields plus `DIAG_POWER_WAKE_POLICY`, so exported diag logs can distinguish firmware failure from current-board GPIO35 wake limitations.
- Automatic overnight deep sleep keeps the 30 minute default threshold (`CONFIG_POWER_MANAGER_OVERNIGHT_SLEEP_MS=1800000`) and uses only RTC-capable wake GPIOs.
- When the device is in deep sleep on V1 hardware, users must press KEY4/GPIO21 or reset/power cycle. Pressing the GPIO35 voice key is not expected to wake the ESP32-S3 from deep sleep on this board.

The policy is intentionally centralized in `power_manager` so desktop diagnostics and future board revisions can key off one stable status output instead of board-specific guesswork.
