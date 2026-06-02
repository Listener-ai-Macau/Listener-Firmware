# Low-Power Wake Policy

Current N4 firmware uses `components/power_manager` for idle power reduction and overnight deep-sleep diagnostics. Deep sleep wakes from KEY4/GPIO21. EC11-KEY/GPIO35 is the active recording key on the N4 validation profile, but it is not RTC deep-sleep wake capable.

Observable firmware contract:

- `~POWER:STATUS` reports `wake_policy=key4_only`, `wake_capable_keys=KEY4/GPIO21`, `wake_gpio_mask`, `wake_key_gpio`, `voice_key_gpio`, `voice_key_rtc_capable`, `voice_key_deep_sleep_wake`, `voice_key_limitation`, `wake_user_action`, thresholds, blockers, current battery, the last sleep/wake state, and sleep drain telemetry (`sleep_entry_battery_*`, `wake_battery_*`, `sleep_duration_ms`, `sleep_drain_*`).
- Sleep entry and rejected sleep attempts log power diagnostics with blocker or reason fields plus `DIAG_POWER_WAKE_POLICY`, so exported diag logs can distinguish firmware behavior from the N4 KEY4-only wake policy.
- Automatic overnight deep sleep keeps the 30 minute default threshold (`CONFIG_POWER_MANAGER_OVERNIGHT_SLEEP_MS=1800000`) and uses only RTC-capable wake GPIOs.
- The voice key remains available after wake and during active runtime, but N4 users must press KEY4/GPIO21 to wake from deep sleep.

## Production Requirement

The N4 KEY4-only wake policy is not acceptable as the final production interaction for a voice-first product. Production hardware must provide one obvious primary voice/wake input that is RTC deep-sleep wake capable and safe in the off/sleep electrical state.

For the V2/N16R8 schematic, `EC11-KEY_IO/GPIO11` is the preferred candidate if the encoder push is the user-facing voice/wake control. Firmware must not enable it for final deep-sleep wake until hardware signs off isolation from the raw power-latch/VBAT domain, reset/off-state leakage, pull policy, and false-wake behavior. If that sign-off is not available, production firmware should either keep deep sleep disabled for the user-facing mode or block release with a hardware follow-up instead of shipping a hidden KEY4-only wake requirement.

The policy is intentionally centralized in `power_manager` so desktop diagnostics and future board revisions can key off one stable status output instead of board-specific guesswork.
