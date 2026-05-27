# firmware-watchdog-boot-safety 1.1 validation

Date: 2026-05-27
Agent: tai
Repo: voice-keyboard-firmware
Branch: ai/tai-firmware-watchdog-boot-safety-1.1

## Scope

Enabled firmware watchdog boot safety for the ESP32-S3 product firmware.

- `sdkconfig.defaults` and `sdkconfig.defaults.esp32s3` enable Task WDT with panic and a 5 second timeout.
- `sdkconfig.defaults` and `sdkconfig.defaults.esp32s3` enable Interrupt WDT with a 300 ms timeout.
- Added the `watchdog_platform` ESP32 component as a small wrapper around `esp_task_wdt_add(NULL)` and `esp_task_wdt_reset()`.
- Registered and fed the main firmware long-running tasks:
  - BLE HID USB command task
  - BLE HID battery task
  - BLE audio stream task
  - audio capture task
  - WASD keyboard scan task
  - voice key input task
  - voice recording control task
  - power manager task
  - system health task
- Long waits in battery, health, power manager, and BLE audio queue/delay paths are bounded or split through the watchdog wrapper so normal idle periods do not trip the 5 second Task WDT.
- `app_main` logs the active watchdog configuration during boot; existing reset-reason diagnostics already map `ESP_RST_TASK_WDT` and `ESP_RST_INT_WDT` to `DIAG_BOOT_WATCHDOG`.
- Added `tools/verify_watchdog_boot_safety_static.ps1` to make the WDT config and task subscription coverage reviewable without hardware.

## Config Evidence

After `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`, generated `sdkconfig` contained:

- `CONFIG_ESP_TASK_WDT_EN=y`
- `CONFIG_ESP_TASK_WDT_INIT=y`
- `CONFIG_ESP_TASK_WDT_PANIC=y`
- `CONFIG_ESP_TASK_WDT_TIMEOUT_S=5`
- `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y`
- `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y`
- `CONFIG_ESP_INT_WDT=y`
- `CONFIG_ESP_INT_WDT_TIMEOUT_MS=300`
- `CONFIG_ESP_INT_WDT_CHECK_CPU1=y`

## Validation

Passed:

- `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
- `pwsh -NoProfile -File .\tools\verify_watchdog_boot_safety_static.ps1`
- `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
- `python -m compileall -q tools`
- `git diff --check`
- `pwsh -NoProfile -File ..\ai-collaboration-workflow\scripts\aiw.ps1 validate -Plan firmware-watchdog-boot-safety`

## Hardware Boundary

Hardware validation was not run in this implementation step because the workflow hardware locks were active:

- `COM5` owner: `codex`
- `BLE-14C19F48FE72` owner: `codex`

Deferred hardware evidence:

- `idf.py flash monitor` normal-run proof that watchdog does not trip during ordinary BLE/audio/keyboard startup.
- Injected deadlock proof that `Task watchdog got triggered` appears and the device reboots within 5 seconds.
- `esptool.py --chip esp32s3 -p COMx chip_id` no-button USB-Serial-JTAG recovery proof.
