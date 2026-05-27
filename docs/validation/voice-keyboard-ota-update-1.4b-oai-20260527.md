# voice-keyboard-ota-update / 1.4b OAI validation

Date: 2026-05-27T10:52+08:00
Agent: oai
Branch: ai/tai-voice-keyboard-ota-update-1.4b

## Summary

Implemented the firmware-side fix for Listener-Type OTA version confirmation.

Root cause from the 1.4 NO-GO was confirmed: firmware called
`ble_svc_dis_firmware_revision_set(listener_device_get_fw_version())`, but
NimBLE DIS Firmware Revision `2A26` was not enabled in sdkconfig defaults.

## Changes

- Enabled NimBLE DIS service identity characteristics in both `sdkconfig.defaults`
  and `sdkconfig.defaults.esp32s3`:
  - `CONFIG_BT_NIMBLE_DIS_SERVICE=y`
  - `CONFIG_BT_NIMBLE_SVC_DIS_HARDWARE_REVISION=y`
  - `CONFIG_BT_NIMBLE_SVC_DIS_FIRMWARE_REVISION=y`
  - `CONFIG_BT_NIMBLE_SVC_DIS_SOFTWARE_REVISION=y`
- Extended `tools/verify_ble_ota_gatt_contract.py` so the static contract check
  now verifies:
  - DIS firmware revision is enabled by defaults.
  - DIS firmware revision is set from `listener_device_get_fw_version()`.
  - DIS hardware/software revision setters are wired.
  - `listener_device_get_fw_version()` returns the ESP app description version.
  - firmware capabilities still advertise `firmware_ota_v1`.

## Offline Validation

| Command | Result |
|---|---|
| `python .\tools\verify_ble_ota_gatt_contract.py` | PASS, BLE OTA contract matches desktop contract and DIS OTA readiness checks pass. |
| `python -m compileall -q tools` | PASS. |
| `git diff --check` | PASS, only CRLF normalization warnings. |
| `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check` | PASS. |
| `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3` | PASS. |

Build output:

- App version: `d0a7596-dirty`
- `voice-keyboard-firmware.bin` size: `0x9ca90`
- Smallest OTA app partition: `0x1b0000`
- Free app slot space: `0x113570` (64%)
- Generated `sdkconfig` contains `CONFIG_BT_NIMBLE_SVC_DIS_FIRMWARE_REVISION=y`.

## Remaining Hardware Validation

Not run yet. `COM5` and `BLE-14C19F48FE72` were still locked by Tai at
2026-05-27T10:52+08:00, with lock timeout from 2026-05-27T08:30+08:00 for
180 minutes.

Required before submit:

- Flash this 1.4b firmware to the target board under a workflow hardware lock.
- Verify Windows/Listener-Type can read DIS Firmware Revision `2A26` and it
  equals the running firmware version.
- Verify `get_firmware_ota_preflight_snapshot` returns non-null
  `firmwareVersion` and includes `firmware_ota_v1`.
- If hardware/time allows, rerun Listener-Type OTA version-confirmation smoke and
  confirm `transfer_firmware_ota_ble` returns `confirmedVersion=<N+1>`.
