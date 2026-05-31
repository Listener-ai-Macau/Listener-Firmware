# Low-Power Wake Policy

Current V2/N16R8 firmware uses `components/power_manager` for idle power reduction and overnight deep-sleep diagnostics. The product wake intent is EC11-KEY_IO/GPIO11, but production deep-sleep wake is disabled until the V2 EC11 isolation and off-state leakage are signed off.

Observable firmware contract:

- `~POWER:STATUS` reports `wake_policy=v2_ec11_provisional`, `wake_capable_keys=EC11-KEY_IO/GPIO11 provisional`, `wake_gpio_mask`, `wake_key_gpio`, `voice_key_gpio`, `voice_key_rtc_capable`, `voice_key_deep_sleep_wake`, `voice_key_limitation`, `wake_user_action`, thresholds, blockers, current battery, the last sleep/wake state, and sleep drain telemetry (`sleep_entry_battery_*`, `wake_battery_*`, `sleep_duration_ms`, `sleep_drain_*`).
- Sleep entry and rejected sleep attempts log power diagnostics with blocker or reason fields plus `DIAG_POWER_WAKE_POLICY`, so exported diag logs can distinguish firmware behavior from V2 EC11 hardware sign-off limitations.
- Automatic overnight deep sleep keeps the 30 minute default threshold (`CONFIG_POWER_MANAGER_OVERNIGHT_SLEEP_MS=1800000`) and uses only RTC-capable wake GPIOs.
- Until `CONFIG_LISTENER_V2_ENABLE_EC11_DEEP_SLEEP_WAKE` is enabled after hardware sign-off, attempted deep sleep is rejected with an unavailable wake GPIO instead of silently entering a state that users cannot wake.

The policy is intentionally centralized in `power_manager` so desktop diagnostics and future board revisions can key off one stable status output instead of board-specific guesswork.
