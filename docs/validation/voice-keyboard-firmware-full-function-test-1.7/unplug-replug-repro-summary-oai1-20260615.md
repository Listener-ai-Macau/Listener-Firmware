# USB Unplug/Replug Flash Diagnostic Repro Summary

Step 1.7 root-cause validation now has two independent USB-unplug/no-serial flash diagnostic captures that pass the verifier with battery display latch enabled.

## Captures

- Repro 1:
  `docs/validation/voice-keyboard-firmware-full-function-test-1.7/post-unplug-replug-oai1-20260615-145041/`
  - `verify_unplugged_flash_diag_bundle.py` PASS
  - PWR classes: `amber`, `off`, `white`; `green` forbidden and absent
  - Timelines present: `status_led.power_input`, `status_led.visual_state`, `status_led.output_state`, `power.external_power`, `power.sleep_wake`
- Repro 2:
  `docs/validation/voice-keyboard-firmware-full-function-test-1.7/post-unplug-replug-oai1-20260615-162625/`
  - `verify_unplugged_flash_diag_bundle.py` PASS
  - PWR classes: `amber`, `off`, `white`; `green` forbidden and absent
  - Timelines present: `status_led.power_input`, `status_led.visual_state`, `status_led.output_state`, `power.external_power`, `power.sleep_wake`

## Interpretation

The flash-backed diagnostic bundles reconstruct the unplugged interval after USB serial is unavailable: external power becomes false, low-power LED output disables, PWR visual state reaches off, and visible PWR state resumes after reconnect without battery-only green flicker. Raw battery readings and display-latched battery level are preserved in `status_led.power_input`.

This evidence covers the USB-unplug/no-serial light-cycle behavior. It does not claim real PWR_HOLD hardware power removal; that route remains documented in `pwr-hold-known-issue-oai1-20260615.md`.

reproducibility:2_pass_out_of_2_runs
