# voice-keyboard-firmware

ESP-IDF seed firmware for the voice keyboard product.

## Quick Start

- New Windows machine bootstrap:
  `powershell -ExecutionPolicy Bypass -File .\tools\setup_windows.ps1`
- Build:
  `powershell -ExecutionPolicy Bypass -File .\tools\build.ps1`
- Flash:
  `powershell -ExecutionPolicy Bypass -File .\tools\flash.ps1 -Port COM5`
- Monitor in an interactive terminal:
  `powershell -ExecutionPolicy Bypass -File .\tools\monitor.ps1 -Port COM5`
- Capture boot logs in a non-interactive Codex session:
  `powershell -ExecutionPolicy Bypass -File .\tools\capture_serial.ps1 -Port COM5 -ResetBeforeRead`
- Send test input to firmware over serial without interactive monitor:
  `powershell -ExecutionPolicy Bypass -File .\tools\send_serial.ps1 -Port COM5 -Text "abc123"`
- Run a single non-interactive BLE HID runtime verification:
  `powershell -ExecutionPolicy Bypass -File .\tools\verify_ble_hid.ps1 -Port COM5 -Text "abc123"`

## Layout

- `main/`: thin `app_main()` entry
- `components/`: cross-platform product modules
- `drivers/`: device/peripheral semantic drivers
- `protocols/`: protocol definitions and codecs
- `ports/`: platform-specific SDK bindings
- `tools/`: build, flash, monitor, and helper scripts
- `tests/`: smoke checks
- `docs/`: human-facing product documents

## Key Docs

- `docs/development_setup.md`
- `docs/product_solutions.md`
