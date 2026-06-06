# voice-keyboard-v2-firmware-board-migration/1.4 partial validation

Agent: oai2
Date: 2026-06-06
Worktree: `C:\Users\Billy\Desktop\listener\voice-keyboard-firmware-wt-oai2-voice-keyboard-v2-firmware-board-migration-1.4`

## Status

BLOCKED: V2/N16R8 real-device validation could not be completed because `COM6` and `BLE-E99FCE38CC0D` became locked by `oai1` before the successful flash retry could run.

Do not submit this step from this artifact alone. It is partial evidence for the blocked state and the next operator should rerun the hardware validation commands after the resource lock is clear.

## Hardware Found

- Serial port: `COM6`
- USB device: `USB JTAG/serial debug unit`, `VID_303A&PID_1001`
- BLE device: `listener`, address `E99FCE38CC0D`

## Commands Run

### Initial flash command

```powershell
pwsh -NoProfile -File ..\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COM6,BLE-E99FCE38CC0D -TimeoutMinutes 30 -Run pwsh -NoProfile -File .\tools\flash.ps1 -Port COM6
```

Result: FAIL before flashing. The command acquired and released both locks correctly, but ESP-IDF build failed in the long step worktree path with a Windows object/dependency file path-length failure:

```text
fatal error: opening dependency file esp-idf\espressif__esp_io_expander_tca95xx_16bit\CMakeFiles\__idf_espressif__esp_io_expander_tca95xx_16bit.dir\esp_io_expander_tca95xx_16bit.c.obj.d: No such file or directory
```

This is an environment/path-length failure, not evidence of firmware or board failure.

### Short build-dir retry

```powershell
$env:LISTENER_IDF_BUILD_DIR = Join-Path $env:TEMP 'listener-idf-build-v2-1-4-oai2'
pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3
```

Result: PASS.

Key evidence:

- ESP-IDF build directory: `C:\Users\Billy\AppData\Local\Temp\listener-idf-build-v2-1-4-oai2`
- App version: `v1002.0.0-ota-test-120-g3eb0e13`
- Flash size in generated flash command: `16MB`
- Partition table includes `ota_0` 6 MB, `ota_1` 6 MB, and `diag_log` 1 MB.
- App binary: `voice-keyboard-firmware.bin`, size `0xbec00`; smallest app partition `0x600000`, 88% free.

### Flash retry blocked

```powershell
pwsh -NoProfile -File ..\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COM6,BLE-E99FCE38CC0D -TimeoutMinutes 20 -Run pwsh -NoProfile -Command ". .\tools\idf_env.ps1 -Target esp32s3; idf.py -B '$env:TEMP\listener-idf-build-v2-1-4-oai2' -p COM6 flash"
```

Result: BLOCKED by workflow locks:

```text
Resource 'BLE-E99FCE38CC0D' is locked by 'oai1' until 2026-06-06 19:34:13
```

`aiw locks` then showed both `COM6` and `BLE-E99FCE38CC0D` active under `oai1`. Because flashing resets the same physical board and disrupts BLE, oai2 did not bypass or narrow the lock.

## Remaining Required Validation

After `COM6` and `BLE-E99FCE38CC0D` are clear, rerun under workflow lock:

```powershell
$env:LISTENER_IDF_BUILD_DIR = Join-Path $env:TEMP 'listener-idf-build-v2-1-4-oai2'
pwsh -NoProfile -File ..\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COM6,BLE-E99FCE38CC0D -TimeoutMinutes 45 -Run pwsh -NoProfile -Command ". .\tools\idf_env.ps1 -Target esp32s3; idf.py -B '$env:TEMP\listener-idf-build-v2-1-4-oai2' -p COM6 flash"
pwsh -NoProfile -File ..\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COM6 -TimeoutMinutes 10 -Run pwsh -NoProfile -File .\tools\inspect_firmware_log.ps1 -Port COM6 -LiveSeconds 20 -Tail 120 -Events 120
pwsh -NoProfile -File ..\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COM6 -TimeoutMinutes 10 -Run pwsh -NoProfile -File .\tools\dump_diag_log.ps1 -Port COM6 -OutputDir tests\artifacts\voice_keyboard_v2_firmware_board_migration_1_4
pwsh -NoProfile -File ..\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COM6,BLE-E99FCE38CC0D -TimeoutMinutes 15 -Run pwsh -NoProfile -File .\tools\verify_ble_hid.ps1 -Port COM6 -Text v2hid14 -BootCaptureSeconds 10 -PostSendCaptureSeconds 5
git diff --check
```

Then capture the full V2/N16R8 boot, flash/PSRAM, pin map, power telemetry, LED, sleep/wake, OTA/watchdog/recovery, and any physical measurement evidence into `docs/validation/voice-keyboard-v2-firmware-board-migration-1.4.md`, run `aiw run-validation`, and submit only if every required acceptance item has PASS or an explicit accepted blocker/follow-up.
