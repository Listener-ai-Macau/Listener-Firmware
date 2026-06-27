# Voice Keyboard Firmware - Claude Guide

Use the same repository facts and constraints as `AGENTS.md`.

Key rules:

- Keep `main/` small and move reusable logic into `components/` or
  `protocols/`.
- Keep ESP-IDF-specific code in `ports/esp32/`.
- Preserve the `audio_data` session protocol and BLE subscription order.
- Keep public docs in `!docs/` focused on stable product behavior, not local
  workflow state or validation logs.

Common checks:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build.ps1
powershell -ExecutionPolicy Bypass -File .\tools\flash.ps1 -Port COMx
powershell -ExecutionPolicy Bypass -File .\tools\capture_serial.ps1 -Port COMx -ResetBeforeRead
```
