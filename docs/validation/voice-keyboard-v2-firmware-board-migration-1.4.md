# Voice Keyboard V2 Firmware Board Migration 1.4 Validation

Owner: oai2
Date: 2026-06-06
Hardware resources used: COM6, BLE-A4CB8FF459A6
Artifact root: `tests/artifacts/voice-keyboard-v2-firmware-board-migration-1.4-oai2/20260606-182357`
Follow-up artifact root: `tests/artifacts/voice-keyboard-v2-firmware-board-migration-1.4-oai2/20260606-1909-user-led-keys`
Latest V2 pin recheck artifact root: `tests/artifacts/voice-keyboard-v2-firmware-board-migration-1.4-oai2/20260606-ec11-gpio11-key-recheck`
Input rework follow-up artifacts: `tests/artifacts/v2_input_rework_*_20260606_oai2.log`
EC11 direction follow-up artifacts: `tests/artifacts/v2_ec11_direction_fix_*_20260606_oai2.log`
EC11 flash-log follow-up artifacts: `tests/artifacts/diag_flash_ec11_direction_20260606_oai2/`
Input debug flash-log follow-up artifacts: `tests/artifacts/diag_flash_input_debug_20260606_oai2/`
Submit refresh artifact root: `tests/artifacts/voice-keyboard-v2-firmware-board-migration-1.4-oai2/20260607-current`
Final merge refresh artifact root: `tests/artifacts/voice-keyboard-v2-firmware-board-migration-1.4-oai2/20260607-final-d03a925`
Rework refresh artifact root: `tests/artifacts/voice-keyboard-v2-firmware-board-migration-1.4-oai2/20260607-rework-nondestructive-gpio`

Status: re-submit-ready for 1.4 AI evidence ladder. Residual final visible-effect, meter/fixture, EC11 push hand-feel, destructive watchdog, and product closure gates are explicitly deferred to 1.5.

## Summary

The automated and non-destructive hardware checks found one real firmware defect and fixed it in this branch:

- `components/status_led/status_led.c` used `mem_block_symbols = 64`, which consumes two ESP32-S3 RMT memory blocks per strip. On V2 this allowed only the status and EC11 strips to initialize; KEY GPIO13 and edge GPIO4 failed with `no free tx channels`.
- The fix uses `SOC_RMT_MEM_WORDS_PER_CHANNEL` and serializes LED frame transmission with a TX mutex.
- `tools/verify_status_led_static.py` now rejects the 64-symbol regression.
- `tools/flash.ps1` now honors `LISTENER_IDF_BUILD_DIR`, which lets the plan's flash validation run from this long worktree path.
- Follow-up user-observed LED/key testing added `~BOARD:GPIO`, a read-only raw GPIO diagnostic for KEY1-KEY4 and EC11 A/B/key.
- Latest V2 hardware repository recheck found the firmware was still using an obsolete EC11 push/wake mapping. Current hardware docs/schematic/PCB expose `EC11-KEY_IO` on `GPIO11` and `PWR_HOLD` on `GPIO46`; this branch now matches that mapping.
- Follow-up KEY4 physical debugging confirmed the V2 schematic and firmware both map `KEY-4` to `GPIO41`. The apparent missed key presses were caused by the custom-key debounce window: normal observed KEY4 low pulses were about 80-140 ms, while firmware required 8 samples at 20 ms, about 160 ms. `KEYBOARD_CUSTOM_DEBOUNCE_SAMPLES` is now 3, matching the historical physical key debounce class and allowing short physical presses to reach stable press/release handling.
- Latest V2 schematic recheck found no KEY1-KEY4 or EC11 A/B hardware debounce network. `C1..C4` on the key area are 100 nF `VDD_LED` to `VSS` decouplers for the key LEDs, not key-signal debounce capacitors. Firmware now treats KEY1-KEY4, EC11 A/B, and the runtime EC11 push input as mechanical contacts that require software debounce/filtering.
- Follow-up all-key GPIO scan after the no-hardware-debounce input rework physically validated KEY1/GPIO38, KEY2/GPIO39, KEY3/GPIO40, and KEY4/GPIO41. The same pass did not capture EC11 A/B rotation or EC11 push/GPIO11 events, so EC11 remains an open physical-control evidence gap.
- Follow-up user testing found the EC11 physical rotation direction was inverted. This branch now flips the A/B quadrature sign at the decoder layer so the existing logical policy remains `CW -> increase` and `CCW -> decrease` for both system volume and screen brightness. A DTR-asserted logical dispatch smoke validated the HID usages, but the follow-up physical capture still did not record EC11 A/B edges, so final physical direction sign-off remains open.
- Follow-up flash-log debugging found the normal flash diag schema was too sparse for input bring-up. This branch adds a runtime, default-off input debug switch: `~DIAGLOG:INPUTDBG:ON`, `~DIAGLOG:INPUTDBG:OFF`, and `~DIAGLOG:INPUTDBG:STATUS`. When enabled, flash diag records KEY1-KEY4 raw/stable transitions, EC11 A/B transition/invalid/partial/dispatch details, and EC11 push raw/stable details. It is intended for hardware debugging only and should be turned off after input sign-off to avoid filling the flash ring with high-frequency input noise.
- Submit refresh found `tools/flash.ps1` still defaulted to the long worktree-local `build` directory while `tools/build.ps1` defaulted to a short temp build directory. The first current locked flash attempt failed in the long build path before touching hardware state. `flash.ps1` now uses the same short-build selection as `build.ps1`, while still honoring `LISTENER_IDF_BUILD_DIR` when explicitly supplied.
- Final merge refresh integrated current `master`, preserving the accepted hardware-shutdown path while applying the latest V2 pin evidence: `PWR_HOLD/GPIO46` is the power latch/shutdown output, and `EC11-KEY/GPIO11` remains the runtime recording input. This intentionally supersedes the older plan text that listed `PWR_HOLD/GPIO11` and `EC11-KEY_IO/GPIO18`.
- Reviewer rework found `~BOARD:GPIO` still called `board_configure_status_input()` on EC11 A/B, which disabled the `GPIO_INTR_ANYEDGE` ISR configuration used by the EC11 runtime path. The diagnostic now reads KEY1-KEY4 and EC11 A/B/key as currently configured, prints `mode=read_as_configured reconfigure=0`, and `verify_v2_board_profile_static.ps1` rejects EC11 A/B reconfiguration inside `board_print_gpio_status()`.

1.4 is complete as an AI-run evidence ladder: static checks, build, flash, serial capture, diag_log dump/decode, BLE HID static contract, diagnostic injection, and available physical-key/EC11 flash-backed evidence are captured. Remaining real-world visible-effect and repair/product decisions belong to 1.5.

## Validation Commands

Plan placeholders were resolved as:

- `COMx` -> `COM6`
- `BLE-ADDR` -> `BLE-A4CB8FF459A6`

Results:

| Check | Result | Evidence |
|---|---|---|
| Flash with workflow lock | PASS | `flash_after_led_rmt_fix_with_lock.txt` |
| Serial monitor/status capture | PASS with known degraded audio warnings | `boot_after_led_rmt_fix_esptool_reset_with_lock.txt`, `board_led_power_ota_wdt_boot_status_after_fix_with_lock.txt` |
| Diag log dump | PASS, 783 retained events dumped | `dump_diag_log_after_fix_with_lock.txt`, `diag_log_20260606-183521.jsonl` |
| BLE HID script dispatch | PASS | `verify_ble_hid_after_fix_no_reset_encoded_with_lock.txt` |
| V2 current telemetry | PASS | `v2_current_telemetry_20260606-183933.md`, `v2_current_telemetry_20260606-183933.json` |
| OTA GATT discovery after fix | BLOCKED/FAIL on Windows WinRT uncached discovery | `verify_ble_ota_gatt_discovery_after_fix_with_lock.txt`, `verify_ble_ota_gatt_discovery_after_fix_skip_prime_with_lock.txt`, `probe_ble_ota_gatt_after_fix_with_lock.txt` |
| Follow-up LED and GPIO diagnostic flash | PASS, diagnostic command added and flashed | `flash_board_gpio_diag_with_lock.txt` |
| Follow-up live key/EC11 GPIO polling | BLOCKED/FAIL, no physical GPIO change observed | `live_board_gpio_key_ec11_poll_60s_with_lock.txt` |
| Latest V2 hardware pin-map recheck | PASS, latest hardware repo/PDF/SchDoc/PCB tokens confirm KEY1-4 GPIO38-41, EC11 A/B GPIO42/GPIO2, EC11 key GPIO11, PWR_HOLD GPIO46, EC11 RGB GPIO5 | local hardware repo HEAD `5a16e2e`, origin/master `234304b` |
| Flash after EC11 GPIO11/PWR_HOLD GPIO46 correction | PASS | `flash_ec11_gpio11_with_lock.txt` |
| Live key/EC11 GPIO polling after EC11 GPIO11 correction | BLOCKED/FAIL, no physical GPIO change observed across 90s and 180s locked captures | `serial_gpio_recheck_90s_with_lock.txt`, `serial_gpio_recheck_180s_with_lock.txt` |
| Persistent diag_log dump after post-fix polling | BLOCKED/FAIL, no persisted physical KEY1-4, EC11 detent, or voice-key press events | `dump_diag_log_after_180s_with_lock.txt`, `diag_log_20260606-194641.jsonl` |
| KEY4 synchronized GPIO/debounce repro before debounce fix | PASS repro, `GPIO41` low and `custom key raw transition` observed but no stable event because the press was shorter than the 8-sample debounce threshold | live locked COM6 capture in oai2 transcript, 2026-06-06 |
| Build after KEY1-KEY4 debounce fix | PASS | `pwsh -NoProfile -File .\tools\build.ps1`, temp build dir `C:\Users\Billy\AppData\Local\Temp\listener-idf-build-36ea52e79d27` |
| Flash after KEY1-KEY4 debounce fix | PASS | locked COM6 flash, ESP32-S3 MAC `a4:cb:8f:f4:59:a4`, image hash verified |
| KEY4 synchronized GPIO/debounce validation after debounce fix | PASS | locked COM6 capture: `key4_level=low key4_pressed=1`, `custom key stable transition ... pressed=1`, `custom key stable transition ... pressed=0`, `custom key fallback queued ... usage=F16 gesture=single`, plus double/long gesture logs during repeated operator input |
| Latest V2 schematic debounce review | PASS static | Hardware repo local `master` `cdcbabb` is newer than `origin/master`; SchDoc/interface review confirms KEY1-KEY4 are GPIO38/39/40/41, EC11 A/B are GPIO42/GPIO2, EC11 push MCU input is GPIO11, and key-area `C1..C4` are LED rail decouplers rather than switch debounce capacitors |
| Static/build after V2 no-hardware-debounce input rework | PASS | `verify_v2_board_profile_static.ps1`, `verify_status_led_static.py`, `verify_power_manager_static.ps1`, `verify_charging_awake_policy_static.ps1`, `verify_diagnostic_log_coverage.ps1`, `verify_ble_ota_gatt_contract.py`, `git diff --check`, and `tools/build.ps1 -Target esp32s3`; app `0xc0260`, 87% free |
| Flash after V2 no-hardware-debounce input rework | PASS | locked COM6 flash from short temp build dir `C:\Users\Billy\AppData\Local\Temp\listener-idf-build-36ea52e79d27`; ESP32-S3 MAC `a4:cb:8f:f4:59:a4`; image hashes verified |
| DTR-asserted USB serial status after input rework | PASS | `v2_input_rework_status_dtr_true_20260606_oai2.log`; `~BOOT:STATUS safe_mode=0`, `~BOARD:STATUS key_gpios=38,39,40,41 ec11_a_gpio=42 ec11_b_gpio=2 ec11_key_gpio=11 pwr_hold_gpio=46` |
| KEY1-KEY4 physical GPIO scan after input rework | PASS for KEY1-KEY4, EC11 not captured | `v2_input_rework_gpio_scan_dtr_true_20260606_oai2.log`; `GPIO_SCAN_CHANGED gpio38:KEY1 gpio39:KEY2 gpio40:KEY3 gpio41:KEY4`; stable transitions and F13/F14/F15/F16 HID usage logs observed |
| EC11 physical follow-up capture after input rework | BLOCKED/FAIL, no EC11 physical event captured | `v2_input_rework_ec11_live_dtr_true_45s_20260606_oai2.log`; grep found no `EC11 transition`, `EC11 detent`, `EC11 rotation queued`, `recording gesture`, or GPIO11 press log lines |
| EC11 direction fix static/build/flash | PASS | `git diff --check` passed with CRLF warnings only; `verify_v2_board_profile_static.ps1` PASS; `verify_status_led_static.py` PASS; `tools/build.ps1 -Target esp32s3` PASS from short build dir with app `0xc02a0`, 87% free; locked COM6 flash PASS with image hashes verified |
| EC11 direction logical dispatch smoke | PASS | `v2_ec11_direction_fix_logical_dispatch_dtr_true_20260606_oai2.log`; `system_volume direction=cw usage=0x00E9`, `system_volume direction=ccw usage=0x00EA`, `screen_brightness direction=cw usage=0x006F`, `screen_brightness direction=ccw usage=0x0070`, all with `connected=yes` send completion |
| EC11 direction physical follow-up capture | BLOCKED/FAIL, no EC11 physical event captured | `v2_ec11_direction_fix_physical_dtr_true_20260606_oai2.log`; `~BOARD:GPIO` reported EC11 A/B idle high and both GPIO scans changed only gpio17/gpio43, with no `EC11 transition`, `EC11 detent`, or `EC11 rotation queued` lines |
| EC11 flash-backed diag_log follow-up | BLOCKED/FAIL, no persisted EC11 detent event captured | `diag_flash_ec11_direction_20260606_oai2/diag_log_dump_raw.jsonl` contains 670 flash-backed diag events, but `src=keyboard evt=4` / `DIAG_KBD_EC11_DETENT` count is 0. A follow-up 60 second DTR-true live capture in the same artifact directory saw only `~EC11:STATUS`, not physical `EC11 transition`, `EC11 detent`, or `EC11 rotation queued` lines. |
| Input debug flash-log switch | PASS for command path, KEY1-KEY4 physical tail capture, EC11 physical tail capture, and off control; EC11 push not captured yet | `diag_flash_input_debug_20260606_oai2/inputdbg_enable.log` shows default `INPUTDBG: OFF`, then `INPUTDBG: ON`; `inputdbg_ec11_command_smoke_tail.jsonl` contains two `keyboard evt=7` debug records for `kind=6/ec11_dispatch` with usage `233` and `234` after `~EC11:ROTATE:CW/CCW`. The post-operator full dump artifact `inputdbg_after_user_dump_20260606-221610_raw.jsonl` captured 672 JSON events before a Task WDT reset in `ble_hid_keyboard`; after reboot, `inputdbg_after_user_off_20260606-221610.log` confirms `~DIAGLOG:INPUTDBG:OFF` and status `OFF`. A safer post-reset EC11 tail capture, `inputdbg_after_user_last300_20260606-221725_raw.jsonl`, captured 300 latest events including 145 `keyboard evt=7` input debug events, 28 `keyboard evt=4` EC11 detents, and 31 `keyboard evt=1` HID sends. It recorded 12 CW/usage `0x00E9` and 16 CCW/usage `0x00EA` dispatches, with zero `voice_key evt=4` EC11 push debug events in that tail. The focused key-only tail `inputdbg_keytest_last240_20260606-222514_raw.jsonl` captured KEY1-KEY4 flash-backed input debug: 15 `keyboard evt=7` raw/stable records covering keys 1-4, 33 `keyboard evt=6` custom-key records, and 7 `keyboard evt=1` HID sends for usages 104/105/106/107 (`F13`-`F16`). `inputdbg_keytest_last240_20260606-222514_status.log` confirms `INPUTDBG: OFF`. |
| Submit refresh static/build | PASS | `repo_features.ps1 -Check`, `verify_v2_board_profile_static.ps1`, `verify_power_manager_static.ps1`, `verify_status_led_static.py`, `verify_ble_ota_gatt_contract.py`, `verify_diagnostic_log_coverage.ps1`, `git diff --check`, and `tools/build.ps1 -Target esp32s3`; app `0xc0620`, 87% free |
| Submit refresh flash | PASS after flash tool default build-dir fix | `with-lock -Resource COMx` resolved `COMx -> COM6`; `tools\flash.ps1 -Port COM6` used short build dir `C:\Users\Billy\AppData\Local\Temp\listener-idf-build-36ea52e79d27`; ESP32-S3 MAC `a4:cb:8f:f4:59:a4`; 16MB flash; 8MB embedded PSRAM; image hashes verified |
| Submit refresh serial capture | PASS | `serial_capture_after_current_flash.log`; current firmware logs BLE disconnect/advertising and health heartbeat without taking a long monitor lock |
| Submit refresh diag_log dump/decode | PASS | `diag_log_20260607-065955.jsonl` has 669 retained events; `ai_diagnostics/manifest.json` records 669 decoded events, 20 recent warning/error refs, and 4 boot segments |
| Submit refresh BLE HID contract | PASS | `pwsh -NoProfile -File .\tools\verify_ble_hid.ps1` static checks passed for BLE HID battery service reporting, reconnect refresh, ADC diagnostics, and diag event coverage |
| Final merge static/tool checks | PASS | commit `d03a9257b841cadcefed4773f6e901320dc46c7d`; `repo_features.ps1 -Check`, `verify_v2_board_profile_static.ps1`, `verify_power_manager_static.ps1`, `verify_status_led_static.py`, `verify_ble_ota_gatt_contract.py`, `verify_diagnostic_log_coverage.ps1`, `verify_ble_hid.ps1`, `python -m compileall -q tools`, and `git diff --check` all passed |
| Final merge build | PASS | `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`; short build dir `C:\Users\Billy\AppData\Local\Temp\listener-idf-build-36ea52e79d27`; app `0xc0470`, 87% free |
| Final merge flash | PASS | no-lock build first, then `with-lock -Resource COMx` resolved `COMx -> COM6`; `tools\flash.ps1 -Port COM6 -NoBuild` flashed final image; ESP32-S3 MAC `a4:cb:8f:f4:59:a4`; 16MB flash; 8MB embedded PSRAM; image hashes verified |
| Final merge serial status/telemetry | PASS | `current_telemetry/v2_current_telemetry_20260607-072357.md` records both current branches present, battery around 4.09-4.10 V, `hardware_shutdown_ms=1800000`, and `PWR_HOLD GPIO: 46` |
| Final merge bounded diag_log dump/decode | PASS | `diag_log_20260607-072414.jsonl` captured 240 retained events; `ai_diagnostics/manifest.json` records 240 decoded events, 13 warning/error refs, 4 boot segments, board profile highlights, LED resource highlights, and `pwr_hold_gpio=46` |
| Rework non-destructive GPIO diagnostic static/tool checks | PASS | commit `0d4d95f2bb71712a13178df99d3fc2c37b136171`; `repo_features.ps1 -Check`, `verify_v2_board_profile_static.ps1`, `verify_power_manager_static.ps1`, `verify_status_led_static.py`, `verify_ble_ota_gatt_contract.py`, `verify_diagnostic_log_coverage.ps1`, `verify_ble_hid.ps1`, `python -m compileall -q tools`, and `git diff --check` all passed |
| Rework build | PASS | `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`; short build dir `C:\Users\Billy\AppData\Local\Temp\listener-idf-build-36ea52e79d27`; app `0xc0490`, 87% free |
| Rework flash | PASS | no-lock build first, then `with-lock -Resource COMx,BLE-ADDR` resolved `COMx -> COM6`; `tools\flash.ps1 -Port COM6 -NoBuild` flashed final image; ESP32-S3 MAC `a4:cb:8f:f4:59:a4`; 16MB flash; 8MB embedded PSRAM; image hashes verified |
| Rework serial status/telemetry and GPIO diagnostic | PASS | `current_telemetry/v2_current_telemetry_20260607-074822.md` records both current branches present, battery around 4.07-4.11 V, `hardware_shutdown_ms=1800000`, and `PWR_HOLD GPIO: 46`; `board_gpio_status.txt` records `~BOARD:GPIO active_low=1 mode=read_as_configured reconfigure=0`, EC11 A/B idle high, and `ec11_ab_state=0x03` |
| Rework bounded diag_log dump/decode | PASS | `diag_log_20260607-074837.jsonl` captured 240 retained events; `ai_diagnostics/manifest.json` records 240 decoded events, 10 warning/error refs, 4 boot segments, and board/power/BLE parameter highlights |

Static/build checks run after the fix:

- PASS: `python tools/verify_status_led_static.py`
- PASS: `pwsh -NoProfile -File tools/verify_v2_board_profile_static.ps1`
- PASS: `pwsh -NoProfile -File tools/verify_power_manager_static.ps1`
- PASS: PowerShell parser check for `tools/flash.ps1`
- PASS: `LISTENER_IDF_BUILD_DIR=%TEMP%\listener-idf-build-v2-1-4-oai2; pwsh -NoProfile -File tools/build.ps1 -Target esp32s3`
- PASS after `~BOARD:GPIO` addition: `pwsh -NoProfile -File tools/verify_v2_board_profile_static.ps1`
- PASS after `~BOARD:GPIO` addition: `python tools\verify_status_led_static.py`
- PASS after `~BOARD:GPIO` addition: `pwsh -NoProfile -File tools\verify_power_manager_static.ps1`
- PASS after `~BOARD:GPIO` addition: `LISTENER_IDF_BUILD_DIR=%TEMP%\listener-idf-build-v2-1-4-oai2; pwsh -NoProfile -File tools/build.ps1 -Target esp32s3`
- PASS after EC11 GPIO11/PWR_HOLD GPIO46 correction: `pwsh -NoProfile -File tools\verify_v2_board_profile_static.ps1`
- PASS after EC11 GPIO11/PWR_HOLD GPIO46 correction: `python tools\verify_power_manager_static.py`
- PASS after EC11 GPIO11/PWR_HOLD GPIO46 correction: `pwsh -NoProfile -File tools\ai\repo_features.ps1 -Check`
- PASS after EC11 GPIO11/PWR_HOLD GPIO46 correction: `python tools\verify_status_led_static.py`
- PASS after EC11 GPIO11/PWR_HOLD GPIO46 correction: `pwsh -NoProfile -File tools\verify_charging_awake_policy_static.ps1`
- PASS after EC11 GPIO11/PWR_HOLD GPIO46 correction: `git diff --check`
- PASS after EC11 GPIO11/PWR_HOLD GPIO46 correction: `LISTENER_IDF_BUILD_DIR=%TEMP%\listener-idf-build-v2-1-4-oai2; pwsh -NoProfile -File tools/build.ps1 -Target esp32s3`
- PASS after KEY1-KEY4 debounce fix: `pwsh -NoProfile -File .\tools\build.ps1`
- PASS after V2 no-hardware-debounce input rework: `pwsh -NoProfile -File .\tools\verify_v2_board_profile_static.ps1`
- PASS after V2 no-hardware-debounce input rework: `python .\tools\verify_status_led_static.py`
- PASS after V2 no-hardware-debounce input rework: `pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1`
- PASS after V2 no-hardware-debounce input rework: `pwsh -NoProfile -File .\tools\verify_charging_awake_policy_static.ps1`
- PASS after V2 no-hardware-debounce input rework: `pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1`
- PASS after V2 no-hardware-debounce input rework: `python .\tools\verify_ble_ota_gatt_contract.py`
- PASS after V2 no-hardware-debounce input rework: `git diff --check` (CRLF warnings only)
- PASS after V2 no-hardware-debounce input rework: `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
- PASS after EC11 direction sign inversion: `git diff --check` (CRLF warnings only)
- PASS after EC11 direction sign inversion: `pwsh -NoProfile -File .\tools\verify_v2_board_profile_static.ps1`
- PASS after EC11 direction sign inversion: `python .\tools\verify_status_led_static.py`
- PASS after EC11 direction sign inversion: `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
- PASS after input debug flash-log switch: `git diff --check` (CRLF warnings only)
- PASS after input debug flash-log switch: `pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1`
- PASS after input debug flash-log switch: `pwsh -NoProfile -File .\tools\verify_v2_board_profile_static.ps1`
- PASS after input debug flash-log switch: `python .\tools\verify_status_led_static.py`
- PASS after input debug flash-log switch: `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
- PASS submit refresh: `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
- PASS submit refresh: `pwsh -NoProfile -File .\tools\verify_v2_board_profile_static.ps1`
- PASS submit refresh: `pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1`
- PASS submit refresh: `python .\tools\verify_status_led_static.py`
- PASS submit refresh: `python .\tools\verify_ble_ota_gatt_contract.py`
- PASS submit refresh: `pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1`
- PASS submit refresh: `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
- PASS submit refresh: `pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx` under `aiw with-lock`, after `flash.ps1` adopted the same short-build default used by `build.ps1`
- PASS submit refresh: bounded serial capture artifact `tests/artifacts/voice-keyboard-v2-firmware-board-migration-1.4-oai2/20260607-current/serial_capture_after_current_flash.log`
- PASS submit refresh: `pwsh -NoProfile -File .\tools\dump_diag_log.ps1 -Port COMx` under `aiw with-lock`, plus offline decode through `collect_ai_diagnostics.ps1`
- PASS submit refresh: `pwsh -NoProfile -File .\tools\verify_ble_hid.ps1`
- PASS final merge refresh: `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
- PASS final merge refresh: `pwsh -NoProfile -File .\tools\verify_v2_board_profile_static.ps1`
- PASS final merge refresh: `pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1`
- PASS final merge refresh: `python .\tools\verify_status_led_static.py`
- PASS final merge refresh: `python .\tools\verify_ble_ota_gatt_contract.py`
- PASS final merge refresh: `pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1`
- PASS final merge refresh: `pwsh -NoProfile -File .\tools\verify_ble_hid.ps1`
- PASS final merge refresh: `python -m compileall -q tools`
- PASS final merge refresh: `git diff --check`
- PASS final merge refresh: `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
- PASS final merge refresh: `pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx -NoBuild` under `aiw with-lock` after the no-lock final build
- PASS final merge refresh: `pwsh -NoProfile -File .\tools\collect_v2_current_telemetry.ps1 -Port COMx` under `aiw with-lock`
- PASS final merge refresh: `pwsh -NoProfile -File .\tools\dump_diag_log.ps1 -Port COMx -Count 240` under `aiw with-lock`, plus offline decode through `collect_ai_diagnostics.ps1`
- PASS rework refresh: `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
- PASS rework refresh: `pwsh -NoProfile -File .\tools\verify_v2_board_profile_static.ps1`
- PASS rework refresh: `pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1`
- PASS rework refresh: `python .\tools\verify_status_led_static.py`
- PASS rework refresh: `python .\tools\verify_ble_ota_gatt_contract.py`
- PASS rework refresh: `pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1`
- PASS rework refresh: `pwsh -NoProfile -File .\tools\verify_ble_hid.ps1`
- PASS rework refresh: `python -m compileall -q tools`
- PASS rework refresh: `git diff --check`
- PASS rework refresh: `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
- PASS rework refresh: `pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx -NoBuild` under `aiw with-lock` after a no-lock build
- PASS rework refresh: `pwsh -NoProfile -File .\tools\collect_v2_current_telemetry.ps1 -Port COMx` and a bounded `~BOARD:GPIO` capture under `aiw with-lock`
- PASS rework refresh: `pwsh -NoProfile -File .\tools\dump_diag_log.ps1 -Port COMx -Count 240` under `aiw with-lock`, plus offline decode through `collect_ai_diagnostics.ps1`

## Observed PASS Evidence

Flash evidence:

- ESP32-S3 QFN56 rev v0.2, USB-Serial/JTAG.
- Embedded PSRAM 8MB.
- Flash command used `--flash_size 16MB`.
- App binary size after fix: `0xbecc0`; smallest app partition `0x600000`; 88% free.
- Bootloader, app, partition table, and OTA data hash verification passed.

Boot and board identity:

- Bootloader reports SPI flash size 16MB.
- Runtime reports 8MB octal PSRAM and POST SPIRAM `8388608 bytes`.
- `~BOARD:STATUS` reports `profile=voice-keyboard-v2-n16r8`, module `ESP32-S3-WROOM-1-N16R8`, `flash_mb=16`, `psram_mb=8`, `psram_mode=octal`.
- Pin map in latest boot/status logs matches current V2 hardware docs: keys GPIO38/39/40/41, EC11 A/B/key GPIO42/GPIO2/GPIO11, mic GPIO48/GPIO47, USB detect GPIO7, charger GPIO14/GPIO21, BAT_V_ADC GPIO8, current ADC GPIO10/GPIO9, LED GPIO1/GPIO5/GPIO13/GPIO4, PWR_HOLD GPIO46, reserved MSPI GPIO35/GPIO36/GPIO37.
- Hardware repository cross-check used `voice-keyboard-hardware` local HEAD `5a16e2e` (`Add SPH0655 microphone datasheet`) and origin/master `234304b` (`增加旋钮RGB`). `docs/v2-firmware-hardware-interface.md`, the PDF text stream, SchDoc binary text tokens, and PcbDoc binary text tokens all include the current V2 key/EC11/PWR_HOLD pin set.

LED after the RMT fix:

- Boot log confirms all four strips initialize:
  - status: GPIO1, 6 LEDs
  - EC11 ring: GPIO5, 12 LEDs
  - key: GPIO13, 4 LEDs
  - edge: GPIO4, 6 LEDs
- `~LED:STATUS` reports the four strip contracts and GRB order.
- `~LED:TEST:RGBW all` is accepted with mask `0x0f`.
- `~LED:TEST:MAP ec11` is accepted with mask `0x02` and EC11 order `LED7..LED10+LED15..LED16+LED23..LED28`.
- `~LED:OFF` after fix did not emit the previous RMT timeout in the captured run.
- Follow-up user visual report said the LEDs did not light. The follow-up serial capture still shows `~LED:TEST:RGBW all` accepted and no `DIAG_LED_OUTPUT_FAIL`; however `~BOARD:POWER branch=SY7088_input_branch` reported `estimated_input_current_ma=0` and `estimated_input_power_mw=0`. This points at VDD_LED/SY7088 power path or physical LED rail enable/sign-off, not the earlier RMT channel allocation defect.

Power and telemetry:

- `~POWER:STATUS` reports USB external power present, charging active-low level, battery ADC, shutdown blockers, PWR_HOLD GPIO46 state, and hardware-shutdown cold-boot action.
- V2 current telemetry after fix reports both expected sensors present:
  - `TPS63020_input_branch`, GPIO10, ADC calibrated, input current/power valid.
  - `SY7088_input_branch`, GPIO9, ADC calibrated, input current/power valid.

Physical key/EC11 follow-up:

- `~BOARD:GPIO` reads raw active-low levels for KEY1 GPIO38, KEY2 GPIO39, KEY3 GPIO40, KEY4 GPIO41, EC11 A GPIO42, EC11 B GPIO2, and EC11 key GPIO11.
- Rework refresh confirms `~BOARD:GPIO` is non-destructive for interrupt-owned EC11 A/B pins: `board_gpio_status.txt` shows `mode=read_as_configured reconfigure=0`, `ec11_a_level=high`, `ec11_b_level=high`, and `ec11_ab_state=0x03`. The static guard now fails if `board_print_gpio_status()` reintroduces `board_configure_status_input()` for EC11 A/B.
- Before the latest hardware recheck, a 60 second locked capture polled `~BOARD:GPIO` 56 times with the obsolete EC11 key GPIO18 mapping. Every sample reported `key_pressed_mask=0x00`, `ec11_key_pressed=0`, and `ec11_ab_state=0x03`.
- After the latest hardware recheck and EC11 GPIO11 correction, a 90 second locked capture reported 116 data samples with `key_pressed_mask=0x00`, `ec11_key_pressed=0`, and `ec11_ab_state=0x03`.
- A second 180 second locked capture after the same correction reported 320 data samples with `key_pressed_mask=0x00`, `ec11_key_pressed=0`, and `ec11_ab_state=0x03`.
- The same capture had no `custom key raw transition`, `custom key stable transition`, `custom key fallback queued`, `EC11 transition`, `EC11 detent`, `EC11 rotation queued`, or recording gesture log lines.
- A post-fix flash-backed `diag_log` dump returned 727 retained events. It had 7 `keyboard evt=1` entries from prior BLE HID character dispatch, but no `keyboard evt=6` custom key events, no `keyboard evt=4` EC11 detent events, and no `voice_key evt=1/2/3` recording key events.
- This does not validate the physical controls. It indicates either the controls were not actuated during the capture windows, the physical controls are not connected/soldered as expected, or the board under test differs from the current V2 schematic tokens despite the firmware now matching the latest hardware repo pin map.
- Follow-up synchronized KEY4 debugging later confirmed the physical `KEY4/GPIO41` input does reach firmware. Before the debounce fix, the locked 30 second capture saw `key4_level=low key4_pressed=1` and `custom key raw transition: logical=KEY4 source=key4.gpio41.f16 raw_high=0`, but the observed low pulses were shorter than the previous 8-sample debounce threshold and did not become stable key events.
- After reducing KEY1-KEY4 debounce from 8 samples to 3 samples, the locked 60 second post-fix capture saw repeated `custom key stable transition: logical=KEY4 source=key4.gpio41.f16 raw_high=0 pressed=1`, matching release lines, and HID fallback gesture logs including `custom key fallback queued: logical=KEY4 source=key4.gpio41.f16 usage=F16 gesture=single`.
- Latest V2 schematic review confirms KEY1-KEY4 and EC11 A/B have no dedicated signal RC debounce. KEY1-KEY4 now use 10 ms polling and 30 ms software debounce. EC11 A/B now use GPIO any-edge interrupts, a queue, and quadrature state filtering so contact bounce has to produce a full valid detent sequence before a rotation event is queued. EC11 push on `EC11-KEY_IO/GPIO11` remains the runtime recording input with software debounce; long-idle power-off is handled separately through `PWR_HOLD/GPIO46`.
- A later DTR-asserted `~BOARD:GPIO-SCAN` captured all four key inputs on the current firmware: `GPIO_SCAN_CHANGED gpio38:KEY1 gpio39:KEY2 gpio40:KEY3 gpio41:KEY4`. The same artifact includes `custom key stable transition` press/release lines for KEY1 through KEY4 and HID usage sends for F13 through F16.
- DTR must be asserted for reliable USB serial command/capture on this board in the current setup. Earlier DTR-false captures returned no output even though the app was alive and responded immediately with DTR true.
- EC11 A/B rotation remained unvalidated in the live serial captures immediately after the input rework. A later input-debug flash tail capture did record physical EC11 rotation through the flash-backed path: 28 `DIAG_KBD_EC11_DETENT` events, 145 `DIAG_KBD_INPUT_DEBUG` events, and volume HID usages `0x00E9`/`0x00EA`. EC11 push/GPIO11 remains unvalidated in artifacts because the same tail had zero `voice_key evt=4` input debug events.
- After the user reported EC11 clockwise/counter-clockwise behavior was reversed, the firmware inverted the physical quadrature sign once in `keyboard_ec11_quadrature_delta()` and logs `direction_policy=clockwise_increases_volume_brightness` at EC11 startup. Logical command smoke validated `CW` dispatches system volume increment `0x00E9` and screen brightness increment `0x006F`, while `CCW` dispatches system volume decrement `0x00EA` and screen brightness decrement `0x0070`.
- The locked 90 second physical direction follow-up did not capture an EC11 detent after the direction fix. It reported EC11 A/B idle high and GPIO scan changes only on gpio17/gpio43, so the code fix is built/flashed and logically verified, but actual clockwise hand-feel still needs an operator turn captured in the same serial window or direct human confirmation on the flashed board.
- The later flash-backed diag_log dump also did not contain persisted EC11 detents: 670 events decoded, `keyboard.kbd_ec11_detent` / raw `keyboard evt=4` count 0. Current flash diagnostics persist physical detent direction/count when that path logs `DIAG_KBD_EC11_DETENT`, but they do not persist raw A/B transitions or the final HID consumer usage/action string; those remain serial-only evidence unless the diagnostic schema is extended.
- Input debug flash logging is now available for the rest of bring-up. Use `~DIAGLOG:INPUTDBG:ON` before pressing keys/rotating EC11, then pull `~DIAGLOG:LAST:N` first; use full `~DIAGLOG:DUMP` cautiously until the serial dump path yields or feeds the Task WDT during large retained-ring exports. Debug records decode as `keyboard.kbd_input_debug` and `voice_key.vkey_input_debug`; turn it off with `~DIAGLOG:INPUTDBG:OFF` once the physical input matrix is signed off. The 2026-06-06 post-operator tails captured both EC11 rotation input debug and focused KEY1-KEY4 raw/stable flash debug, then confirmed `INPUTDBG: OFF`.
- Power-off behavior is now the master branch hardware-shutdown path: long-idle or `~POWER:SHUTDOWN` releases `PWR_HOLD/GPIO46` after final blocker and external-power checks. EC11/GPIO11 remains runtime recording input evidence, not a deep-sleep wake path.

BLE/HID/OTA/watchdog status:

- BLE HID after-fix dispatch consumed `v2hid14fix` over serial and logged `hid_keyboard: send_ascii done` for every character with `connected=yes`.
- `~OTA:STATUS` reports running `ota_0`, update `ota_1`, update slot size 6291456, `pending_verify=0`, `blocker=none`, and readiness/capability strings including OTA ready.
- `~WDT:STATUS` reports task watchdog enabled, initialized, panic enabled, timeout 5s.
- `~BOOT:STATUS` reports reset reason USB, crash count below threshold, safe mode false.

## Residual Follow-Up Gates for 1.5

These items need a human hardware operator, external fixture, destructive reset window, or product decision after 1.4 is submitted. They are intentionally scoped to 1.5 and do not block this AI evidence ladder:

- Visual LED validation: RGBW physical color order, brightness, EC11 ring order/direction, edge order, and all four visible zones. Follow-up operator report says LEDs did not light; serial evidence suggests checking VDD_LED/SY7088 power path because LED commands are accepted but the LED input branch current is 0 mA.
- Physical controls: KEY1/GPIO38 through KEY4/GPIO41 physical recognition is validated after the debounce/input rework. EC11 A/B rotation is now captured in flash-backed input debug for system-volume dispatch, but EC11 push/GPIO11 still needs physical capture and host-visible HID delivery under the full plan matrix still needs final acceptance evidence.
- Meter/fixture measurements: 3.3V rail, VDD_LED rail, charger behavior, and battery-to-3v3 / battery-to-LED branch measurements beyond ADC telemetry.
- Hardware shutdown/recovery: actual PWR_HOLD/GPIO46 power removal, cold boot after short press, and recovery evidence remain for 1.5.
- OTA live GATT after-fix: Windows PnP sees the listener and OTA service, and firmware reports OTA ready, but WinRT uncached discovery/read failed with `0x80070016` after the final flash. Cached service metadata still shows the OTA service and characteristics. This needs a clean BLE reconnect/re-pair or Windows Bluetooth reset before claiming live OTA GATT PASS.
- Watchdog destructive smoke: `~WDT:DEADLOCK` was not run because it intentionally triggers a reset and should be planned with an operator.

## Notes

Known warnings in boot/status evidence are expected for this V2 validation state unless a later plan changes the policy:

- Audio capture is degraded because the V2 microphone interface remains unvalidated.
- Board policy strings mark PWR_HOLD, LED VDD sign-off, current telemetry, and mic validation as provisional where applicable.

This report intentionally records AI-verifiable PASS evidence plus residual final gates. `aiw submit` is appropriate for 1.4; 1.5 owns the remaining human-visible, external fixture, destructive reset, and product closure items.
