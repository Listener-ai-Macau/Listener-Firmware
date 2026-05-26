# voice-keyboard-ota-update 1.4a validation

Date: 2026-05-26
Agent: codex
Hardware: COM5 / BLE 14:C1:9F:48:FE:72

## Change

- Fixed the BLE advertising metadata for the new firmware OTA service.
- The legacy scan response can only fit one 128-bit service UUID, so the firmware continues to advertise the existing audio service UUID but now marks the 128-bit UUID list as incomplete. This prevents Windows from treating the audio-only advertised list as the full GATT service set and hiding the new OTA service from uncached discovery.
- Hardened `tools/verify_ble_ota_gatt_discovery.ps1` so WinRT async failures unwrap the inner HRESULT instead of reporting only `AggregateException`.
- Added a static contract guard so the single advertised 128-bit UUID list cannot be marked complete while OTA is also present.

## Validation

- `python tools\verify_ble_ota_gatt_contract.py`
  - PASS: OTA service/control/data UUIDs match the desktop contract.
  - PASS: BLE path still calls `firmware_ota_begin`, `firmware_ota_write`, `firmware_ota_finish(false)`, and abort paths.
  - PASS: advertising contract requires `uuids128_is_complete = 0`.
- `python -m compileall -q tools`
  - PASS.
- PowerShell parser check for `tools\verify_ble_ota_gatt_discovery.ps1`
  - PASS.
- `git diff --check`
  - PASS; only Git line-ending warnings were printed on Windows.
- `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
  - PASS.
  - `voice-keyboard-firmware.bin` size: `0x9c580`.
  - Smallest OTA app partition: `0x1b0000`; free: `0x113a80` (64%).
- Clean rebuild after committing source/doc changes
  - PASS.
  - CMake reported app version `bb85ddf`.
- `pwsh -NoProfile -File .\tools\flash.ps1 -Port COM5 -Target esp32s3`
  - PASS.
- Final clean-commit flash
  - PASS.
  - Serial boot reported app version `bb85ddf`, not `-dirty`.
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\verify_ble_ota_gatt_discovery.ps1 -DeviceName listener -BluetoothAddress 14C19F48FE72 -TimeoutSeconds 30`
  - PASS.
  - OTA service: `710af845-6d9f-6583-0c4d-9e5b3bc3092a`.
  - Control characteristic: `710af845-6d9f-6583-0c4d-9e5b3bc3092b`, `Write`.
  - Data characteristic: `710af845-6d9f-6583-0c4d-9e5b3bc3092c`, `WriteWithoutResponse, Write`.
- Same GATT discovery with `-SkipCachedSessionPrime`
  - PASS.
- Serial `~OTA:GATT` / `~OTA:STATUS`
  - PASS.
  - OTA GATT handles after NimBLE sync: service `9`, control value `11`, data value `13`.
  - Final clean-commit OTA status: running `ota_0`, update `ota_1`, version `bb85ddf`, active `0`, pending_verify `0`, blocker `none`.
- Direct BLE audio transport smoke:
  - `python tools\capture_audio_ble_wav.py --port COM5 --device-name listener --bluetooth-address 14C19F48FE72 --capture-seconds 5 ...`
  - PASS: duration `5.060s`, received `338/338` packets, missing packets `0`, PCM bytes `161920`, notify disable status `0`.

## Notes

- Before the advertising fix, Windows uncached GATT discovery failed with `0x80070016` and PnP/WinRT only exposed the older audio service. After marking the advertised 128-bit UUID list incomplete and clearing the stale Windows BLE pairing/cache, Windows discovered 8 services including the OTA service.
- A1 product-chain was retried after pairing recovered. It no longer failed on notify readiness: `notify_ready=true`, `stream_ready=true`, `missingPacketCount=0`, and Listener-Type inserted a history session. The remaining A1 failure was transcript accuracy caused by live environmental speech mixing into the scripted playback, so it is recorded as an acoustic test condition rather than a firmware OTA/GATT regression.

## Local Artifacts

Generated under `tests/artifacts/ota_gatt_1_4a_codex_20260526_resume/`:

- `flash_com5_after_adv_fix.log`
- `verify_ble_ota_gatt_discovery_after_adv_fix_unpaired_winps.log`
- `verify_ble_ota_gatt_discovery_after_adv_fix_skip_prime_winps.log`
- `verify_ble_ota_gatt_discovery_final_winps.log`
- `serial_ota_gatt_status_after_adv_fix.log`
- `serial_ota_gatt_status_final_clean_rebuild.log`
- `verify_ble_ota_gatt_discovery_final_clean_rebuild_winps.log`
- `capture_audio_ble_after_adv_fix.serial.log`
- `audio_matrix_A1_after_adv_fix_retry_paired.log`
- `audio_matrix_A1_after_adv_fix_fixed_sentence.log`
