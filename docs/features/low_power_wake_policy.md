# Low-Power Wake Policy

Current V2/N16R8 firmware uses `components/power_manager` for idle power reduction and overnight deep-sleep diagnostics. EC11-KEY_IO/GPIO11 is the active recording key and the only user-facing RTC-capable wake candidate, but deep-sleep wake remains disabled until the V2 power-latch/off-state behavior is signed off.

Observable firmware contract:

- `~POWER:STATUS` reports `wake_policy=v2_ec11_provisional`, `wake_capable_keys=EC11_KEY/GPIO11`, `wake_gpio_mask`, `wake_key_gpio`, `voice_key_gpio`, `voice_key_rtc_capable`, `voice_key_deep_sleep_wake`, `voice_key_limitation`, `wake_user_action`, thresholds, blockers, sleep-only blockers, USB/charger raw levels, interpreted `external_power_present`, `charging`, `charge_full`, current battery, the last sleep/wake state, and sleep drain telemetry (`sleep_entry_battery_*`, `wake_battery_*`, `sleep_duration_ms`, `sleep_drain_*`).
- Sleep entry and rejected sleep attempts log power diagnostics with blocker or reason fields plus `DIAG_POWER_WAKE_POLICY`; charging/USB automatic sleep blocks also log `DIAG_POWER_EXTERNAL_POWER`, so exported diag logs can distinguish firmware behavior from the V2 EC11 provisional wake policy and from non-power blockers.
- Automatic overnight deep sleep keeps the 30 minute default threshold (`CONFIG_POWER_MANAGER_OVERNIGHT_SLEEP_MS=1800000`) and uses only RTC-capable wake GPIOs. With the default `CONFIG_LISTENER_V2_ENABLE_EC11_DEEP_SLEEP_WAKE=n`, `wake_gpio_mask=0` and sleep entry is rejected rather than silently enabling an unsigned wake path.
- USB or charger/full detection blocks inactivity-triggered automatic deep sleep while preserving connected/disconnected awake idle behavior. The explicit `~POWER:SLEEP` debug command remains the manual sleep override and records `manual_command` rather than `overnight_idle`.
- The voice key remains available during active runtime. Deep-sleep wake requires explicit V2 EC11 hardware sign-off before enabling `CONFIG_LISTENER_V2_ENABLE_EC11_DEEP_SLEEP_WAKE`.

## Production Requirement

The old KEY4-only wake policy is not acceptable as the final production interaction for a voice-first product. Production hardware must provide one obvious primary voice/wake input that is RTC deep-sleep wake capable and safe in the off/sleep electrical state.

For the V2/N16R8 schematic, `EC11-KEY_IO/GPIO11` is the preferred candidate if the encoder push is the user-facing voice/wake control. Firmware must not enable it for final deep-sleep wake until hardware signs off isolation from the raw power-latch/VBAT domain, reset/off-state leakage, pull policy, and false-wake behavior. If that sign-off is not available, production firmware should keep deep sleep disabled for the user-facing mode or block release with a hardware follow-up instead of shipping a hidden wake requirement.

The policy is intentionally centralized in `power_manager` so desktop diagnostics and future board revisions can key off one stable status output instead of board-specific guesswork.
