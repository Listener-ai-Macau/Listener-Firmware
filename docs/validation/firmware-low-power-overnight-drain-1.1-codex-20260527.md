# firmware-low-power-overnight-drain / 1.1 Codex continuation

Date: 2026-05-27
Agent: codex
Branch: ai/codex-firmware-low-power-overnight-drain-1.1
Firmware HEAD: 8f764cd
Hardware: COM5, BLE-14C19F48FE72

## Result

Blocked, not review-ready.

The current firmware builds, flashes, and reports the expected low-power policy on COM5. Existing hardware evidence from earlier 1.1 work covers the battery overnight run, 15 minute auto sleep, KEY4/GPIO21 EXT1 wake, and several post-wake regressions. The remaining blocker is the Windows BLE/HID host state: the PC no longer enumerates the Listener device via `Get-PnpDevice -Class Bluetooth`, and fresh HID/BLE product regression cannot be honestly rerun from this checkout until the host pairing/device interface is restored.

## Checks run in this continuation

- PASS: `git diff --check`
- PASS: `python -m compileall -q tools`
- PASS: `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
- PASS: `pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1`
- PASS: `pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1`
- PASS: `pwsh -NoProfile -File .\tools\cmake_build.ps1`
  - app version: `8f764cd`
  - app binary size: `0xa4170`
  - smallest app partition: `0x1b0000`
  - free app partition space: `0x10be90` (62%)
- PASS: `pwsh -NoProfile -File .\tools\flash.ps1 -Port COM5`
  - flashed bootloader, partition table, otadata, and app to COM5
  - ESP32-S3 MAC: `14:c1:9f:48:fe:70`
- PASS: post-flash serial status capture:
  - artifact: `tests/artifacts/firmware_low_power_codex_20260527-2330/post_flash_power_status_serial.log`
  - boot loaded `ota_0` from `0x20000`
  - app version `8f764cd`
  - ESP power management enabled: max 160 MHz, min 40 MHz, light sleep enabled
  - `~POWER:STATUS` reported `blockers=0x00000000`, `blocker_names=none`, `battery_mv=4126`, `battery_level=94`
  - thresholds: `audio_idle_ms=5000`, `connected_idle_ms=30000`, `disconnected_idle_ms=30000`, `overnight_sleep_ms=900000`
  - wake policy: `key4_only`, `wake_gpio_mask=0x0000000000200000`, `wake_capable_keys=KEY4/GPIO21`
  - voice key limitation reported: GPIO35 is not RTC deep-sleep wake capable on V1
  - `~OTA:STATUS` reported `active=0`, `pending_verify=0`, `blocker=none`

## Existing 1.1 evidence reviewed

Earlier artifacts still provide the long-duration and wake evidence required by the plan:

- 8 hour battery-only idle run:
  - `voice-keyboard-firmware-wt-oai-firmware-low-power-overnight-drain-1.1/tests/artifacts/firmware_low_power_battery_drain_20260526-231837`
  - start: `2026-05-26T23:18:37+08:00`, `4078mV`, `90%`
  - sleep entry: `idle_ms=28801629`, `4008mV`, `84%`, reason `overnight_idle`
  - post reconnect: battery was not drained
- KEY4/GPIO21 manual deep-sleep wake:
  - `voice-keyboard-firmware-wt-oai-firmware-low-power-overnight-drain-1.1/tests/artifacts/firmware_low_power_key4_wake_20260527-0808`
  - post wake reported `last_wake_source=ext1`, `wake_gpio_mask=0x0000000000200000`
- 15 minute automatic sleep and product wake validation:
  - `voice-keyboard-firmware-wt-oai-firmware-low-power-overnight-drain-1.1/tests/artifacts/low_power_wake_product_validation_20260527-204851/validation_summary.md`
  - COM5 disappeared about 15m37s after idle start
  - KEY4/GPIO21 wake returned COM5
  - post-wake `~POWER:STATUS` reported `last_sleep_reason=overnight_idle`, `last_wake_source=ext1`, and GPIO21 wake mask
  - recorded post-wake HID, BLE audio transport, cancel/recover, and disconnect/reconnect transport evidence, with residual desktop product-chain flakiness called out separately

## Current blocker

Fresh HID/BLE regression from this checkout is blocked by the Windows host Bluetooth state.

- `Get-PnpDevice -Class Bluetooth` did not enumerate `listener` or `14C19F48FE72`.
- `pwsh -NoProfile -File .\tools\verify_ble_hid.ps1 -Port COM5 -Text lpwake13 -BootCaptureSeconds 15 -PostSendCaptureSeconds 5` failed before acceptance evidence with `missing boot marker 'ble_hid: START'`; it did not produce a usable HID PASS artifact.
- This matches the adjacent OTA 1.4 investigation: Windows address-direct WinRT can sometimes see GATT services, but the product pairing/device-interface layer is not healthy enough for release-gate BLE/HID validation.

## Next action

Restore the Windows BLE product connection first: Listener should appear in `Get-PnpDevice -Class Bluetooth`, the host should be paired/connected, audio notify CCCD should write successfully, and firmware HID send logs should complete. Then rerun:

- `pwsh -NoProfile -File .\tools\verify_ble_hid.ps1 -Port COM5`
- BLE audio start/stop/cancel regression
- disconnect/reconnect recovery regression
- final `~POWER:STATUS` and `~DIAGLOG:LAST` capture

Only after that should this step be submitted for review.

## OAI follow-up - 2026-05-28

- PASS: changed the default overnight deep-sleep threshold from `900000 ms` to `1800000 ms` so automatic deep sleep starts after 30 minutes of no activity and no blockers.
- PASS: confirmed the branch already contains the timer-wake fix: `power_manager_configure_wakeup()` clears stale wake sources with `esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)` before enabling KEY4/GPIO21 EXT1.
- PASS: built and flashed the 30 minute firmware to COM5 from the main firmware checkout. The live device reported:
  - app version `v1002.0.0-ota-test-9-gcbbf239-d`
  - `overnight_sleep_ms=1800000`
  - `wake_policy=key4_only`
  - `wake_gpio_mask=0x0000000000200000`
- PASS: manual sleep/wake spot check on 2026-05-28:
  - sent `~POWER:SLEEP`; COM5 disappeared, consistent with deep sleep
  - user pressed KEY4/GPIO21; COM5 returned
  - persistent diagnostics preserved the wake record: `power evt=3 a1=2 a2=2097152 a3=2`
  - `a1=2` is EXT1, `a2=2097152` is `1 << 21` / GPIO21, and `a3=2` is manual command sleep
- NOTE: opening COM5 after the wake still triggers a USB-Serial/JTAG reset on this board/host path, so the immediate text boot log shows `USB_UART_CHIP_RESET` / `power_on`. The persistent diag_log record above is the reliable evidence for the KEY4/EXT1 wake.
- Remaining: a no-touch soak after the 30 minute build is still needed to prove it does not self-wake by timer after automatic overnight sleep. Leave the device idle for more than 30 minutes, then confirm COM5 stays absent until KEY4/GPIO21 is pressed and persistent diagnostics show EXT1/GPIO21 rather than timer.

## OAI follow-up - 2026-05-29

- Finding: the 2026-05-29 morning persistent diag capture did not show an overnight deep-sleep wake. Instead, the newest retained diagnostics were repeated BLE GAP/HID connect, encryption-failure, disconnect, and reconnect events around 8.97 hours of uptime. This means the device remained awake rather than sleeping and waking by KEY4.
- Fix: split power-manager idle tracking into user idle and radio idle:
  - `user_idle_ms` drives the 30 minute overnight deep-sleep guard.
  - `radio_idle_ms` drives connected/disconnected BLE low-power states.
  - BLE connect/disconnect updates only radio activity and no longer resets the overnight user-idle clock.
  - `~POWER:STATUS` now reports both `user_idle_ms` and `radio_idle_ms` while keeping legacy `idle_ms` mapped to user idle.
- Fix: removed `power_manager_record_activity("ble_connect")` and `power_manager_record_activity("ble_disconnect")` from the BLE HID event path so BLE churn cannot bypass the split idle clocks.
- PASS: `python tools\verify_power_manager_static.py`
- PASS: `pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1`
- PASS: `pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1`
- PASS: `git diff --check`
- PASS: `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
  - app binary size: `0xa4300`
  - smallest app partition: `0x1b0000`
  - free app partition space: `0x10bd00` (62%)
- Remaining: flash this build and rerun a no-touch soak for at least 30 minutes with a nearby host still allowed to attempt BLE reconnects. Expected result: COM5 disappears after overnight idle, stays absent without timer self-wake, then KEY4/GPIO21 returns COM5 and persistent diagnostics show EXT1/GPIO21.
