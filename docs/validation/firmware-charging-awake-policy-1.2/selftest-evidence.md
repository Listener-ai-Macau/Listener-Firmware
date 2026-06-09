# Charging Awake Hardware Evidence

status=PASS
hardware_mode=self-test
port=
observe_seconds=20

## Evidence
- PASS: board status identifies V2/N16R8 target
- PASS: PWR_HOLD/GPIO11 is held low during runtime and releases high for hardware shutdown
- PASS: power status response is present
- PASS: power status reports hardware-shutdown threshold
- PASS: power status uses EC11/GPIO18 voice-key contract
- PASS: KEY1-KEY4 and EC11 key GPIO status are responsive
- PASS: EC11 diagnostic command path is responsive
- PASS: diagnostic log tail command path is responsive
- PASS: final power status reports external_power_present=1
- PASS: final power status reports usb_power_present=1
- PASS: final power state is not HARDWARE_SHUTDOWN under external power

## Manual Gates
- Long-idle threshold was not reached by this short run: user_idle_ms=54000, hardware_shutdown_ms=1800000. Use a plugged-in soak beyond the threshold for final awake proof.
- Manual hardware-shutdown override was not executed. Re-run with -IncludeManualShutdown only when the operator is ready for a destructive power-off/recovery check.
- Battery-only long-idle hardware-shutdown evidence requires an unplugged/battery run and cold-boot recovery artifact; serial-over-USB capture cannot prove this while USB power is attached.
- Post-unplug stale-idle validation requires a physical charging/USB soak, unplug action, and follow-up power diagnostics.

## Serial Transcript
```text
> ~BOARD:STATUS
~BOARD:STATUS profile=voice-keyboard-v2-n16r8 module=ESP32-S3-WROOM-1-N16R8 flash_mb=16 psram_mb=8 psram_mode=octal key_gpios=38,39,40,41 ec11_a_gpio=42 ec11_b_gpio=2 ec11_key_gpio=18 mic_clk_gpio=48 mic_dout_gpio=47 pwr_hold_gpio=11 pwr_hold_level=low pwr_hold_configured=1 pwr_hold_policy=v2_gpio11_power_latch_hold_low_release_high_for_hardware_shutdown usb_det_gpio=7 usb_det_level=high bat_chg_gpio=14 bat_chg_level=high bat_std_gpio=21 bat_std_level=low charger_polarity=v2_gpio14_chg_gpio21_std_active_low battery_gpio=8 battery_mv=4084 battery_level=92 battery_valid=1
> ~POWER:STATUS
~POWER:STATUS state=CONNECTED_IDLE blockers=0x00000080 blocker_names=external_power shutdown_blockers=0x00000080 shutdown_blocker_names=external_power idle_ms=32000 user_idle_ms=32000 radio_idle_ms=32000 ble_connected=1 automatic_shutdown_blocked_by_external_power=0 external_power_present=1 usb_power_present=1 charging=0 charge_full=1 usb_det_level=high bat_chg_level=high bat_std_level=low usb_det_policy=v2_gpio7_r37_r32_10K_10K_divider charger_polarity=v2_gpio14_chg_gpio21_std_active_low battery_mv=4084 battery_level=92 battery_valid=1 last_shutdown_reason=none last_shutdown_idle_ms=0 last_shutdown_blockers=0x00000000 guard=1 audio_idle_ms=5000 connected_idle_ms=30000 disconnected_idle_ms=30000 hardware_shutdown_ms=1800000 pwr_hold_gpio=11 pwr_hold_level=low pwr_hold_configured=1 pwr_hold_policy=v2_gpio11_power_latch_hold_low_release_high_for_hardware_shutdown voice_key_gpio=18 hardware_shutdown_user_action="short-press hardware power key for cold boot after PWR_HOLD/GPIO11 release-high"
> ~BOARD:GPIO
~BOARD:GPIO active_low=1 mode=read_as_configured reconfigure=0 key1_gpio=38 key1=high key2_gpio=39 key2=high key3_gpio=40 key3=high key4_gpio=41 key4=high ec11_a_gpio=42 ec11_a=high ec11_b_gpio=2 ec11_b=high ec11_key_gpio=18 ec11_key=high recording_key=EC11_KEY/GPIO18
> ~EC11:STATUS
EC11 rotation status: action=volume enabled=1
> ~DIAGLOG:LAST:64
DIAG EVENT source=power event=DIAG_POWER_EXTERNAL_POWER severity=info a1=5 a2=5 a3=32000 a4=128
> observe-usb-awake seconds=20
> ~POWER:STATUS
~POWER:STATUS state=CONNECTED_IDLE blockers=0x00000080 blocker_names=external_power shutdown_blockers=0x00000080 shutdown_blocker_names=external_power idle_ms=54000 user_idle_ms=54000 radio_idle_ms=54000 ble_connected=1 automatic_shutdown_blocked_by_external_power=0 external_power_present=1 usb_power_present=1 charging=0 charge_full=1 usb_det_level=high bat_chg_level=high bat_std_level=low usb_det_policy=v2_gpio7_r37_r32_10K_10K_divider charger_polarity=v2_gpio14_chg_gpio21_std_active_low battery_mv=4084 battery_level=92 battery_valid=1 last_shutdown_reason=none last_shutdown_idle_ms=0 last_shutdown_blockers=0x00000000 guard=1 audio_idle_ms=5000 connected_idle_ms=30000 disconnected_idle_ms=30000 hardware_shutdown_ms=1800000 pwr_hold_gpio=11 pwr_hold_level=low pwr_hold_configured=1 pwr_hold_policy=v2_gpio11_power_latch_hold_low_release_high_for_hardware_shutdown voice_key_gpio=18
```
