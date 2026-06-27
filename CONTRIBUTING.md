# Contributing

This repository is an ESP-IDF firmware project for the Listener voice keyboard.

## Development

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\setup_windows.ps1
powershell -ExecutionPolicy Bypass -File .\tools\build.ps1
powershell -ExecutionPolicy Bypass -File .\tools\flash.ps1 -Port COMx
```

ESP-IDF downloads component-manager dependencies into `managed_components/`.
That directory is generated and should not be committed.

## Pull Requests

- Keep ESP-IDF bindings inside `ports/esp32/`.
- Keep reusable product logic in `components/` and `protocols/`.
- Do not commit `build/`, `managed_components/`, serial logs, validation
  artifacts, diagnostic bundles, or firmware binaries.
- Use PR text for validation evidence instead of adding local
  `docs/validation` output.
