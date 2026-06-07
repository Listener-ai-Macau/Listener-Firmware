# Listener Factory Firmware Package

Version: v1002.0.0-ota-test-161-g92ecd8f

Target: esp32s3

BLE name: listener

Model: keyboard-v2

Hardware revision: esp32s3-wroom-1-n16r8

Appearance: 0x03C1 keyboard

DIS firmware revision: v1002.0.0-ota-test-161-g92ecd8f

DIS software revision / protocol: 1

Readiness characteristic: 710af845-6d9f-6583-0c4d-9e5b3bc3091c

Capabilities characteristic: 710af845-6d9f-6583-0c4d-9e5b3bc3091d

## Flash

Run this command from this package directory:

~~~powershell
python $env:IDF_PATH\components\esptool_py\esptool\esptool.py --chip esp32s3 -p COMx -b 460800 --before=default_reset --after=hard_reset write_flash 0x0 .\bootloader.bin 0x8000 .\partition-table.bin 0x20000 .\voice-keyboard-firmware.bin
~~~

Partition evidence from the generated partition table:
- otadata: type=data subtype=ota offset=0xf000 size=8K
- ota_0: type=app subtype=ota_0 offset=0x20000 size=6M
- ota_1: type=app subtype=ota_1 offset=0x620000 size=6M

## SHA256
- bootloader.bin @ 0x0: de63e475062b6ce26dc49b96861e2631396ebf0753cc38607e3d5be99d20b19a (20912 bytes)
- partition-table.bin @ 0x8000: a7f91539844d456d24a589cc77ca103df1a3c8b0a9d91d1c684149830cc44cb5 (3072 bytes)
- voice-keyboard-firmware.bin @ 0x20000: 558526dbc063fd8ddeefbae65c4062b6a010c8820d4d9404de33a3031626a412 (787600 bytes)

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
