# voice-keyboard-production-readiness/3.2 Evidence

Assignee: `oai2`

Firmware worktree: `C:\Users\Billy\Desktop\listener\voice-keyboard-firmware-wt-oai2-voice-keyboard-production-readiness-3.2`

Branch: `ai/oai2-voice-keyboard-production-readiness-3.2`

## Factory Package

Package:

`docs/validation/voice-keyboard-production-readiness-3.2/factory_package/listener-factory-v1002.0.0-ota-test-108-g6f9311d-20260607-155115/`

Package version: `v1002.0.0-ota-test-108-g6f9311d`

Package source commit: `6f9311d4a1a83a957e2af02bffa690b9b5fefedf`

Manifest dirty flag: `false`

Package artifacts:

| Role | File | Offset | SHA256 |
| --- | --- | --- | --- |
| bootloader | `bootloader.bin` | `0x0` | `fa280d8d94401a5439a4a72f30b8404e28711030c6c9ac640c6381f3f4804295` |
| partition table | `partition-table.bin` | `0x8000` | `d2b4d3dd6dd60b393f1e1c864ad3aefe737d240ca1f42157a2b6c198cbef43d7` |
| app | `voice-keyboard-firmware.bin` | `0x20000` | `04890f7dbf7d0b5cf07d94dee844024142d90fe6067ca99f3663d2dd2e3530cb` |

Package verifier:

```powershell
pwsh -NoProfile -File .\tools\verify_factory_firmware_package.ps1 -PackageDir .\docs\validation\voice-keyboard-production-readiness-3.2\factory_package\listener-factory-v1002.0.0-ota-test-108-g6f9311d-20260607-155115
```

Result: `PASS: factory firmware package valid`.

## BLE And Readiness Contract

The package manifest and `FLASHING.md` encode the first power-on assumptions:

- BLE GAP name `listener`
- BLE Appearance `0x03C1`
- HID service `1812`
- DIS manufacturer/model/hardware/firmware/protocol fields
- Readiness characteristic `710af845-6d9f-6583-0c4d-9e5b3bc3091c`
- Capabilities characteristic `710af845-6d9f-6583-0c4d-9e5b3bc3091d`
- Baseline readiness `factory_ready;pairable_on_boot;post_degraded_boot;board=voice-keyboard-n4;model=keyboard-v2;fw_version=<package version>`
- Capabilities include BLE HID, VKA1 audio, BLE audio control, USB serial text, voice recording toggle, custom F13-F24 fallback gestures, POST status, OTA, 4 MB flash, and no PSRAM.

## Diagnostics Without Raw Log Interpretation

`manifest.json`, `FLASHING.md`, and `!docs/features/factory_firmware_readiness.md` document the user-facing diagnostic commands:

- `~OTA:STATUS`
- `~DIS:GATT`
- `~OTA:GATT`
- `~DIAG:GATT`
- `~BOARD:STATUS`
- `~POWER:STATUS`
- `~DIAGLOG:COUNT`
- `~DIAGLOG:LAST:32`

The current available device on `COM6` is a `voice-keyboard-v2-n16r8` prototype, not the N4 4 MB/no-PSRAM target for this factory package. The N4 package was not flashed onto that mismatched prototype. The locked serial capture still proves the diagnostic-command path is observable without raw log interpretation:

- `docs/validation/voice-keyboard-production-readiness-3.2/locked-readiness-serial.log`

That log shows:

- `COMx` resolved to `COM6`
- `OTA STATUS` printed running/boot/update partitions, version, readiness, and capabilities
- `~DIS:GATT`, `~OTA:GATT`, and `~DIAG:GATT` printed registered handles
- `~BOARD:STATUS` and `~POWER:STATUS` printed product-facing status lines
- `DIAGLOG COUNT` and `DIAGLOG LAST 32` returned structured diagnostic log evidence
- the lock was released

## Hardware Observation Boundary

No N4-target physical BLE scan was recorded in this step because the only current ESP32 COM resource was the V2/N16R8 prototype. This step therefore uses build, package, manifest, static contract, verifier, and locked serial diagnostic evidence for AI-owned validation. Physical N4 first-power BLE scan or visual observation remains deferred to later hardware or human gates.

Locked HID smoke:

- `docs/validation/voice-keyboard-production-readiness-3.2/locked-verify-ble-hid.log`

The HID log shows `COMx` resolved to `COM6`, firmware consumed `hello`, HID reports completed, and the lock was released.
