# firmware-watchdog-boot-safety 1.1 validation

Date: 2026-05-28
Agent: oai1
Branch: ai/oai1-firmware-watchdog-boot-safety-1.1
Commit: cf76aaa
Device: ESP32-S3 on COM5, USB-Serial-JTAG, MAC 14:c1:9f:48:fe:70

## Commands

- `pwsh -NoProfile -File .\tools\verify_watchdog_boot_safety_static.ps1`
- `git diff --check`
- `python -m compileall -q tools`
- `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
- `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
- `pwsh -NoProfile -File .\tools\flash.ps1 -Port COM5 -Target esp32s3`
- USB serial `~WDT:STATUS`
- USB serial `~WDT:DEADLOCK`

## Results

- Static watchdog checks passed.
- Build passed for ESP32-S3. Binary size: `0xa4670`; smallest app partition: `0x1b0000`; 62% free.
- Flash passed over COM5 without BOOT or RESET button interaction. `esptool.py` detected ESP32-S3 and verified all written segments.
- Boot log after forced reconfigure reports `App version: cf76aaa` and `fw_version=cf76aaa`.
- Runtime watchdog status reports `task_wdt=1 init=1 panic=1 timeout_s=5 int_wdt=1 int_timeout_ms=300`.
- Normal boot subscribes core runtime tasks to Task WDT, including BLE audio, audio capture, voice key input, voice recording control, keyboard, health, power manager, BLE HID battery, and BLE HID keyboard tasks.
- `~WDT:DEADLOCK` was accepted by firmware and triggered Task WDT panic after approximately 5.8 seconds.
- Device rebooted automatically after the watchdog panic and returned to normal boot with `fw_version=cf76aaa`.

## Key Evidence

- `tests/artifacts/firmware_watchdog_boot_safety_20260528-085608/cf76aaa_wdt_status.log`
  - `App version:      cf76aaa`
  - `watchdog: config: task_wdt=1 init=1 panic=1 timeout_s=5 int_wdt=1 int_timeout_ms=300`
  - `ble_hid: USB SERIAL INPUT READY`
- `tests/artifacts/firmware_watchdog_boot_safety_20260528-085632/cf76aaa_wdt_deadlock_reset.log`
  - `watchdog: WDT DEADLOCK test command accepted; spinning without feed`
  - `task_wdt: Task watchdog got triggered`
  - `task_wdt: Aborting.`
  - `Rebooting...`
  - post-reboot `App version:      cf76aaa`

## Note On Low Power Observation

During this validation, the observed boot source was not deep-sleep KEY4/EXT1 wake. The captured boot log reports `power wake status: wake_source=0 wake_gpio_mask_low=0x00000000` and `wake=power_on`.
