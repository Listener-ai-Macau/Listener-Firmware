# Manual LED observation notes

Plan: `voice-keyboard-camera-status-key-led-tuning`
Step: `1.2`

## 2026-06-08 pre-reset-fix observation

Context: user watched the board after the first manual LED run. The session log showed ESP32 boot text after opening `COM6`, so this observation is recorded as a setup defect and partial visual clue rather than final LED mapping evidence.

| command context | user observation | interpretation |
|---|---|---|
| first light / status LED1 sequence | green and white appear swapped | likely real color-order or color-token defect; retest after serial-open reset is fixed |
| while testing first light | second light blue flashing | likely normal boot/BLE status animation or stale status effect because serial open reset the ESP32 and the manual script did not preclear LEDs before the pixel command |

Follow-up: `tools/status_led_manual_calibration.ps1` was updated to open serial without DTR/RTS reset, drain boot text when present, and send `~LED:OFF` before every one-pixel command.

## 2026-06-08 live-state observation

Context: user reported the board state after the pre-reset-fix manual session and before any confirmed new flash by `oai1`.

| observed state | interpretation |
|---|---|
| first status light stays green | likely normal PWR/battery baseline or stale status output after reset; not final color-order evidence |
| second status light stays blue | likely BLE connected/reconnect baseline; not final LED2 command evidence |
| KEY LEDs no longer light when key buttons are pressed, although they previously did | likely firmware state issue after `~LED:OFF`/sleep-style output disable, because key events update `key_pressed_mask` but do not clear `output_disabled` |

Follow-up: keep physical LED backend separate from status/key business rendering, and make USB/manual `~LED:OFF` a recoverable manual output clear rather than a sleep-only path that suppresses later key feedback.

## 2026-06-08 rework implementation status

Context: `oai1` implemented the recoverable manual-off fix and split the WS2812/RMT strip output into a separate backend before attempting hardware validation.

| check | result |
|---|---|
| `python tools\verify_status_led_static.py` | PASS |
| `pwsh -NoProfile -File .\tools\status_led_manual_calibration.ps1 -ManifestOnly` | PASS; generated 50 status/key RGBW/off commands |
| `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3` | PASS; build output includes `status_led_strip_backend.c` |
| `git diff --check` | PASS; only CRLF conversion warnings |

Superseded hardware follow-up: an early `aiw with-lock -Resource COMx` run resolved `COMx` to `COM6`, but `tools\flash.ps1 -Port COM6 -Target esp32s3 -NoBuild` failed during `esptool.py write_flash` with `Failed to connect to ESP32-S3: No serial data received`. That was later resolved by reflashing from the active `oai1` hardware session; the final observations below are the acceptance evidence for the current firmware.

Next required evidence at this point was to retry the flash and single-pixel validation. That follow-up is complete for status/key LEDs; EC11/edge 4020 darkness is tracked separately in `hardware-4020-pinout-audit.md`.

## 2026-06-08 full-brightness live observation

Context: `oai1` changed active status/key LED commands to full brightness, rebuilt, flashed the firmware on `COM6`, and drove one status/key LED command at a time through the manual calibration tool.

| command | user observation | result | interpretation |
|---|---|---|---|
| `~LED:TEST:PIXEL status LED1 red 100` | first physical light is red; brightness feels acceptable | PASS | LED1/PWR physical mapping is correct for red, and full-brightness output is acceptable for continued calibration |
| `~LED:TEST:PIXEL status LED1 green 100` | first physical light is green; brightness normal | PASS | LED1/PWR green channel matches the commanded color |
| `~LED:TEST:PIXEL status LED1 blue 100` | first physical light is blue; brightness looks bright enough | PASS | LED1/PWR blue channel matches the commanded color |
| `~LED:TEST:PIXEL status LED1 white 100` | first physical light is white | PASS | LED1/PWR RGB channels combine correctly for white |
| `~LED:TEST:PIXEL status LED1 off 0` | first physical light is off | PASS | LED1/PWR off command clears the pixel |

## 2026-06-08 operator fast-forward confirmation

Context: after LED1 RGBW/off was proven one command at a time, the operator approved fast-forwarding the rest of the six status LEDs and four key LEDs instead of requiring a 50-row human confirmation table. The flashed firmware then drove status/key chase and RGBW cycle commands at full brightness.

| observed scope | user observation | result | interpretation |
|---|---|---|---|
| status strip `LED1..LED6` | "状态灯 ... 是好的" after the status/key chase and color cycle | PASS | The status strip is visually active as a strip and responds to the full-brightness color-cycle path |
| key strip `LED11..LED14` | "按键灯是好的" after the status/key chase and color cycle | PASS | The key strip is visually active as a strip and responds to the full-brightness color-cycle path |
| status/key combined chase | user observed the initial chase and then all colors cycling | PASS | The shared manual validation path can address status/key strips without cross-driving EC11/edge |
| EC11 ring and edge/frame 4020 LEDs | "旋钮和板框灯不亮"; later confirmed still not lit | OUT_OF_SCOPE_RESIDUAL | EC11/edge are outside the original 1.2 status/key scope; see `hardware-4020-pinout-audit.md` for the likely 4020 pinout/footprint issue |

Decision: status/key mapping, color order, full-brightness policy, recoverable manual off, and manual/chase validation tooling are sufficient for the 1.2 status/key scope. EC11 ring and edge/frame darkness is recorded as a hardware investigation residual rather than a status/key firmware blocker.
