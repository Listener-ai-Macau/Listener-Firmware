# voice-keyboard-production-readiness/3.2 Evidence

Assignee: `oai2`

Firmware worktree: `C:\Users\Billy\Desktop\listener\voice-keyboard-firmware-wt-oai2-voice-keyboard-production-readiness-3.2`

Branch: `ai/oai2-voice-keyboard-production-readiness-3.2`

## Factory Package

Package:

`docs/validation/voice-keyboard-production-readiness-3.2/factory_package/listener-factory-v1002.0.0-ota-test-161-g92ecd8f-20260607-214244/`

Package version: `v1002.0.0-ota-test-161-g92ecd8f`

Package source commit: `0ee94b2d55bd28fd9bc9511c458a7c5efc01d7dd`

Manifest dirty flag: `false`

Package artifacts:

| Role | File | Offset | SHA256 |
| --- | --- | --- | --- |
| bootloader | `bootloader.bin` | `0x0` | `de63e475062b6ce26dc49b96861e2631396ebf0753cc38607e3d5be99d20b19a` |
| partition table | `partition-table.bin` | `0x8000` | `a7f91539844d456d24a589cc77ca103df1a3c8b0a9d91d1c684149830cc44cb5` |
| app | `voice-keyboard-firmware.bin` | `0x20000` | `558526dbc063fd8ddeefbae65c4062b6a010c8820d4d9404de33a3031626a412` |

Package verifier:

```powershell
pwsh -NoProfile -File .\tools\verify_factory_firmware_package.ps1 -PackageDir .\docs\validation\voice-keyboard-production-readiness-3.2\factory_package\listener-factory-v1002.0.0-ota-test-161-g92ecd8f-20260607-214244
```

Result: `PASS: factory firmware package valid`.

Latest package creation log: `docs/validation/voice-keyboard-production-readiness-3.2/factory-package-create-20260607-oai2-v2.log`.

Latest verifier log: `docs/validation/voice-keyboard-production-readiness-3.2/factory-package-verify-20260607-oai2-v2.log`.

Latest validation report: `docs/validation/voice-keyboard-production-readiness-3.2-validation-evidence.json`.

Latest ESP-IDF build log: `docs/validation/voice-keyboard-production-readiness-3.2/idf-build-20260607-oai2-v2-pass.log`.

## BLE And Readiness Contract

The package manifest and `FLASHING.md` encode the first power-on assumptions:

- BLE GAP name `listener`
- BLE Appearance `0x03C1`
- HID service `1812`
- DIS manufacturer/model/hardware/firmware/protocol fields
- Readiness characteristic `710af845-6d9f-6583-0c4d-9e5b3bc3091c`
- Capabilities characteristic `710af845-6d9f-6583-0c4d-9e5b3bc3091d`
- Baseline readiness `factory_ready;pairable_on_boot;post_degraded_boot;board=voice-keyboard-v2-n16r8;model=keyboard-v2;fw_version=<package version>`
- Capabilities include BLE HID, VKA1 audio, BLE audio control, USB serial text, voice recording toggle, custom F13-F24 fallback gestures, POST status, OTA, 16 MB flash, and 8 MB Octal PSRAM.

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

The current available device on `COM6` is a `voice-keyboard-v2-n16r8` prototype. The package was not flashed during this rebase refresh, but the locked serial capture proves the V2 diagnostic-command path is observable without raw log interpretation:

- `docs/validation/voice-keyboard-production-readiness-3.2/locked-readiness-serial.log`

That log shows:

- `COMx` resolved to `COM6`
- `OTA STATUS` printed running/boot/update partitions, version, readiness, and capabilities
- `~DIS:GATT`, `~OTA:GATT`, and `~DIAG:GATT` printed registered handles
- `~BOARD:STATUS` and `~POWER:STATUS` printed product-facing status lines
- `DIAGLOG COUNT` and `DIAGLOG LAST 32` returned structured diagnostic log evidence
- the lock was released

## Hardware Observation Boundary

No new physical first-power BLE scan was recorded in this step. This step therefore uses build, package, manifest, static contract, verifier, and locked serial diagnostic evidence for AI-owned validation. Physical first-power BLE scan or visual observation remains deferred to later hardware or human gates.

Locked HID smoke:

- `docs/validation/voice-keyboard-production-readiness-3.2/locked-verify-ble-hid.log`

The HID log shows `COMx` resolved to `COM6`, firmware consumed `hello`, HID reports completed, and the lock was released.
