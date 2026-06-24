# Low-Power And Status LED Final Acceptance - 2026-06-24

## Scope

This record covers the Listener V2/N16R8 low-power, wake, shutdown, and status LED fixes accepted on COM10 by Codex.

## Firmware State

- Idle timeout under test: 1 minute.
- Battery auto shutdown under test: 2 minutes.
- Plugged auto shutdown: off.
- Plugged low-power idle: enabled.
- Active status strip DMA is preserved for REC/AI and normal effect switching.
- Low-power idle PWR-only latch and final hardware-shutdown PWR-only cue use repeated non-DMA status writes.
- Active `TYPE_READY` BLE LED is steady blue with a 30 second LED-only hold for short Listener-Type heartbeat misses; strict audio readiness still uses the 12 second heartbeat timeout.

## Evidence

- Build: `pwsh -NoProfile -File .\tools\build.ps1` passed, firmware size `0xd5630`, 86% app partition free.
- Flash: COM10 flashed successfully after the TYPE_READY LED-hold change.
- Static checks passed:
  - `python tools\verify_status_led_static.py`
  - `python tools\verify_power_manager_static.py`
  - `python tools\verify_current_docs_static.py`
  - `pwsh -NoProfile -File .\tools\verify_ble_status_led_connected_sync.ps1`
  - `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
  - `python tools\verify_voice_recording_control_fsm.py`
- Full guided low-power acceptance: `.cache\validation\low-power-acceptance-guided-20260624-114336\guided-summary.md`
  - Result: PASS.
  - Plugged idle observation: PASS, user reported `20-22ma`, only one white PWR LED.
  - Plugged idle capture: `state=CONNECTED_IDLE`, `low_power_disabled=1`, `status_rgb=PWR:7,7,7;BLE:0,0,0;REC:0,0,0;AI:0,0,0;OK:0,0,0;WARN:0,0,0`.
  - Battery idle observation: PASS.
  - Battery auto shutdown observation: PASS.
  - Post-replug diagnostics saw shutdown evidence.
- Latest status LED no-all-on check after the TYPE_READY LED-hold firmware:
  - `.cache\validation\status-led-log-no-all-on-20260624-115829\summary.json`
  - Result: PASS.
  - `current_status_tail_any_on=false`, `current_status_tail_all_on=false`, no warnings or failures.
- TYPE_READY LED-hold spot check:
  - `.cache\validation\type-ready-led-hold-status-20260624-quick.txt`
  - The strict Type heartbeat timeout log appeared while `~LED:STATUS` still reported `ble=type_ready` and `BLE:0,0,25`, showing the LED no longer immediately drops to HID-only double flash on a short heartbeat miss.

## Notes

- `git diff --check` passed; only Git's LF-to-CRLF working-copy warnings were reported.
- The generic `send_serial_and_capture.ps1` tool does not understand `WAIT` pseudo-commands. A quick exploratory capture accidentally sent `WAIT 15000`/`WAIT 17000` as HID text; future delayed captures should sleep in the host script instead of sending `WAIT` through the device serial command path.
