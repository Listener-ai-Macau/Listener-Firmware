# Firmware Log Inspector Validation

Step: firmware-log-inspector/1.1
Agent: Tai
Date: 2026-05-25

## Change

Added `tools/inspect_firmware_log.ps1`, a read-only firmware log inspection helper for saved ESP-IDF/serial logs and explicit live serial captures.

The script reports:
- known Windows serial ports
- selected source: saved log file or `-Port COMx -LiveSeconds n`
- summary counts for boot, warnings/errors, BLE/HID, audio, and voice-key lines
- latest reset/boot, crash, memory, BLE/HID, audio, and voice-key evidence lines
- recent warnings/errors, recent key events, and tail output

It also supports:
- `-LogPath <file>` for saved logs
- `-Port COMx -LiveSeconds <seconds>` for explicit live serial capture
- `-ListPorts`
- `-Tail <n>`
- `-Events <n>` aliasing to event count
- `-ScanLines <n>`
- `-RawTailOnly`

## Validation Commands

```powershell
pwsh -NoProfile -Command '$errors=$null; $null=[System.Management.Automation.PSParser]::Tokenize((Get-Content -Raw .\tools\inspect_firmware_log.ps1), [ref]$errors); if ($errors) { $errors; exit 1 }'
pwsh -NoProfile -File .\tools\inspect_firmware_log.ps1 -LogPath .\tests\artifacts\sample_firmware_log.txt -Tail 20 -Events 20
pwsh -NoProfile -File .\tools\inspect_firmware_log.ps1 -ListPorts
pwsh -NoProfile -File .\tools\inspect_firmware_log.ps1 -Port COM5 -LiveSeconds 10 -Tail 80 -Events 80
git diff --check
```

## Result

PASS.

Observed sample-log smoke summary:
- known serial ports: `COM5`
- boot lines: 4
- BLE/HID lines: 4
- audio lines: 4
- voice key lines: 2
- warnings: 1
- errors: 1
- last memory issue: `ESP_ERR_NO_MEM`
- last voice-key event: `GPIO35 voice_key ready`

Observed real-device COM5 live capture:
- locked COM5 through workflow before capture and unlocked afterwards
- 10-second serial capture succeeded
- 21 serial log lines captured
- errors: 0
- warnings: 0
- audio lines: 11
- latest captured line included `audio_capture: frame captured count=166000 bytes=106240000`
