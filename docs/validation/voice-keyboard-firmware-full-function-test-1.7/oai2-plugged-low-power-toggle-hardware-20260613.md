# OAI2 Plugged Low-Power Toggle Hardware Evidence

Date: 2026-06-13

Scope: verify that `plugged_low_power_enabled` changes real firmware power-manager behavior on V2 hardware while USB/external power is present.

Method:

- Flashed the current firmware to COM7.
- Held the COM7 workflow lock during the serial test.
- Kept Listener Type closed during the test so BLE diagnostics would not reset the idle timer.
- Used `low_power_idle_ms=60000` and waited 70 seconds after `~POWER:ACTIVITY` for each case.
- Restored the original settings at the end: `low_power_idle_ms=60000 plugged_low_power_enabled=1`.

## Evidence

Switch on:

```text
~DEVICE:SETTINGS ... low_power_idle_ms=60000 ... plugged_low_power_enabled=1 ... external_power_present=1 usb_power_present=1
~POWER:STATUS state=DISCONNECTED_IDLE ... external_power_present=1 usb_power_present=1 ... plugged_low_power_enabled=1 low_power_idle_allowed=1 ... audio_idle_power_save=1
```

Switch off:

```text
~DEVICE:SETTINGS ... low_power_idle_ms=60000 ... plugged_low_power_enabled=0 ... external_power_present=1 usb_power_present=1
~POWER:STATUS state=ACTIVE ... external_power_present=1 usb_power_present=1 ... plugged_low_power_enabled=0 low_power_idle_allowed=0 ... audio_idle_power_save=1
```

Result:

```text
PASS: plugged low-power switch verified on hardware. on_state=DISCONNECTED_IDLE off_state=ACTIVE restored_low_power_idle_ms=60000 restored_plugged_low_power_enabled=1
```

## Interpretation

The switch is not just a UI label. With external power physically detected, enabling it lets the device enter low-power idle; disabling it keeps the device awake while plugged in. Battery-mode low-power and battery-only shutdown policy are unchanged.
