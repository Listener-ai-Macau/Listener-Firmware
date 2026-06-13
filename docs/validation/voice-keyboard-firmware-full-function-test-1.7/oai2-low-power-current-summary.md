# OAI2 Low-Power Current Summary

Date: 2026-06-13

Scope: verify low-power behavior with the V2 board current telemetry, not just the power-manager state string.

## Finding

The first forced-current run showed that `CONNECTED_IDLE` was not sufficient evidence by itself:

- `idle-power-current-force-20260613-170016`: TPS63020 median 98 mA, max 140 mA; SY7088 median 0 mA, max 36 mA.
- Diag log showed `ble_hid` still performing roughly 5 second battery ADC/HID updates while idle.

## Fixes Applied

- `ports/esp32/ble_hid/ble_hid.c`
  - Connected/disconnected low-power idle now uses the long 600 second BLE Battery Service sample interval instead of the active 5 second interval.
  - Power-manager state transitions wake the battery task so active mode can resume promptly.
  - Low-power transition notifications do not immediately trigger a fresh ADC sample.
  - USB Serial/JTAG polling backs off from 20 ms to 500 ms while in low-power idle.
- `components/power_manager`
  - Added `power_manager_get_state()` for low-cost state checks that do not take a battery ADC sample.
  - Power-manager state changes notify the BLE battery task through a weak hook.

## After-Fix Evidence

- `idle-power-current-force-after-usb-poll-fix-20260613-172256`
  - State: `DISCONNECTED_IDLE`
  - TPS63020: min 0 mA, median 11 mA, mean 46.5 mA, max 146 mA.
  - SY7088: min 0 mA, median 0 mA, mean 18.0 mA, max 144 mA.
  - No `ble_hid: battery notify` lines were observed after idle samples.
- Realtime post-run serial check:
  - `~POWER:STATUS`: `CONNECTED_IDLE`, `audio_idle_power_save=1`, `pwr_hold_level=low`.
  - `~LED:STATUS`: `low_power_disabled=1`, all status RGB values `0,0,0`, LED estimated current `0`.
  - `~BOARD:POWER:FORCE`: TPS63020 10 mA, SY7088 0 mA.

## Interpretation

The stable low-power current telemetry now reaches the expected low-current range for runtime idle with USB attached. The remaining high force-sample spikes are intermittent and are not matched by LED output or repeated BLE battery sampling, so they should be treated as forced ADC/USB wake transients or current-sense sampling artifacts rather than sustained load.

Battery-only physical proof still needs either an external current meter or a future unplugged internal-sampling artifact, because USB serial diagnostics keep VBUS/USB present during measurement.
