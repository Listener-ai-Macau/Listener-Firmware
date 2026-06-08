# voice-keyboard-production-readiness 4.3 oai3 validation summary

Status: AI-scriptable checks, flash, serial-toggle audio capture, and KEY1-KEY4 fallback evidence now pass. The remaining gate is human/fixture EC11 physical-key start/stop during the `physical-key` audio capture command, so this step is not submitted yet.

## 2026-06-08 rerun5

- Code change:
  - Added `physical_key_waiting_for_start` and `physical_key_waiting_for_stop` progress lines every 10 seconds in `tools/capture_audio_ble_wav.py` so long physical-key waits remain visible to operators and do not look like a hung validation process.
- PASS: `idf.py build` after loading the repo ESP-IDF environment.
  - `docs/validation/voice-keyboard-production-readiness-4.3/idf-build-20260608-oai3-rerun5-progress.log`
  - The first raw `idf.py build` attempt without `tools\idf_env.ps1` failed with missing `esp_idf_monitor`; this was a shell environment issue, then the same command passed after dot-sourcing `tools\idf_env.ps1`.
- PASS: `tools\verify_custom_key_command_hid.ps1`
  - `docs/validation/voice-keyboard-production-readiness-4.3/verify-custom-key-command-hid-20260608-oai3-rerun5.log`
- PASS: extra no-lock static coverage for V2 board profile, diagnostic log coverage, and voice recording control FSM.
  - `docs/validation/voice-keyboard-production-readiness-4.3/static-extra-20260608-oai3-rerun5-fixed.log`
- PASS: Python syntax check for the edited audio verification scripts.
  - `docs/validation/voice-keyboard-production-readiness-4.3/python-pycompile-20260608-oai3-rerun5-progress.log`
- PASS: `git diff --check`
  - `docs/validation/voice-keyboard-production-readiness-4.3/git-diff-check-20260608-oai3-rerun5-final.log`
- Hardware window evidence:
  - First rerun5 `aiw with-lock -Resource COMx` resolved `COMx` to `COM6`; no lock or process remained afterward.
  - PASS before interruption: `idf.py -p COM6 flash` exited 0.
  - PASS before interruption: scripted serial-toggle BLE audio capture exited 0 with `received_packet_count=344`, `expected_packet_count=344`, `missing_packet_count=0`, `best_corr=0.4533`, and `recorded_peak=712`.
  - NOT COMPLETE: the process stopped at the start of physical-key capture before EC11 start/stop evidence; this led to the progress-output fix above.
  - `docs/validation/voice-keyboard-production-readiness-4.3/with-lock-hardware-window-20260608-oai3-rerun5.log`
  - Second rerun5 hardware attempt did not enter the locked command because `COM6` was already actively locked by `oai1`; oai3 did not release or override another agent's lock.
  - `docs/validation/voice-keyboard-production-readiness-4.3/with-lock-hardware-window-20260608-oai3-rerun5-progress.log`
- This step is still not submitted because the validation command set still lacks PASS evidence for the physical-key EC11 audio capture.

## 2026-06-08 current attempt after PDM software gain

- Code changes:
  - Added saturating 8x software gain for the SPH0655 PDM path before BLE audio session emission; the PDM init log now reports `sw_gain=8`.
  - Kept the hardware PDM amplify field guarded by ESP-IDF SOC support, and added static checks so the gain assumptions cannot silently regress.
  - Made `serial-toggle` audio verification play the generated reference WAV during capture, so correlation validates the actual microphone path.
  - Accepted current EC11 GPIO11 and legacy GPIO35 physical-key source labels in physical-key log validation.
  - Allowed the BLE audio capture tool to treat runtime firmware markers as a ready fallback when the boot ready line was missed after flash.
- PASS: `idf.py build`
  - `docs/validation/voice-keyboard-production-readiness-4.3/idf-build-20260608-oai3-after-pdm-sw-gain.log`
- PASS: extra no-lock static coverage for V2 board profile, diagnostic log coverage, and voice recording control FSM.
  - `docs/validation/voice-keyboard-production-readiness-4.3/static-extra-20260608-oai3-after-pdm-sw-gain.log`
- PASS: Python syntax check for the edited audio verification scripts.
  - `docs/validation/voice-keyboard-production-readiness-4.3/python-pycompile-20260608-oai3-after-pdm-sw-gain.log`
- PASS: `tools\verify_custom_key_command_hid.ps1`
  - `docs/validation/voice-keyboard-production-readiness-4.3/verify-custom-key-command-hid-20260608-oai3-after-pdm-sw-gain.log`
  - `docs/validation/voice-keyboard-production-readiness-4.3/custom-key-command-hid-contract-20260608-oai3-after-pdm-sw-gain.md`
- PASS: `git diff --check`
  - `docs/validation/voice-keyboard-production-readiness-4.3/git-diff-check-20260608-oai3-final.log`
- Hardware window:
  - `aiw with-lock -Resource COMx` resolved the current unique serial resource to `COM6`; no workflow lock remained afterward.
  - PASS: `idf.py -p COM6 flash` exited 0.
  - PASS: scripted serial-toggle BLE audio capture exited 0 with `received_packet_count=342`, `expected_packet_count=342`, `missing_packet_count=0`, `best_corr=0.6693`, and `recorded_peak=832`.
  - PASS evidence inside the same serial log: KEY1-KEY4 physical presses queued F13/F14/F15/F16 fallback usages and sent HID usages `0x68`/`0x69`/`0x6A`/`0x6B`.
  - NOT PASS: `python tools\verify_audio_capture_session_end_to_end.py --port COM6 --capture-seconds 5 --trigger-mode physical-key` printed `ready_for_key=1`, then timed out after 300 seconds waiting for EC11 physical start. No EC11 recording start/stop press was observed.
  - `docs/validation/voice-keyboard-production-readiness-4.3/with-lock-hardware-window-20260608-oai3-after-pdm-sw-gain-retry.log`
- Audio artifacts from the passing serial-toggle run are under `tests/artifacts/audio/voice-keyboard-production-readiness-4.3-20260608-oai3/`.

## Current not completed

- The physical-key validation command still needs an operator or fixture to single-click the EC11 push switch once to start and once to stop the 5-second capture while the locked hardware script is running.
- This step was not submitted because the full validation command set did not reach PASS.

## 2026-06-08 rerun

- PASS: `idf.py build`
  - `docs/validation/voice-keyboard-production-readiness-4.3/idf-build-20260608-oai3-rerun.log`
- PASS: `tools\verify_custom_key_command_hid.ps1`
  - `docs/validation/voice-keyboard-production-readiness-4.3/verify-custom-key-command-hid-20260608-oai3-rerun.log`
  - `docs/validation/voice-keyboard-production-readiness-4.3/custom-key-command-hid-contract-20260608-oai3-rerun.md`
- PASS: extra no-lock static coverage for V2 board profile, diagnostic log coverage, and voice recording control FSM.
  - `docs/validation/voice-keyboard-production-readiness-4.3/static-extra-20260608-oai3-rerun.log`
- PASS: `git diff --check`
  - `docs/validation/voice-keyboard-production-readiness-4.3/git-diff-check-20260608-oai3-rerun.log`
- Hardware window script:
  - `docs/validation/voice-keyboard-production-readiness-4.3/run_locked_hardware_window_20260608_oai3.ps1`
- Hardware window result:
  - `aiw with-lock -Resource COMx` resolved `COMx` to `COM6`, acquired and released the lock.
  - Reset serial capture returned `<no serial output>`.
  - `idf.py -p COM6 flash` failed with `Failed to connect to ESP32-S3: No serial data received`.
  - Bootloader probe tried `default_reset`, `usb_reset`, and `no_reset` at 115200 and 460800 baud; all failed with `No serial data received`.
  - `docs/validation/voice-keyboard-production-readiness-4.3/with-lock-hardware-window-20260608-oai3-rerun.log`

## 2026-06-08 rerun2

- PASS: `idf.py build`
  - `docs/validation/voice-keyboard-production-readiness-4.3/idf-build-20260608-oai3-rerun2.log`
- PASS: `tools\verify_custom_key_command_hid.ps1`
  - `docs/validation/voice-keyboard-production-readiness-4.3/verify-custom-key-command-hid-20260608-oai3-rerun2.log`
  - `docs/validation/voice-keyboard-production-readiness-4.3/custom-key-command-hid-contract-20260608-oai3-rerun2.md`
- PASS: `git diff --check`
  - `docs/validation/voice-keyboard-production-readiness-4.3/git-diff-check-20260608-oai3-rerun2.log`
- Hardware window result:
  - `aiw with-lock -Resource COMx` resolved `COMx` to `COM6`, acquired and released the lock.
  - Reset serial capture returned `<no serial output>`.
  - `idf.py -p COM6 flash` failed with `Failed to connect to ESP32-S3: No serial data received`.
  - Bootloader probe again tried `default_reset`, `usb_reset`, and `no_reset` at 115200 and 460800 baud; all failed with `No serial data received`.
  - `docs/validation/voice-keyboard-production-readiness-4.3/with-lock-hardware-window-20260608-oai3-rerun2.log`

## 2026-06-08 rerun3

- PASS: `idf.py build`
  - `docs/validation/voice-keyboard-production-readiness-4.3/idf-build-20260608-oai3-rerun3.log`
- PASS: `tools\verify_custom_key_command_hid.ps1`
  - `docs/validation/voice-keyboard-production-readiness-4.3/verify-custom-key-command-hid-20260608-oai3-rerun3.log`
  - `docs/validation/voice-keyboard-production-readiness-4.3/custom-key-command-hid-contract-20260608-oai3-rerun3.md`
- PASS: `git diff --check`
  - `docs/validation/voice-keyboard-production-readiness-4.3/git-diff-check-20260608-oai3-rerun3.log`
- Hardware window result:
  - `aiw with-lock -Resource COMx` resolved `COMx` to `COM6`, acquired and released the lock.
  - Reset serial capture returned `<no serial output>`.
  - `idf.py -p COM6 flash` failed with `Failed to connect to ESP32-S3: No serial data received`.
  - Bootloader probe again tried `default_reset`, `usb_reset`, and `no_reset` at 115200 and 460800 baud; all failed with `No serial data received`.
  - `docs/validation/voice-keyboard-production-readiness-4.3/with-lock-hardware-window-20260608-oai3-rerun3.log`

## 2026-06-08 rerun4

- PASS: `idf.py build`
  - `docs/validation/voice-keyboard-production-readiness-4.3/idf-build-20260608-oai3-rerun4.log`
- PASS: `tools\verify_custom_key_command_hid.ps1`
  - `docs/validation/voice-keyboard-production-readiness-4.3/verify-custom-key-command-hid-20260608-oai3-rerun4.log`
  - `docs/validation/voice-keyboard-production-readiness-4.3/custom-key-command-hid-contract-20260608-oai3-rerun4.md`
- PASS: `git diff --check`
  - `docs/validation/voice-keyboard-production-readiness-4.3/git-diff-check-20260608-oai3-rerun4.log`
- Hardware window result:
  - `aiw with-lock -Resource COMx` resolved `COMx` to `COM6`, acquired and released the lock.
  - Reset serial capture returned `<no serial output>`.
  - `idf.py -p COM6 flash` failed with `Failed to connect to ESP32-S3: No serial data received`.
  - Bootloader probe again tried `default_reset`, `usb_reset`, and `no_reset` at 115200 and 460800 baud; all failed with `No serial data received`.
  - `docs/validation/voice-keyboard-production-readiness-4.3/with-lock-hardware-window-20260608-oai3-rerun4.log`

## Automated evidence

- PASS: `idf.py build`
  - `docs/validation/voice-keyboard-production-readiness-4.3/idf-build-20260607-oai3.log`
- PASS: `idf.py build` rerun after loading the repository ESP-IDF environment.
  - `docs/validation/voice-keyboard-production-readiness-4.3/idf-build-20260607-oai3-rerun-env.log`
- PASS: V2 board profile static contract for current N16R8 baseline, including KEY1-KEY4 GPIO38/GPIO39/GPIO40/GPIO41, EC11 key GPIO11, SPH0655 PDM mic path, and rejection of stale N4/SPH0645 defaults.
  - `docs/validation/voice-keyboard-production-readiness-4.3/verify-v2-board-profile-static-20260607-oai3.log`
  - `docs/validation/voice-keyboard-production-readiness-4.3/verify-v2-board-profile-static-20260607-oai3-rerun.log`
- PASS: diagnostic log coverage, including input debug and bounded diag export paths.
  - `docs/validation/voice-keyboard-production-readiness-4.3/verify-diagnostic-log-coverage-20260607-oai3.log`
  - `docs/validation/voice-keyboard-production-readiness-4.3/verify-diagnostic-log-coverage-20260607-oai3-rerun.log`
- PASS: voice recording control FSM model, including recovery transition coverage.
  - `docs/validation/voice-keyboard-production-readiness-4.3/verify-voice-recording-control-fsm-20260607-oai3.log`
  - `docs/validation/voice-keyboard-production-readiness-4.3/verify-voice-recording-control-fsm-20260607-oai3-rerun.log`
- PASS: custom key command/HID source contract. KEY1-KEY4 map to F13/F14/F15/F16 single-click fallback, F17-F20 double-click, F21-F24 long-press, stable diagnostic labels, and no active WASD/text fallback.
  - `docs/validation/voice-keyboard-production-readiness-4.3/custom-key-command-hid-contract-20260607-oai3.md`
  - `docs/validation/voice-keyboard-production-readiness-4.3/custom-key-command-hid-contract-20260607-oai3-rerun.md`
  - `docs/validation/voice-keyboard-production-readiness-4.3/verify-custom-key-command-hid-20260607-oai3-rerun.log`
- PASS: `git diff --check` before hardware waiting state.
- PASS: `git diff --check` rerun on 2026-06-07 after additional artifacts were added.

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
- 2026-06-07 rerun found exactly one ESP32 serial device, `COM6`, and used one short `aiw with-lock -Resource COMx` window for reset/capture plus `flash -NoBuild`.
  - Script: `docs/validation/voice-keyboard-production-readiness-4.3/run_locked_hardware_window_20260607_oai3.ps1`
  - Log: `docs/validation/voice-keyboard-production-readiness-4.3/with-lock-hardware-window-20260607-oai3-rerun5.log`
  - Result: reset/capture returned `<no serial output>`; flash failed with `Failed to connect to ESP32-S3: No serial data received`.
- 2026-06-07 bootloader probe exhausted scriptable reset variants without flashing.
  - Script: `docs/validation/voice-keyboard-production-readiness-4.3/run_bootloader_probe_20260607_oai3.ps1`
  - Log: `docs/validation/voice-keyboard-production-readiness-4.3/with-lock-bootloader-probe-20260607-oai3.log`
  - Result: `default_reset`, `usb_reset`, and `no_reset` at 115200 and 460800 all failed `chip_id` with `No serial data received`.
- No workflow lock remained after the attempts.
- Process command-line search did not find an obvious remaining `COM6`, monitor, capture, flash, idf, or esptool process from this worktree.

## Prior blocker status

- The earlier bootloader/flash blocker is no longer current. The 2026-06-08 PDM software-gain retry flashed successfully through `aiw with-lock -Resource COMx`.
- `tools\verify_physical_custom_key_hid.ps1` remains useful for host-visible HID confirmation, but the current hardware log already contains KEY1-KEY4 physical fallback firmware/HID evidence. The remaining scripted validation gap is EC11 physical-key audio start/stop.
- This step was not submitted because the full validation command set did not reach PASS.

## Current unblock condition

Rerun the short locked 2026-06-08 hardware window with the current unique ESP32 serial device, and have an operator or fixture single-click EC11 once to start and once to stop the physical-key capture when prompted:

1. `pwsh -NoProfile -File C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COMx -Owner oai3 -Run pwsh -NoProfile -File C:\Users\Billy\Desktop\listener\voice-keyboard-firmware-wt-oai3-voice-keyboard-production-readiness-4.3\docs\validation\voice-keyboard-production-readiness-4.3\run_locked_hardware_window_20260608_oai3.ps1 -Port COMx`
2. If `physical-key-audio-capture exit_code=0`, rerun the no-lock static validations listed above if code changed, then submit the step with the new PASS evidence.
