# voice-keyboard-ota-update / 1.4b Tai continuation

Date: 2026-05-27T13:20+08:00
Agent: Tai
Branch: ai/tai-voice-keyboard-ota-update-1.4b
Hardware lock: COM5, BLE-14C19F48FE72

## Summary

The firmware-side DIS/readiness fix is implemented and continues to pass
offline validation, but the step cannot be submitted because the real-device
acceptance requires flashing or OTA-updating the target board and Windows is
currently unable to use either path.

The local build registers DIS Firmware Revision 2A26 and includes Service
Changed refresh hooks, but the target board is still running the older firmware
shape from Windows' point of view. The host currently has no COM port for USB
flash/serial recovery, and BLE OTA writes fail before transfer begins.

## Firmware-side changes preserved

- DIS default config enables manufacturer, serial, hardware, firmware,
  software, and PNP identity characteristics.
- `~DIS:GATT` logs DIS service and characteristic handles, including Firmware
  Revision 2A26, for hardware validation.
- GAP queues Service Changed on connect and sends Service Changed indication
  when a central subscribes or encryption changes, then logs tx completion.
- `tools/verify_ble_ota_gatt_contract.py` checks the DIS config, firmware
  version source, `~DIS:GATT`, and Service Changed refresh behavior.

## Offline validation

| Command | Result |
|---|---|
| `python .\tools\verify_ble_ota_gatt_contract.py` | PASS |
| `python -m compileall -q tools tests\artifacts\ota_update_1_4b` | PASS |
| `git diff --check` | PASS, CRLF warnings only |
| `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check` | PASS |
| `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3` | PASS |

Latest build output from this continuation:

- `voice-keyboard-firmware.bin` size: `0x9d420`
- OTA slot free: `0x112be0` (64%)
- Generated `sdkconfig` contains
  `CONFIG_BT_NIMBLE_SVC_DIS_FIRMWARE_REVISION=y`.

## Hardware validation attempted

USB serial:

- `[System.IO.Ports.SerialPort]::GetPortNames()` returned no ports.
- `Get-CimInstance Win32_SerialPort` returned no serial devices.
- `pwsh -NoProfile -File .\tools\flash.ps1 -Port COM5 -Target esp32s3`
  failed earlier in this continuation because COM5 could not be opened.

BLE before pairing/cache reset:

- `recover_ble_hid_host.ps1` restarted Bluetooth and briefly got cached GATT
  service enumeration back to 8 services.
- `verify_ble_ota_gatt_discovery.ps1` still failed with
  `OTA service 710af845-6d9f-6583-0c4d-9e5b3bc3092a discovery failed:
  Unreachable`.
- `run_listener_type_preflight_cdp.py` reported `connected=true`,
  `hardwareRevision="keyboard-v1"`, capability `firmware_ota_v1`, and
  `firmwareVersion=null`.
- `run_listener_type_ota_transfer_cdp.py` failed at begin:
  `BLE OTA control begin write returned status=GattCommunicationStatus(1)`.
- DIS selector probing exposed only stale cached DIS shape and could not read
  Firmware Revision 2A26.

Targeted Windows BLE pairing/cache reset:

- `manage_ble_pairing.ps1 -Action Status` showed Windows considered
  `14C19F48FE72` paired.
- `manage_ble_pairing.ps1 -Action Unpair` returned `unpairStatus="Unpaired"`
  and `isPairedAfter=false`.
- After Bluetooth restart, `ensure_ble_hid_connection.ps1` reported
  `gatt=Unreachable services=0 sessions=0`.
- `manage_ble_pairing.ps1 -Action Pair` returned `pairStatus="Failed"` and
  `isPairedAfter=false`.
- After this failed re-pair, OTA discovery still returned `Unreachable` and
  DIS service selector returned `candidateCount=0`.

## Blocker

1.4b should not be submitted yet. Acceptance requires a real board running this
firmware and Listener-Type reading a non-null DIS firmware version after OTA or
flash. Current unblock condition is one of:

- COM5 or another board serial port reappears so this branch can be flashed and
  validated with `~OTA:STATUS`, `~DIS:GATT`, OTA GATT discovery, and
  Listener-Type preflight; or
- the Windows BLE pairing/cache state and device bond are recovered so BLE OTA
  can transfer this branch firmware, reboot, and confirm `firmwareVersion` /
  `confirmedVersion` non-null.

If using the current unpaired Windows state, first restore pairing for
`listener` at `14C19F48FE72` through Windows UI or device-side recovery. The
scripted WinRT `PairAsync` attempt failed on this host.
