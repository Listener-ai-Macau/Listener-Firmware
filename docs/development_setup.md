# Development Setup

This repository targets Windows first and assumes `ESP-IDF` lives at `%USERPROFILE%\esp\esp-idf`.

## New Machine Bootstrap

Run this from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\setup_windows.ps1
```

What this script does:

- installs `Git` with `winget` if missing
- installs `Python 3.10` with `winget` if missing
- clones `ESP-IDF` to `%USERPROFILE%\esp\esp-idf` if missing
- syncs `ESP-IDF` submodules
- installs the `esp32s3` toolchain and Python environment into `%USERPROFILE%\.espressif`
- verifies that `idf.py --version` runs successfully

## Standard Workflow

Build:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build.ps1
```

Flash:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash.ps1 -Port COM5
```

Interactive monitor:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\monitor.ps1 -Port COM5
```

## Monitor Note

`idf.py monitor` requires an interactive TTY.

- In a normal terminal window, use `tools/monitor.ps1`.
- In a non-interactive Codex session, use `tools/capture_serial.ps1` to grab boot logs without TTY support.
- If the firmware test path reads from `stdin`, use `tools/send_serial.ps1` to inject test bytes without relying on a human typing into `monitor`.
- For the standard Codex runtime check, use `tools/verify_ble_hid.ps1` to reset the board, capture boot logs, inject test bytes, and summarize whether the HID path ran.

Example:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\capture_serial.ps1 -Port COM5 -ResetBeforeRead
```

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\send_serial.ps1 -Port COM5 -Text "abc123"
```

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify_ble_hid.ps1 -Port COM5 -Text "abc123"
```

## Board Detection

The current bring-up board shows up as an Espressif USB serial device similar to:

- `USB\VID_303A&PID_1001`
- serial port `COM5` on the current machine
