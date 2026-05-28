# firmware-watchdog-boot-safety 1.2 validation

Date: 2026-05-28
Agent: oai1
Branch: ai/oai1-firmware-watchdog-boot-safety-1.2
Commit: 7cfd683
Device: ESP32-S3 on COM5, USB-Serial-JTAG, MAC 14:c1:9f:48:fe:70

## Commands

- `pwsh -NoProfile -File .\tools\verify_boot_safety_static.ps1`
- `pwsh -NoProfile -File .\tools\verify_watchdog_boot_safety_static.ps1`
- `git diff --check`
- `python -m compileall -q tools`
- `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
- `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
- `idf.py reconfigure`
- `idf.py build`
- `pwsh -NoProfile -File .\tools\flash.ps1 -Port COM5 -Target esp32s3`
- USB serial sequence: `~BOOT:CLEAR`, `~BOOT:CRASH`, wait 30 seconds, `~BOOT:STATUS`, `~BOOT:CLEAR`, `~BOOT:CRASH` three times, `~BOOT:CLEAR`

## Results

- Static boot safety checks passed.
- Watchdog static checks still passed.
- Build passed for ESP32-S3. Binary size: `0xa5200`; smallest app partition: `0x1b0000`; 62% free.
- Flash passed over COM5 without BOOT or RESET button interaction.
- Boot log after forced reconfigure reports `App version: 7cfd683` and `fw_version=7cfd683`.
- One `~BOOT:CRASH` restart reported `crash_count=1 safe_mode=0`.
- After 30 seconds of normal runtime, firmware logged `crash counter cleared`; `~BOOT:STATUS` then reported `crash_count=0 safe_mode=0`.
- Three consecutive `~BOOT:CRASH` restarts reported `crash_count=3 safe_mode=1`.
- Safe mode boot reported `boot_safety_safe_mode;audio_disabled`, `audio GAP integration enabled=0`, and `safe mode: BLE audio GATT disabled`.
- Safe mode did not start audio capture, voice recording control, BLE audio GATT, or BLE audio GAP state transitions.
- Final validation command sent `~BOOT:CLEAR`, clearing the safe mode latch on the device.

## Key Evidence

- `tests/artifacts/firmware_boot_safety_20260528-093620/boot_safety_crash_counter.log`
  - `App version:      7cfd683`
  - `fw_version=7cfd683`
  - `boot_safety: status: reset_reason=software(3) crash_count=1 threshold=3 safe_mode=0`
  - `normal boot survived 30000ms; crash counter cleared`
  - `boot_safety: USB STATUS reset_reason=software(3) crash_count=0 threshold=3 safe_mode=0`
  - `boot_safety: status: reset_reason=software(3) crash_count=3 threshold=3 safe_mode=1`
  - `boot safety safe mode active: BLE HID and recovery diagnostics only; audio disabled`
  - `audio GAP integration enabled=0`
  - `safe mode: BLE audio GATT disabled`
  - `keyboard: safe mode: voice recording control and audio capture are disabled`
  - `RESULT=PASS`
