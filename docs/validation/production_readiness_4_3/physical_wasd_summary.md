# Physical WASD BLE HID Evidence — 2026-05-25

**Firmware**: commit e37f2be, built and flashed to COM5 (ESP32-S3 rev v0.2, MAC 14:C1:9F:48:FE:70)
**Human operator**: Billy physically pressed KEY1-KEY4 on SPH0645 V1 board

## Result: All 4 physical keys produce host-visible BLE HID characters

| Key  | GPIO  | Output | Raw transition | Queued          | send_ascii done       |
|------|-------|--------|----------------|-----------------|-----------------------|
| KEY4 | GPIO21 | s     | high→low→high  | output=s        | connected=yes, done   |
| KEY3 | GPIO47 | a     | high→low→high  | output=a        | connected=yes, done   |
| KEY2 | GPIO48 | w     | high→low→high  | output=w        | connected=yes, done   |
| KEY1 | GPIO45 | d     | high→low→high  | output=d        | connected=yes, done   |

All 4 GPIOs are connected, all produced raw GPIO transitions, all were debounced correctly,
all queued the correct character, and all delivered via BLE HID to the connected host.

## Additional: GPIO35 EC11 physical voice key also verified

The same capture session shows gpio35.ec11_key press/release triggering recording start/stop.

## Raw log

See `physical_wasd_hid_20260525_final.log` for the complete serial output.
