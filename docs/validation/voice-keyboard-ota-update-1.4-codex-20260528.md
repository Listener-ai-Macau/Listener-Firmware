# Voice Keyboard OTA Update 1.4 Codex Gate Attempt

Date: 2026-05-28
Agent: codex
Hardware: COM5 / BLE 14C19F48FE72

## Summary

OTA release gate is still NO-GO.

Windows can enumerate the product device and Listener OTA GATT service again, and Listener-Type headless preflight can read device identity, DIS firmware revision, capability `firmware_ota_v1`, and battery. Real transfer still fails during BLE OTA data writes with Windows HRESULT `0x800704C7`.

## Package

- Package: `.cache/ota_e2e_codex_20260528/listener-ota-v1000.0.0-ota-test-20260528-080851`
- Manifest version: `v1000.0.0-ota-test`
- Firmware size: `662944`
- SHA256: `b153b420715104e9fb5a95fe6032511b8018bfbb57108b3ade1726e55b0c0bba`
- Build evidence: `tools/build.ps1 -Target esp32s3`
- App size: `0xa1da0`, OTA slot free `0x10e260` (63%)

The `v1000.0.0-ota-test` version was generated with a temporary local git tag so Listener-Type's version gate would treat it as newer than hash-like current versions.

## Passing Evidence

- Windows PnP enumerated:
  - `BTHLE\DEV_14C19F48FE72`
  - OTA service `710AF845-6D9F-6583-0C4D-9E5B3BC3092A`
  - DIS service `0000180A-0000-1000-8000-00805F9B34FB`
- Preflight PASS:
  - artifact: `tests/artifacts/voice_keyboard_ota_update_1.4_codex_20260528/preflight_v1000_20260528-0810.log`
  - device firmware: `8f764cd`
  - capability: `firmware_ota_v1`
  - battery: `90%`
  - blockers: none
- Forced-address preflight PASS:
  - artifact: `tests/artifacts/voice_keyboard_ota_update_1.4_codex_20260528/preflight_v1000_forced_addr_20260528-0817.log`
  - device firmware: `8f764cd`
  - capability: `firmware_ota_v1`
  - battery: `95%`
  - blockers: none
- After closing a running Listener-Type GUI instance and restarting Bluetooth, preflight PASS:
  - artifact: `tests/artifacts/voice_keyboard_ota_update_1.4_codex_20260528/preflight_after_long_failed_transfer_20260528-0838.log`
  - device firmware: `0b2b06f`
  - capability: `firmware_ota_v1`
  - battery: `93%`
  - blockers: none

## Failed Evidence

- Transfer with normal selector failed before opening a usable target:
  - artifact: `tests/artifacts/voice_keyboard_ota_update_1.4_codex_20260528/transfer_v1000_20260528-0811.log`
  - failure: stale or invalid Windows BLE target selection; service id/address fallback failed.
- Transfer after Bluetooth recovery still failed before transfer:
  - artifact: `tests/artifacts/voice_keyboard_ota_update_1.4_codex_20260528/transfer_v1000_after_recover_20260528-0813.log`
  - failure: `GattCommunicationStatus(1)` while opening OTA characteristics.
- Transfer with forced real BLE address and Listener-Type GUI closed reached data write stage, then failed:
  - artifact: `tests/artifacts/voice_keyboard_ota_update_1.4_codex_20260528/transfer_v1000_forced_addr_after_app_close_20260528-0824.log`
  - duration: about 13 minutes
  - preflight inside transfer: PASS, device firmware `0b2b06f`, capability `firmware_ota_v1`, battery `93%`
  - failure: `BLE OTA data write async error: Some(HRESULT(0x800704C7))`

## Additional Findings

- A running Listener-Type GUI process was present and likely held BLE/serial resources; closing it made the headless checks cleaner.
- The current desktop BLE fallback can misparse Windows service instance ids like `DEV_VID&0216C0_PID&05DF...` as a Bluetooth address (`D0216C0D05DF`). For validation, `LISTENER_TYPE_BLE_ADDRESS=14C19F48FE72` was used to force the real device address.
- Opening serial after failed transfer showed the device can return to a safe OTA state:
  - artifact: `tests/artifacts/voice_keyboard_ota_update_1.4_codex_20260528/post_failed_transfer_serial_20260528-081341.log`
  - `active=0`, `bytes=0`, `expected=0`, `blocker=none`
- After the long transfer failure, COM5 temporarily disappeared from `Win32_SerialPort`; BLE PnP still listed the product and services as present.

## Decision

NO-GO for release gate. The next fix should be in Listener-Type's Windows BLE OTA transport:

- reject VID/PID-derived pseudo-addresses when parsing Windows service device ids;
- ensure no GUI/headless double-open of the same BLE target during OTA;
- add progress artifact emission for headless transfer so failures report the last successful chunk;
- add data-write retry/reopen or checkpointed abort/retry behavior for `0x800704C7`;
- rerun full OTA E2E only after the transport can survive or recover from a canceled write.
