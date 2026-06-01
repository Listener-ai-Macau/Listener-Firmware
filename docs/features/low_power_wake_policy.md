# Low-Power Wake Policy

Current N4 firmware uses `components/power_manager` for idle power reduction and overnight deep-sleep diagnostics. Deep sleep wakes from KEY3/GPIO21. `VOICE` uses GPIO45 through the current board's legacy KEY1 pin define, but it is not RTC deep-sleep wake capable on the active N4 profile.

Observable firmware contract:

- `~POWER:STATUS` reports `wake_policy=key3_only`, `wake_capable_keys=KEY3/GPIO21`, `wake_gpio_mask`, `wake_key_gpio`, `voice_key_gpio`, `voice_key_rtc_capable`, `voice_key_deep_sleep_wake`, `voice_key_limitation`, `wake_user_action`, thresholds, blockers, current battery, the last sleep/wake state, and sleep drain telemetry (`sleep_entry_battery_*`, `wake_battery_*`, `sleep_duration_ms`, `sleep_drain_*`).
- Sleep entry and rejected sleep attempts log power diagnostics with blocker or reason fields plus `DIAG_POWER_WAKE_POLICY`, so exported diag logs can distinguish firmware behavior from the N4 KEY4-only wake policy.
- Automatic overnight deep sleep keeps the 30 minute default threshold (`CONFIG_POWER_MANAGER_OVERNIGHT_SLEEP_MS=1800000`) and uses only RTC-capable wake GPIOs.
- The voice key remains available after wake and during active runtime, but users must press KEY3/GPIO21 to wake from deep sleep.

The policy is intentionally centralized in `power_manager` so desktop diagnostics and future board revisions can key off one stable status output instead of board-specific guesswork.
