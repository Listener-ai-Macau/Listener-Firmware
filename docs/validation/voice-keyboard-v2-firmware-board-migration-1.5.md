# voice-keyboard-v2-firmware-board-migration / 1.5

Agent: oai2
Date: 2026-06-08
Repo: voice-keyboard-firmware
Commit: 95bd31d
CWD: C:\Users\Billy\Desktop\listener\voice-keyboard-firmware
Status: WAITING on hardware boot/serial response; not submitted.

## Initial State

- Existing diff at start: none (`git status --short --untracked-files=all`, `git diff --stat`, `git diff --name-status`, and `git diff --check` were clean for current tracked firmware changes).
- Workflow step: `voice-keyboard-v2-firmware-board-migration/1.5`, owner `oai2`, `coordination_only`, `requires_hardware=true`.
- Previous card blocker: hardware lock was held by another agent; recheck showed `No resource locks found` before hardware actions.
- 1.4 mapping used for hardware resources: `BLE-ADDR -> BLE-A4CB8FF459A6`; `COMx` resolved at action time.

## No-Lock Validation Completed

| Command | Result | Evidence |
|---|---|---|
| `python .\tools\verify_status_led_static.py` | PASS | Covers V2 four-zone WS2812 resources, EC11 GPIO5/count12, key GPIO13/count4, edge GPIO4/count6, diagnostics, USB validation hooks, camera one-pixel status/key harness, and low-power off path. |
| `pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1` | PASS | Covers hardware shutdown, PWR_HOLD/GPIO46, external-power blockers, idle actions, diagnostics, and Deep Sleep removal. |
| `pwsh -NoProfile -File .\tools\verify_v2_board_profile_static.ps1` | PASS | Covers V2 N16R8 board profile, pin map, four-zone LED resources, SY7088/TPS63020 battery-side current telemetry, USB_Det divider policy, diagnostics, partitions, and package identity. |
| `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3` | PASS | Built `voice-keyboard-firmware.bin` size `0xc0a80`; smallest app partition `0x600000`; `87%` free. |
| `pwsh -NoProfile -File .\tools\collect_v2_current_telemetry.ps1 -SelfTest` | PASS | Parser self-test passed for TPS63020/SY7088 current telemetry and `~POWER:STATUS` parsing. |
| `pwsh -NoProfile -File .\tools\verify_ble_hid.ps1` | PASS | Static BLE HID checks cover Battery Service live reporting, reconnect refresh, and ADC diagnostics. |

## Hardware Validation Attempt

### Flash validation

Command run under short hardware lock:

```powershell
pwsh -NoProfile -File ..\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COMx,BLE-A4CB8FF459A6 -Run pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
```

Result: FAIL, hardware/serial gate. `aiw with-lock` resolved `COMx -> COM6`, locked `BLE-A4CB8FF459A6` and `COM6`, and released both. Build portion passed, but flash failed:

```text
A fatal error occurred: Failed to connect to ESP32-S3: No serial data received.
idf.py -B C:\Users\Billy\Desktop\listener\voice-keyboard-firmware\build -p COM6 flash failed with exit code 2
```

### Bounded serial and diagnostic capture

Command run under short `COMx` lock:

```powershell
pwsh -NoProfile -File ..\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COMx -TimeoutMinutes 10 -Run pwsh -NoProfile -File .\tests\artifacts\voice_keyboard_v2_firmware_board_migration_1_5\run_locked_serial_diag.ps1 -Port COMx
```

Artifacts:

- `tests/artifacts/voice_keyboard_v2_firmware_board_migration_1_5/locked_serial_diag_results.json`
- `tests/artifacts/voice_keyboard_v2_firmware_board_migration_1_5/serial_monitor_bounded.log`
- `tests/artifacts/voice_keyboard_v2_firmware_board_migration_1_5/dump_diag_log.log`
- `tests/artifacts/voice_keyboard_v2_firmware_board_migration_1_5/v2_current_telemetry.log`
- `tests/artifacts/voice_keyboard_v2_firmware_board_migration_1_5/v2_current_telemetry/v2_current_telemetry_20260608-083715.json`
- `tests/artifacts/voice_keyboard_v2_firmware_board_migration_1_5/v2_current_telemetry/v2_current_telemetry_20260608-083715.md`

Sub-results:

- `inspect_firmware_log.ps1 -Port COM6 -LiveSeconds 12`: exit 0, but captured `<no serial output>`.
- `dump_diag_log.ps1 -Port COM6 -Count 240 -ReadSeconds 12`: exit 0, but captured no diag_log events.
- `collect_v2_current_telemetry.ps1 -Port COM6`: exit 1 because `~BOARD:STATUS` / `~POWER:STATUS` responses were absent.

### esptool connection probe

Command run under short `COMx` lock:

```powershell
pwsh -NoProfile -File ..\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COMx -TimeoutMinutes 8 -Run pwsh -NoProfile -File .\tests\artifacts\voice_keyboard_v2_firmware_board_migration_1_5\run_locked_esptool_probe.ps1 -Port COMx
```

Artifacts:

- `tests/artifacts/voice_keyboard_v2_firmware_board_migration_1_5/esptool_probe_results.json`
- `tests/artifacts/voice_keyboard_v2_firmware_board_migration_1_5/esptool_read_mac_default_reset_115200.log`
- `tests/artifacts/voice_keyboard_v2_firmware_board_migration_1_5/esptool_read_mac_default_reset_460800.log`
- `tests/artifacts/voice_keyboard_v2_firmware_board_migration_1_5/esptool_read_mac_usb_reset_115200.log`
- `tests/artifacts/voice_keyboard_v2_firmware_board_migration_1_5/esptool_read_mac_usb_reset_460800.log`
- `tests/artifacts/voice_keyboard_v2_firmware_board_migration_1_5/esptool_read_mac_no_reset_115200.log`
- `tests/artifacts/voice_keyboard_v2_firmware_board_migration_1_5/esptool_read_mac_no_reset_460800.log`
- `tests/artifacts/voice_keyboard_v2_firmware_board_migration_1_5/esptool_read_mac_no_reset_no_sync_115200.log`
- `tests/artifacts/voice_keyboard_v2_firmware_board_migration_1_5/esptool_read_mac_no_reset_no_sync_460800.log`

Result: all 8 `read_mac` probes failed with exit code 2. `default_reset` and `usb_reset` reported `No serial data received`; `no_reset_no_sync` reported the chip stopped responding.

### Windows device evidence

Artifact: `tests/artifacts/voice_keyboard_v2_firmware_board_migration_1_5/com6_pnp.json`

COM6 is present and Windows reports it OK:

```text
Name: USB serial device (COM6)
DeviceID: USB\VID_303A&PID_1001&MI_00\7&2DA1C99B&0&0000
Manufacturer: Microsoft
Status: OK
```

## Current Residual Gate

The board enumerates as Espressif USB serial/JTAG on COM6, but neither the app serial command path nor the ESP32-S3 ROM loader responds. Because the validation flash command, bounded serial monitor, diag_log export, current telemetry command, and esptool connection probes all failed to receive serial data, AI cannot complete the final real-effect closure without a physical hardware action.

Required operator action before retry:

1. Keep this board connected as COM6 / Espressif USB serial/JTAG.
2. Put the ESP32-S3 into ROM bootloader manually: hold BOOT/GPIO0 low, press and release RESET/EN or power-cycle the board, then release BOOT after esptool starts connecting.
3. If manual bootloader entry still gives `No serial data received`, inspect hardware/repair around EN/RESET, BOOT/GPIO0 strap, USB serial/JTAG lines, board power/PWR_HOLD/GPIO46, 3V3, and any LED-repair short or loading on VDD_LED/SY7088 rails.
4. After the board responds to esptool, rerun the validation flash, bounded serial monitor, `dump_diag_log.ps1`, and `collect_v2_current_telemetry.ps1` commands. Then verify VDD_LED/SY7088, the four WS2812 zones, EC11 12-LED ring order/direction, key/status/edge LEDs, current telemetry, and command/diagnostic paths.

## Not Submitted

This step is not eligible for `aiw submit` because the hardware validation commands have not passed. The failure is classified as a hardware/external gate, not an AI-actionable build/test/tooling failure.
