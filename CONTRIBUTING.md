# Contributing

This repository is an ESP-IDF firmware project for the Listener voice keyboard.

## Development

```powershell
pwsh -NoProfile -File .\tools\setup_windows.ps1
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
```

ESP-IDF downloads component-manager dependencies into `managed_components/`.
That directory is generated and should not be committed.

## Repository structure

- `components/` contains reusable controls, power, settings, health, OTA, and diagnostic logic.
- `ports/esp32/` binds product logic to ESP-IDF, the V2 board, audio, BLE, and storage.
- `protocols/` defines shared device messages and codecs.
- `main/` owns startup and subsystem wiring.
- `tools/` owns setup, build, flash, monitor, packaging, and validation entry points.
- `docs/features/firmware-feature-map.md` is the implementation and validation index.

Keep desktop policy in Listener Type. Firmware should report transport and device evidence without deciding final transcript ownership, writing style, or cursor insertion.

## Pull Requests

- Keep ESP-IDF bindings inside `ports/esp32/`.
- Keep reusable product logic in `components/` and `protocols/`.
- Do not commit `build/`, `managed_components/`, serial logs, validation
  artifacts, diagnostic bundles, or firmware binaries.
- Use PR text for validation evidence instead of adding local
  `.cache/validation` or legacy `docs/validation` output.
- State which hardware path was exercised and include the relevant counters or
  LED sequence for audio, BLE, power, and OTA changes.
