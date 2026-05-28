# voice-keyboard-ota-update/1.4 Codex v1001 OTA validation

Date: 2026-05-28

## Result

Core Listener-Type BLE OTA transfer and version confirmation: PASS.

This does not by itself approve the full commercial OTA release gate. Rollback/failure-path matrix and broader product regression remain separate release-gate work.

## Package

- Package: `.cache/ota_e2e_codex_20260528/listener-ota-v1001.0.0-ota-test-20260528-092050`
- Manifest: `ota_manifest.json`
- Firmware: `firmware_ota.bin`
- Version: `v1001.0.0-ota-test`
- Size: `673392`
- SHA256: `e14e2d70a8856c37349c5bab9debca4b24ff9d8f65a1600fd022d13792c6271d`
- Channel: `internal-test`

## Commands

Locked hardware command:

```powershell
pwsh -NoProfile -File .\scripts\aiw.ps1 with-lock -Resource COM5,BLE-14C19F48FE72 -Run pwsh -NoProfile -File C:\Users\Billy\Desktop\listener\Listener-Type\tools\recover_listener_ble_ota.ps1 -DeviceName listener -BluetoothAddress 14C19F48FE72 -Port COM5 -ManifestPath C:\Users\Billy\Desktop\listener\voice-keyboard-firmware\.cache\ota_e2e_codex_20260528\listener-ota-v1001.0.0-ota-test-20260528-092050\ota_manifest.json -FirmwarePath C:\Users\Billy\Desktop\listener\voice-keyboard-firmware\.cache\ota_e2e_codex_20260528\listener-ota-v1001.0.0-ota-test-20260528-092050\firmware_ota.bin -RunTransfer
```

Post-OTA duplicate-package preflight:

```powershell
$env:LISTENER_TYPE_BLE_ADDRESS='14C19F48FE72'
cargo run --manifest-path src-tauri\Cargo.toml -- --firmware-ota-preflight <v1001 manifest> <v1001 firmware>
```

## Evidence

Main recovery/transfer summary:

- `tests/artifacts/listener_ble_ota_recovery/recover_listener_ble_ota_20260528-095415.md`
- `tests/artifacts/listener_ble_ota_recovery/recover_listener_ble_ota_20260528-095415.json`
- `tests/artifacts/listener_ble_ota_recovery/recover_listener_ble_ota_serial_20260528-095415.log`

Post-OTA status capture:

- `tests/artifacts/listener_ble_ota_recovery/recover_listener_ble_ota_serial_20260528-100933.log`

500-byte chunk recovery/transfer rerun after desktop timeout/progress fixes:

- `tests/artifacts/listener_ble_ota_recovery/recover_listener_ble_ota_20260528-104220.md`
- `tests/artifacts/listener_ble_ota_recovery/recover_listener_ble_ota_20260528-104220.json`
- `tests/artifacts/listener_ble_ota_recovery/recover_listener_ble_ota_serial_20260528-104220.log`

## Observed Results

Listener-Type OTA transfer result:

```json
{
  "status": "PASS",
  "mode": "transfer",
  "transfer": {
    "bytesTransferred": 673392,
    "transport": "listener_ble_ota",
    "confirmedVersion": "v1001.0.0-ota-test",
    "versionConfirmed": true
  },
  "errors": []
}
```

Post-OTA serial evidence:

```text
Loaded app from partition at offset 0x1d0000
App version:      v1001.0.0-ota-test
OTA STATUS running=ota_1 ... boot=ota_1 ... version=v1001.0.0-ota-test ... pending_verify=0 ... blocker=none
DIS identity: manufacturer=listener model=keyboard-v1 hw=esp32s3-devkit fw=v1001.0.0-ota-test proto=1
```

Post-OTA duplicate-package preflight result:

- Status: FAIL as expected.
- Reason: `This package is not newer than the device firmware.`
- Current device firmware reported by Listener-Type: `v1001.0.0-ota-test`.

This confirms the desktop can read DIS Firmware Revision after the v1001 OTA.

## 500-byte Chunk Rerun

The manual UI run exposed a desktop-side defect: the UI command timed out after 180 seconds while the blocking BLE writer kept running, and the background BLE audio listener restarted during the OTA window. The visible panel could then reset to "unselected" even while the writer still held the transfer path.

Fix validation rerun:

```text
Package manifest gattChunkBytes: 500
Preflight firmware: v1000.0.0-manual-base
Transfer bytes: 673392
Confirmed firmware: v1001.0.0-ota-test
Version confirmed: true
Result: PASS
```

The desktop package copied to `C:\Users\Billy\Desktop\listener-ota-v1001.0.0-ota-test` was updated to include `requirements.gatt_chunk_bytes = 500` for this rerun.

## Remaining Risks

- Windows still shows mixed OK/Unknown GATT service instances after OTA; this is now covered by `voice-keyboard-ble-customer-recovery-hardening`.
- This evidence covers successful transfer and version confirmation, not rollback/failure-path acceptance.
- The recovery script is a development/support tool; customer-facing recovery still needs product integration.
- Successful 500-byte transfer is promising but still needs manual UI regression and failure-path coverage before release-gate approval.
