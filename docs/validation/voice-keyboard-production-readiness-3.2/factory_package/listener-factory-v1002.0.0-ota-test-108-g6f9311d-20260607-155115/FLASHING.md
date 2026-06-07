# Listener Factory Firmware Package

Version: v1002.0.0-ota-test-108-g6f9311d

Target: esp32s3

BLE name: listener

Model: keyboard-v2

Hardware revision: esp32s3-wroom-1-n4

Appearance: 0x03C1 keyboard

DIS firmware revision: v1002.0.0-ota-test-108-g6f9311d

DIS software revision / protocol: 1

Readiness characteristic: 710af845-6d9f-6583-0c4d-9e5b3bc3091c

Capabilities characteristic: 710af845-6d9f-6583-0c4d-9e5b3bc3091d

## Flash

Run this command from this package directory:

~~~powershell
python $env:IDF_PATH\components\esptool_py\esptool\esptool.py --chip esp32s3 -p COM3 -b 460800 --before=default_reset --after=hard_reset write_flash 0x0 .\bootloader.bin 0x8000 .\partition-table.bin 0x20000 .\voice-keyboard-firmware.bin
~~~

Partition evidence from the generated partition table:
- otadata: type=data subtype=ota offset=0xf000 size=8K
- ota_0: type=app subtype=ota_0 offset=0x20000 size=1728K
- ota_1: type=app subtype=ota_1 offset=0x1d0000 size=1728K

## SHA256
- bootloader.bin @ 0x0: fa280d8d94401a5439a4a72f30b8404e28711030c6c9ac640c6381f3f4804295 (20912 bytes)
- partition-table.bin @ 0x8000: d2b4d3dd6dd60b393f1e1c864ad3aefe737d240ca1f42157a2b6c198cbef43d7 (3072 bytes)
- voice-keyboard-firmware.bin @ 0x20000: 04890f7dbf7d0b5cf07d94dee844024142d90fe6067ca99f3663d2dd2e3530cb (764912 bytes)

## First Power-On Contract

After flashing and reset, the device advertises as listener without any serial command. POST failures are logged and the firmware continues into degraded BLE mode so status remains observable.

Expected first power-on identity:

- BLE GAP name: listener
- BLE appearance: 0x03C1 keyboard
- HID service: 1812
- DIS manufacturer/model/hardware/firmware/protocol fields match manifest.json
- Readiness characteristic: 710af845-6d9f-6583-0c4d-9e5b3bc3091c
- Capabilities characteristic: 710af845-6d9f-6583-0c4d-9e5b3bc3091d

Serial diagnostics that do not require interpreting raw firmware logs:

- ~OTA:STATUS prints running/boot/update partitions, version, blocker, readiness and capabilities.
- ~DIS:GATT, ~OTA:GATT, and ~DIAG:GATT print service/characteristic registration handles.
- ~BOARD:STATUS and ~POWER:STATUS print board, battery, wake-policy and power blockers.
- ~DIAGLOG:COUNT and ~DIAGLOG:LAST:32 expose recent structured diagnostic events.

If a physical BLE scan is not available, treat this package's manifest, SHA256 values and readiness contract as build/package evidence. Record the live BLE observation in the next hardware or human gate.
