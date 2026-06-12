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
