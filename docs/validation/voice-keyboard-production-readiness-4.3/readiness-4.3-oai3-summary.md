# voice-keyboard-production-readiness 4.3 oai3 validation summary

Status: hardware waiting after AI-scriptable checks and follow-up LED/camera probing.

## Automated evidence

- PASS: `idf.py build`
  - `docs/validation/voice-keyboard-production-readiness-4.3/idf-build-20260607-oai3.log`
- PASS: V2 board profile static contract for current N16R8 baseline, including KEY1-KEY4 GPIO38/GPIO39/GPIO40/GPIO41, EC11 key GPIO11, SPH0655 PDM mic path, and rejection of stale N4/SPH0645 defaults.
  - `docs/validation/voice-keyboard-production-readiness-4.3/verify-v2-board-profile-static-20260607-oai3.log`
- PASS: diagnostic log coverage, including input debug and bounded diag export paths.
  - `docs/validation/voice-keyboard-production-readiness-4.3/verify-diagnostic-log-coverage-20260607-oai3.log`
- PASS: voice recording control FSM model, including recovery transition coverage.
  - `docs/validation/voice-keyboard-production-readiness-4.3/verify-voice-recording-control-fsm-20260607-oai3.log`
- PASS: custom key command/HID source contract. KEY1-KEY4 map to F13/F14/F15/F16 single-click fallback, F17-F20 double-click, F21-F24 long-press, stable diagnostic labels, and no active WASD/text fallback.
  - `docs/validation/voice-keyboard-production-readiness-4.3/custom-key-command-hid-contract-20260607-oai3.md`
- PASS: `git diff --check` before hardware waiting state.

## Hardware evidence

- The initial `aiw run-validation` attempt is preserved only as placeholder/tooling failure evidence, not as the final PASS validation report:
  - `docs/validation/voice-keyboard-production-readiness-4.3/run-validation-placeholder-failure-20260607-oai3.json`
- Current action-time serial discovery found exactly one Windows serial device: `COM6`.
- `aiw with-lock -Resource COMx` resolved `COMx` to `COM6`, acquired the lock, ran flash, and released the lock.
- Flash did not complete because the device could not be opened by esptool:
  - `docs/validation/voice-keyboard-production-readiness-4.3/with-lock-flash-20260607-oai3.log`
  - Symptom: `Could not open COM6, the port is busy or doesn't exist` / `PermissionError(13)`.
- A separate short `aiw with-lock -Resource COM6` serial-open retry also acquired and released the lock, but `SerialPort.Open()` failed with Windows device-not-functioning error:
  - `docs/validation/voice-keyboard-production-readiness-4.3/with-lock-serial-open-retry-20260607-oai3.log`
- Follow-up on 2026-06-07 found COM6 openable and used the existing LED validation command surface with camera evidence. No production LED effect code was changed.
  - `docs/validation/voice-keyboard-production-readiness-4.3/led-camera/led-camera-evidence-20260607-oai3.md`
  - `docs/validation/voice-keyboard-production-readiness-4.3/led-camera/with-lock-led-camera-20260607-oai3.log`
  - `docs/validation/voice-keyboard-production-readiness-4.3/led-camera/led-factory-rgbw-camera0-20260607-222433.jpg`
  - Visual result: the factory/RGBW command window visibly lit the four key LEDs and lower strip. Bright pixels `gray>=220` increased from 2880 in the baseline probe to 17625 in the factory/RGBW frame.
- Follow-up serial diagnostics still returned no RX text after LED/board/power commands:
  - `docs/validation/voice-keyboard-production-readiness-4.3/led-camera/with-lock-led-status-capture-20260607-oai3.log`
- A follow-up flash retry opened COM6 but failed to connect to the ESP32-S3 bootloader:
  - `docs/validation/voice-keyboard-production-readiness-4.3/with-lock-flash-retry-20260607-oai3.log`
  - Symptom: `Failed to connect to ESP32-S3: No serial data received`.
- No workflow lock remained after the attempts.
- Process command-line search did not find an obvious remaining `COM6`, monitor, capture, flash, idf, or esptool process from this worktree.

## Not completed

- `idf.py -p <current-esp32-com> flash` cannot pass until the current ESP32 serial device enters the ESP32-S3 bootloader and returns serial data. COM6 is now openable, but the bootloader connect attempt returns no serial data.
- `python tools\verify_audio_capture_session_end_to_end.py --port <current-esp32-com> --capture-seconds 5 --trigger-mode physical-key` cannot run until the device is openable and the physical-key trigger can be performed by a human or jig.
- `tools\verify_physical_custom_key_hid.ps1` remains the host-visible physical fallback check for KEY1-KEY4. The new command/HID contract script covers source-level and verifier-level evidence before that final physical check.

## Unblock condition

Make the current unique ESP32 serial device enter bootloader/diagnostic serial reliably, then rerun the short locked hardware window:

1. `pwsh -NoProfile -File C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COMx -Run pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx -NoBuild`
2. `pwsh -NoProfile -File C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COMx -Run pwsh -NoProfile -File .\tools\verify_audio_capture_session_end_to_end.ps1 -Port COMx -CaptureSeconds 5 -PhysicalKey`
3. Run `pwsh -NoProfile -File .\tools\verify_physical_custom_key_hid.ps1 -Port COMx -OutputPath docs\validation\voice-keyboard-production-readiness-4.3\physical-custom-key-hid-after-unblock.md` if host-visible KEY1-KEY4 fallback evidence is required before acceptance.
