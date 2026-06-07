# Step 1.2 Hardware Gate: No ESP32 Serial Port

Timestamp: 2026-06-08T00:47:06+08:00

Plan: voice-keyboard-camera-status-key-led-tuning
Step: 1.2
Owner: oai1

## No-Lock Evidence Completed

- `python tools\verify_status_led_static.py` -> PASS
- `pwsh -NoProfile -File .\tools\status_led_camera_calibration.ps1 -Camera dry-run -Zones status,key -Mode rgbw-single-led -ManifestOnly -VerifyMapping` -> PASS
- `python -m compileall -q tools` -> PASS
- `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check` -> PASS
- `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3` -> PASS
- `git diff --check` -> PASS

The dry-run artifact was retained as `dry-run-manifest.json` only. The final `manifest.json` is intentionally left for real camera validation so dry-run output cannot be mistaken for accepted RGBW evidence.

## Blocking Hardware Command

```powershell
pwsh -NoProfile -File ..\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COMx -Run pwsh -NoProfile -File .\tools\status_led_camera_calibration.ps1 -Port COMx -Camera auto -Zones status,key -Mode rgbw-single-led -VerifyMapping
```

Result: FAIL before firmware/camera validation could start.

Symptom:

```text
COMx placeholder requires exactly one current openable ESP32/serial port. waiting for any serial/COM device
```

Independent serial probe:

```powershell
python -m serial.tools.list_ports -v
```

Output:

```text
no ports found
```

## Resume Condition

Resume when exactly one openable ESP32 serial port is visible. Then rerun the blocking hardware command above; `COMx` should resolve to the current unique serial port through `aiw with-lock`.
