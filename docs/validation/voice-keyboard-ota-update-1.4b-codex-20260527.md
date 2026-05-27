# Voice Keyboard OTA Update 1.4b Codex Validation

Date: 2026-05-27

Worktree: `C:\Users\Billy\Desktop\listener\voice-keyboard-firmware-wt-codex-voice-keyboard-ota-update-1.4b`

Branch: `ai/codex-voice-keyboard-ota-update-1.4b`

## Firmware Changes

- Service Changed indications are now gated by firmware version in NVS.
- After a successful Service Changed indication for a firmware version, reconnects skip the GATT refresh until the firmware version changes again.
- The BLE OTA contract verifier now checks for version-gated Service Changed persistence.

## Desktop Companion

The true BLE transfer validation used the companion Listener-Type step `listener-type-ota-transfer-write-options/1.1`, which keeps an audio notify subscription open during OTA transfer so Windows does not tear down the BLE link mid-transfer.

## Package Evidence

- Package: `tests\artifacts\voice_keyboard_ota_1_4b_codex_20260527-135752\ota_keepalive_target_package_path.txt`
- Version: `14f779f-dirty`
- Size: `645600`
- SHA256: `cf673aa4eb80cb0b764b7eb668066401e44b03780bd0c1eb59fe790bccae5c5d`
- Package log: `tests\artifacts\voice_keyboard_ota_1_4b_codex_20260527-135752\package_current_keepalive_target.txt`

## Hardware Validation

Baseline flashed over USB:

- Version: `ota-smoke-0.0.1-dirty`
- Flash log: `tests\artifacts\voice_keyboard_ota_1_4b_codex_20260527-135752\flash_ota_smoke_baseline_keepalive.txt`

BLE OTA transfer:

- Transfer log: `tests\artifacts\voice_keyboard_ota_1_4b_codex_20260527-135752\listener_type_transfer_keepalive_ota_smoke_to_14f779f_dirty.txt`
- Serial log: `tests\artifacts\voice_keyboard_ota_1_4b_codex_20260527-135752\serial_during_keepalive_transfer.log`
- Result: PASS.
- Device preflight version: `ota-smoke-0.0.1-dirty`.
- Transferred bytes: `645600`.
- Confirmed version after reboot: `14f779f-dirty`.

Key serial evidence:

- `audio notify subscribed` before OTA begin.
- `firmware_ota: OTA begin ... from=ota-smoke-0.0.1-dirty to=14f779f-dirty`.
- `firmware_ota: OTA set boot partition=ota_1 ... bytes=645600`.
- `ble_firmware_ota: OTA control finish size=645600 ret=ESP_OK`.
- `service changed confirmed for fw_version=14f779f-dirty; future reconnects skip GATT refresh`.

No `disconnect; reason=546` occurred between OTA begin and OTA finish. The Windows idle reconnect cycle still appears after reboot; that is outside the OTA transfer window and should be treated as a separate idle BLE behavior if product requirements require eliminating it globally.

## Commands

| Command | Result |
|---|---|
| `python .\tools\verify_ble_ota_gatt_contract.py` | PASS |
| `python -m compileall -q tools` | PASS |
| `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3` | PASS |
| `pwsh -NoProfile -File .\tools\flash.ps1 -Port COM5 -Target esp32s3` | PASS, baseline flash |
| `pwsh -NoProfile -File .\tools\package_ota_firmware.ps1 -BuildDir .\build -OutputRoot .\.cache\ota_firmware_keepalive -Channel internal-test` | PASS |
| Listener-Type `cargo run --manifest-path src-tauri\Cargo.toml -- --firmware-ota-transfer <manifest> <firmware>` | PASS |
| `git diff --check` | PASS, only LF/CRLF warnings |
