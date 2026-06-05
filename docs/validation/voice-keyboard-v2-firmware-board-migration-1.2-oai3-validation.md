# voice-keyboard-v2-firmware-board-migration/1.2 Validation Evidence

Agent: oai3
Date: 2026-06-05

## Manual Interface Compare

PASS: Compared the active V2 firmware contract against `C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\docs\validation\voice-keyboard-v2-firmware-board-migration-1.1-interface-confirmed.md`.

- `ports/esp32/board_pins/include/board_pins.h` now targets `ESP32-S3-WROOM-1-N16R8`, 16 MB flash, 8 MB Octal PSRAM, and reserves `GPIO35,GPIO36,GPIO37`.
- V2 pin map matches the confirmed interface for keys, EC11, USB, charger, battery ADC, PWR_HOLD, current ADCs, microphone CLK/DOUT, and all four WS2812 data pins.
- LED resources match the confirmed four-zone map: status GPIO1 LED1..6, EC11 GPIO5 LED7..10 + LED15..16 + LED23..28, key GPIO13 LED11..14, and edge GPIO4 LED17..22.
- Power telemetry reports TPS63020/SY7088 as battery-side input branch current monitors and combines current with reconstructed battery voltage from BAT_V_ADC/GPIO8 using the 68K/68K midpoint.
- USB_Det diagnostics report the latest GPIO7 R37/R32 10K/10K divider policy.

## Rework Closure

PASS: Closed the changes_requested finding that `~LED:*` USB commands were swallowed by `board_consume_usb_command` before reaching `status_led_consume_usb_command`.

- `ports/esp32/ble_hid/ble_hid.c` now dispatches `status_led_consume_usb_command(line)` before `board_consume_usb_command(line)`.
- `components/board/board.c` no longer consumes the `LED:` prefix or returns a board-level blocked LED test stub; board LED resources remain observable through `~BOARD:STATUS`.
- `tools/verify_status_led_static.py` now fails if board USB dispatch can swallow `LED:` commands or if BLE HID dispatch orders `board_consume_usb_command(line)` before `status_led_consume_usb_command(line)`.

## Required Validation Commands

PASS:

```powershell
pwsh -NoProfile -File tools\verify_v2_board_profile_static.ps1
```

Output:

```text
PASS: V2 N16R8 board profile, memory defaults, pin map, four-zone LED resources, battery-side current telemetry, USB_Det divider policy, diagnostics, partitions, and package identity checks passed.
```

PASS:

```powershell
python tools\verify_status_led_static.py
```

Output:

```text
PASS: status LED static verification covers V2 four-zone WS2812 resources, EC11 GPIO5/count12, key GPIO13/count4, edge GPIO4/count6, diagnostics, USB validation hooks, and low-power off path.
```

## Additional Self-Review Checks

PASS:

```powershell
python tools\verify_power_manager_static.py
```

PASS:

```powershell
git diff --check
```

Result: no whitespace errors; only normal Windows LF-to-CRLF working-copy warnings.

PASS:

```powershell
python -m py_compile tools\verify_status_led_static.py tools\verify_power_manager_static.py
```

PASS:

```powershell
pwsh -NoProfile -File tools\build.ps1
```

Build summary: ESP-IDF build completed for `esp32s3`; generated flash command uses `--flash_size 16MB`; partition table has 6 MB `ota_0`, 6 MB `ota_1`, and 1 MB `diag_log`; app binary size `0xbec00`, with 88% free in the smallest app partition.

## Scope Notes

No hardware flash, serial, BLE, or LED photo validation was run for this step. The validation here is static/manual contract comparison plus an ESP-IDF firmware build.
