# Serial No-Reset Diagnostics

Firmware diagnostic tools must not reset the board just by opening a USB serial
port. On ESP32-S3 USB serial/JTAG, reset-prone DTR/RTS defaults can produce
`USB_UART_CHIP_RESET`, making a healthy board look like it disappeared or
rebooted.

## Contract

- Ordinary diagnostic, status, capture, and validation tools must open serial
  ports with DTR and RTS deasserted.
- Python tools must prefer the no-reset pattern: create `serial.Serial()`, set
  `port`, `baudrate`, `timeout`, `dsrdtr=False`, `rtscts=False`, `dtr=False`,
  and `rts=False`, then call `open()`. Reassert `dtr=False` and `rts=False`
  immediately after opening.
- Do not use `serial.Serial(port=...)`, `serial.Serial("COMx", ...)`, or
  `serial.Serial(port, ...)` for ordinary diagnostics because those forms open
  before DTR/RTS can be forced low.
- Only explicit reset/flash paths may intentionally toggle RTS/DTR, and their
  command names or parameters must make that reset behavior clear.
- When investigating USB disappearance, treat `rst:0x15 (USB_UART_CHIP_RESET)`
  or `reset_reason=usb(11)` as a host/tool/USB reset path unless power-manager
  diagnostics also show `DIAG_POWER_SLEEP_ENTRY` or `HARDWARE_SHUTDOWN`.

## Guardrail

Run this static check before merging serial-tool changes:

```powershell
python .\tools\verify_serial_no_reset_static.py
```

The check rejects reset-prone PySerial open patterns under `tools/` and verifies
the status LED validation tools use no-reset serial opens.

## Incident record: 2026-09-06/07

The apparent "recent spontaneous reboot" was split into two different signals:

- The live serial capture `tests/artifacts/serial现场_20260906-latest-50s.log`
  began with `rst:0x15 (USB_UART_CHIP_RESET)`. It had no retained watchdog
  timeout, and the firmware survived the 30-second boot-safety window and the
  remaining capture without another reset. The Type log at the same time only
  shows a BLE disconnect followed by its normal reconnect path.
- That capture was made with positional `serial.Serial("COM5", ...)`, which
  opens the port before DTR/RTS can be forced low. This violates the contract
  above and is sufficient to produce the USB reset observed in the capture.
- The bounded capture on 2026-09-07 decoded the same boot as
  `reset_reason=usb(11)`; it is not evidence of a new interrupt/task watchdog.
  Small `system`-only tails that contain old `interrupt_wdt(5)` records are RTC
  retained history unless a new boot segment and matching boot-safety record are
  observed.
- The 2026-09-07 capture used the checked-in PowerShell/.NET sequence (DTR/RTS
  disabled before `Open`) and still returned a fresh USB boot tail. Therefore the
  no-reset serial contract is a necessary guard, but not a proof that this
  board/driver combination is observationally side-effect free. Until that
  host/USB path is isolated, treat opening COM5 itself as a possible reset
  stimulus and prefer already-running Type telemetry or previously captured
  artifacts for product behavior conclusions.

This is a tooling/observation failure, not a product-level watchdog root cause.
Future diagnosis must use the no-reset constructor/open sequence above and must
classify reset causes from the live boot line plus boot-safety record before
changing firmware scheduling or watchdog policy.
