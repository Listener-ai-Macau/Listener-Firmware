# Factory Firmware Image And BLE Readiness

## Scope

The factory firmware image covers the first power-on state for production bring-up:

- A deliverable package contains `bootloader.bin`, `partition-table.bin`, the app image, version metadata, SHA256 values, partition offsets, and flashing instructions.
- First power-on does not require a serial command before the device advertises as a pairable and discoverable BLE HID keyboard.
- BLE identity is stable: GAP name `listener`, Appearance `0x03C1` keyboard, HID service `0x1812`, and the Listener audio service `710af845-6d9f-6583-0c4d-9e5b3bc3091a`.
- POST critical failures are logged, readiness marks degraded subsystems, and firmware continues into observable BLE/HID/serial recovery where possible.

## BLE Identity Contract

Device Information Service fields:

- Manufacturer: `listener`
- Model Number: `keyboard-v2`
- Hardware Revision: `esp32s3-wroom-1-n16r8`
- Firmware Revision: the package firmware version
- Software Revision: protocol version `1`
- PnP ID: VID `0x16C0`, PID `0x05DF`, product version `1`

Custom Listener service fields:

- Audio service: `710af845-6d9f-6583-0c4d-9e5b3bc3091a`
- Audio notify: `710af845-6d9f-6583-0c4d-9e5b3bc3091b`
- Audio control: `710af845-6d9f-6583-0c4d-9e5b3bc3091e`
- Readiness: `710af845-6d9f-6583-0c4d-9e5b3bc3091c`
- Capabilities: `710af845-6d9f-6583-0c4d-9e5b3bc3091d`

Baseline readiness tokens:

```text
factory_ready;pairable_on_boot;post_degraded_boot;board=voice-keyboard-v2-n16r8;model=keyboard-v2;fw_version=<version>
```

Runtime readiness appends subsystem tokens such as `hid_ready`, `audio_ready`, `ota_ready`, `diagnostic_ready`, or the corresponding `*_degraded` tokens.

Capabilities include:

```text
ble_hid_keyboard;ble_audio_vka1;ble_audio_control_v1;usb_serial_text;voice_record_toggle;custom_keys_f13_f16;custom_key_gestures_f13_f24;post_status;firmware_ota_v1;flash_16mb;psram_8mb_octal
```

## Packaging

Build first:

```powershell
idf.py build
```

Generate a package:

```powershell
pwsh -NoProfile -File .\tools\package_factory_firmware.ps1
```

Default output is `.cache\factory_firmware\listener-factory-<version>-<timestamp>\`. The package contains:

- `bootloader.bin`, flash offset `0x0`
- `partition-table.bin`, flash offset `0x8000`
- `voice-keyboard-firmware.bin`, flash offset derived from the generated `ota_0` partition
- `manifest.json`
- `FLASHING.md`

`manifest.json` records schema version, project version, git commit, dirty state, BLE identity, DIS fields, readiness/capabilities values, serial diagnostic commands, partition evidence, SHA256 values, and the complete flash command.

Validate a generated package:

```powershell
pwsh -NoProfile -File .\tools\verify_factory_firmware_package.ps1 -PackageDir .\.cache\factory_firmware\<package>
```

## Diagnostics Without Raw Log Interpretation

Factory support can use serial commands instead of reading raw boot logs:

- `~OTA:STATUS` prints running, boot, update partitions, version, blocker, readiness, and capabilities.
- `~DIS:GATT`, `~OTA:GATT`, and `~DIAG:GATT` print service and characteristic registration handles.
- `~BOARD:STATUS` and `~POWER:STATUS` print board, battery, wake policy, and power blockers.
- `~DIAGLOG:COUNT` and `~DIAGLOG:LAST:32` expose recent structured diagnostic events.

## Validation

Scriptable validation:

```powershell
idf.py build
pwsh -NoProfile -File .\tools\package_factory_firmware.ps1
pwsh -NoProfile -File .\tools\verify_factory_firmware_package.ps1 -PackageDir .\.cache\factory_firmware\<package>
pwsh -NoProfile -File .\tools\verify_v2_board_profile_static.ps1
pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1
git diff --check
```

If a current unique ESP32 COM/BLE resource is available, hardware validation should be run inside one short `aiw with-lock` window and can include:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify_ble_hid.ps1 -Port COMx -Text "hello"
```

The hardware check confirms BLE name, appearance, DIS firmware/protocol, readiness, and capabilities. If no physical BLE scan is available, the generated package manifest, SHA256 values, readiness contract, static checks, and validation report are the AI-owned evidence; live visual/BLE observation is deferred to later hardware or human gates.

## Invariants

- Factory BLE name remains `listener`.
- Appearance remains keyboard `0x03C1`.
- First power-on advertises without a serial command.
- Factory app image offset is derived from `ota_0`, not hard-coded to `0x10000`.
- POST critical failures log and continue into observable degraded BLE/HID/serial recovery where possible.
- Default package output lives under `.cache\factory_firmware`; workflow validation packages may be copied under `docs/validation` as review evidence.
