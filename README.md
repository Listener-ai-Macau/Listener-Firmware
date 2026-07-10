# voice-keyboard-firmware

ESP-IDF seed firmware for the voice keyboard product.

## Quick Start

- New Windows machine bootstrap:
  `pwsh -NoProfile -File .\tools\setup_windows.ps1`
- Build:
  `pwsh -NoProfile -File .\tools\build.ps1`
- Flash:
  `pwsh -NoProfile -File .\tools\flash.ps1 -Port COM5`
- Flash bootloader only:
  `pwsh -NoProfile -File .\tools\flash_bootloader.ps1 -Port COMx`
- Monitor in an interactive terminal:
  `pwsh -NoProfile -File .\tools\monitor.ps1 -Port COM5`
- Capture boot logs in a non-interactive Codex session:
  `pwsh -NoProfile -File .\tools\capture_serial.ps1 -Port COM5 -ResetBeforeRead`
- Send test input to firmware over serial without interactive monitor:
  `pwsh -NoProfile -File .\tools\send_serial.ps1 -Port COM5 -Text "abc123"`
- Run a single non-interactive BLE HID runtime verification:
  `pwsh -NoProfile -File .\tools\verify_ble_hid.ps1 -Port COM5 -Text "abc123"`
- Serial diagnostics must avoid reset-prone DTR/RTS opens; see
  `docs\features\serial_no_reset_diagnostics.md`.

## Layout

- `main/`: thin `app_main()` entry
- `components/`: cross-platform product modules
- `drivers/`: device/peripheral semantic drivers
- `protocols/`: protocol definitions and codecs
- `ports/`: platform-specific SDK bindings
- `tools/`: build, flash, monitor, and helper scripts
- `tests/`: smoke checks
- `docs/`: product, feature, maintenance, and release documents

## Key Docs

- `docs/features/firmware-feature-map.md`
- `docs/tools/device_maintenance.md`
- `docs/ota_manifest_schema.md`
