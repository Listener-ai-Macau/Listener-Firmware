# Low-Power Hardware Shutdown Policy

Current V2/N16R8 firmware uses `components/power_manager` for runtime idle power reduction and long-idle hardware shutdown. The long-idle path no longer uses ESP32-S3 Deep Sleep. Instead, battery-only long idle enters `POWER_MANAGER_STATE_HARDWARE_SHUTDOWN`, records diagnostics, prepares BLE/audio/LED shutdown, and releases `PWR_HOLD/GPIO11` HIGH through the board abstraction. `EC11-KEY_IO/GPIO18` remains the active runtime recording key.

Observable firmware contract:

- `~POWER:STATUS` reports thresholds, blockers, `shutdown_blockers`, USB/charger raw levels, interpreted `external_power_present`, `charging`, `charge_full`, current battery, `last_shutdown_*`, `PWR_HOLD/GPIO11` status, `voice_key_gpio`, and the cold-boot user action.
- Shutdown entry and rejected shutdown attempts log power diagnostics with blocker or reason fields. Charging/USB automatic shutdown blocks also log `DIAG_POWER_EXTERNAL_POWER`, so exported diag logs can distinguish external-power policy from other blockers.
- Automatic long-idle hardware shutdown keeps the 30 minute V2 default threshold (`CONFIG_POWER_MANAGER_HARDWARE_SHUTDOWN_MS=1800000`) and uses only the board-level `PWR_HOLD/GPIO11` active-low latch release. No ESP sleep source is configured.
- USB or charger/full detection blocks inactivity-triggered automatic hardware shutdown and keeps the firmware in awake `ACTIVE` behavior instead of connected/disconnected low-power idle. The explicit `~POWER:SHUTDOWN` command remains the manual hardware-shutdown request and records `manual_command`.
- `CONFIG_PM_ENABLE`, `CONFIG_FREERTOS_USE_TICKLESS_IDLE`, and the connected/disconnected idle states remain the short-idle low-power behavior. They do not replace the long-idle hardware shutdown path.

## Production Requirement

`PWR_HOLD/GPIO11` must remain LOW during normal boot and runtime. Long-idle shutdown may release it HIGH only after diagnostics, BLE disconnect preparation, I2S idle power-save, LED shutdown preparation, and a final blocker/power-source check.

Real confirmation that `PWR_HOLD/GPIO11` HIGH fully removes power, that short press cold-boots the device afterward, and that USB/charging blocks automatic shutdown belongs to the hardware validation gate. Static firmware checks must not claim real power-off success.

The policy is intentionally centralized in `power_manager` so desktop diagnostics and future board revisions can key off one stable status output instead of board-specific guesswork.
