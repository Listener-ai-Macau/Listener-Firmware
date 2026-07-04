# Voice Keyboard Firmware - Agent Guide

This repository contains ESP32-S3 firmware for the Listener voice keyboard.

## Project Map

- `main/` keeps `app_main()` thin.
- `components/` contains cross-platform product logic.
- `protocols/` contains protocol definitions, codecs, and error models.
- `ports/esp32/` contains ESP-IDF bindings.
- `tools/` contains build, flash, monitor, diagnostic, and validation helpers.
- `!docs/` contains stable product and feature documentation.

## Build

```powershell
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
pwsh -NoProfile -File .\tools\monitor.ps1 -Port COMx
```

Do not run bare `idf.py` in this repo. Use `pwsh -NoProfile -File .\tools\idf.ps1 <idf.py args>`
for ad hoc ESP-IDF commands so `tools\idf_env.ps1` always loads `export.ps1` first and avoids
`No module named 'esp_idf_monitor'` from a non-IDF shell.

The project uses ESP-IDF component manager dependencies from
`idf_component.yml` files and `dependencies.lock`. Do not commit
`managed_components/` or `build/`.

## Constraints

- Keep product logic portable: ESP-IDF details stay under `ports/esp32/`.
- Do not reinterpret `audio_data` as the older `chunk + fragment` model.
- Keep the host subscription order `CCCD notify -> ValueChanged`.
- Keep subscribe-before-connect compatibility.
- Use `snake_case` for files, functions, and variables.

## Validation

Prefer the narrowest relevant check:

```powershell
pwsh -NoProfile -File .\tools\build.ps1
python .\tools\verify_audio_ble_product_matrix.py --list-cases
pwsh -NoProfile -File .\tools\capture_serial.ps1 -Port COMx -ResetBeforeRead
```

Do not commit generated validation logs, serial captures, diagnostic packages,
build outputs, or firmware binaries.
