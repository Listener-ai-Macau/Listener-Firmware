# voice-keyboard-ota-update/1.1 validation

Date: 2026-05-26
Assignee: Tai
Device: ESP32-S3 on COM5, MAC 14:c1:9f:48:fe:70

## Automated validation

- `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`: PASS
  - `voice-keyboard-firmware.bin` size: `0x9a630`
  - smallest OTA app partition: `0x1b0000`
  - free space: `0x1159d0` (64%)
- `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`: PASS
- `python -m compileall -q tools`: PASS
- `git diff --check`: PASS
- static check: PASS, confirmed ESP-IDF OTA/rollback APIs and config:
  - `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`
  - `esp_ota_get_next_update_partition`
  - `esp_ota_begin`
  - `esp_ota_write`
  - `esp_ota_end`
  - `esp_ota_set_boot_partition`
  - `esp_ota_mark_app_valid_cancel_rollback`
  - `esp_ota_mark_app_invalid_rollback_and_reboot`

## Hardware validation

- `pwsh -NoProfile -File .\tools\flash.ps1 -Port COM5 -Target esp32s3`: PASS
- Clean boot log: `tests/artifacts/ota_update_1_1/final_clean_boot_ota_status_com5.log`
  - Bootloader partition table shows `otadata`, `ota_0`, `ota_1`, and `diag_log`.
  - Firmware booted from `ota_0`.
  - `~OTA:STATUS` returned `running=ota_0 boot=ota_0 update=ota_1 active=0 pending_verify=0 state=2 blocker=none`.
  - `~OTA:BLOCKER` returned `none`.
  - `~DIAGLOG:LAST:20` included `src="ota"` state event.
- Pending verify log: `tests/artifacts/ota_update_1_1/final_pending_verify_mark_valid_com5.log`
  - Wrote current app image to inactive `ota_1` at `0x1d0000`.
  - Triggered internal factory validation command `~OTA:TEST_BOOT_INACTIVE`.
  - Bootloader loaded app from `0x1d0000`.
  - Firmware logged `OTA running partition=ota_1 boot=ota_1 state=1`.
  - POST, BLE init/start, and keyboard start passed.
  - Firmware called `esp_ota_mark_app_valid_cancel_rollback()` and logged `OTA pending verify accepted for partition=ota_1`.
  - Final `~OTA:STATUS` returned `running=ota_1 boot=ota_1 update=ota_0 active=0 pending_verify=0 state=2 blocker=none`.
  - `~DIAGLOG:LAST:30` included OTA set-boot, reboot, pending-verify, and mark-valid events.

## Notes

- The full firmware app is now an OTA-slot image; factory flashing writes it to `ota_0` and initializes `otadata`.
- `diag_log` remains 512 KiB at `0x380000`.
- USB OTA commands are limited to diagnostics/control (`~OTA:STATUS`, `~OTA:BLOCKER`, `~OTA:ABORT`) plus the internal hardware validation command `~OTA:TEST_BOOT_INACTIVE`; OTA write/finish remains a firmware C API for the later BLE transfer step.
- The build still shows the pre-existing `AUDIO_CAPTURE_TASK_STACK_BYTES` redefinition warning; this step did not introduce it.
