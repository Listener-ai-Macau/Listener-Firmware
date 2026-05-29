# firmware-low-power-overnight-drain / 1.1 oai1 rework

Date: 2026-05-29
Agent: oai1
Branch: ai/oai1-firmware-low-power-overnight-drain-1.1
Validation source: `23957a2` plus the `KEYBOARD_WASD_DEBOUNCE_SAMPLES=8` hardening change in this submission
Status: PASS, ready for review

## Scope

This rework closes the step 1.1 hardware gate. The final branch must prove overnight idle sleep is driven by real user inactivity, does not timer self-wake, wakes by KEY4/GPIO21 EXT1, and preserves HID, voice, BLE audio, and disconnect/reconnect behavior after sleep/wake.

## Code state

- Reapplied the prior power manager fix:
  - split `power_manager` idle clocks into `user_idle_ms` and `radio_idle_ms`;
  - overnight sleep uses `user_idle_ms`;
  - BLE connected/disconnected idle uses `radio_idle_ms`;
  - BLE connect/disconnect no longer calls `power_manager_record_activity()`;
  - `~POWER:STATUS` prints both idle clocks.
- Reapplied the wake diagnostic fix:
  - maps ESP-IDF wake causes into `power_manager_wake_source_t` before recording wake diagnostics;
  - preserves EXT1/GPIO21 evidence instead of relying on raw ESP wake enum values.
- Added debounce hardening:
  - `KEYBOARD_WASD_DEBOUNCE_SAMPLES` increased from 3 to 8;
  - this changes the WASD/KEY4 debounce window from 60 ms to 160 ms at the existing 20 ms poll interval;
  - reason: the first no-touch soak recorded a spurious KEY4/keyboard event during the 30 minute idle window.

## Automated validation

- PASS: `python tools\verify_power_manager_static.py`
- PASS: `pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1`
- PASS: `pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1`
- PASS: `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
- PASS: `python -m compileall -q tools`
- PASS: `git diff --check`
- PASS: `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
  - app binary size: `0xa5ac0`
  - smallest app partition: `0x1b0000`
  - free app partition space: `0x10a540`
- PASS: `pwsh -NoProfile -File .\tools\flash.ps1 -Port COM5 -Target esp32s3`
  - target MAC: `14:c1:9f:48:fe:70`
  - app binary size: `0xa5ac0`

## Hardware artifacts

Primary artifact directory:

- `tests/artifacts/firmware_low_power_oai1_debounce_soak_20260529-194607`

Important files:

- `boot_after_debounce_flash.log`
- `power_status_baseline.log`
- `diaglog_clear_before_soak.log`
- `soak_status.json`
- `soak_events.jsonl`
- `power_status_after_key4_wake.log`
- `diag_log_last200_after_key4_wake.raw.log`
- `diag_log_last200_after_key4_wake.jsonl`
- `custom_ble_hid_dispatch_after_wake_pass.log`
- `verify_audio_capture_session_after_wake_no_reset.log`
- `verify_audio_cancel_session_after_wake.log`
- `matrix_t_reconnect_pass_result.json`
- `matrix_t_reconnect_pass_summary.log`

## Overnight sleep and wake evidence

- Firmware was flashed to COM5, then boot confirmed `WASD keys ready ... debounce_samples=8`.
- Baseline `~POWER:STATUS` showed `overnight_sleep_ms=1800000`, `wake_policy=key4_only`, and wake mask `0x0000000000200000`.
- The first no-touch run before debounce hardening was invalidated by a spurious KEY4/keyboard event at about 1206 s. That is why this submission includes the 8-sample debounce change.
- The second no-touch run started at `2026-05-29T19:47:33+08:00`.
- The monitor saw the serial port close at elapsed `1772` seconds, consistent with deep sleep entry. Its final `port_returned_without_key` state is a Windows SerialPort race: manual checks immediately after showed no present COM ports, and COM5 stayed absent through the 5 minute confirmation check ending at `2026-05-29T20:24:33+08:00`.
- The user pressed KEY4/GPIO21 after the absence confirmation. COM5 returned at `2026-05-29T20:26:15+08:00`.
- Post-wake `~POWER:STATUS` recorded:
  - `last_sleep_reason=overnight_idle`
  - `last_wake_source=ext1`
  - `wake_gpio_mask=0x0000000000200000`
  - `wake_key_gpio=21`

Persistent diagnostic tail from `~DIAGLOG:LAST:200` confirms the same path:

- sleep entry: `src=power evt=2 a1=1801805 a2=4070 a3=89 a4=1`
  - idle before sleep was `1801805 ms`;
  - battery was `4070 mV`, `89%`;
  - sleep reason `1` is `overnight_idle`.
- wake policy: `src=power evt=8 a1=1 a2=2097152 a3=0 a4=35`
  - low mask `2097152` is GPIO21.
- wake record: `src=power evt=3 a1=2 a2=2097152 a3=1 a4=1801805`
  - wake source `2` is EXT1;
  - GPIO21 mask preserved;
  - last sleep reason and idle duration persisted across boot.

## Post-wake regression evidence

- HID dispatch:
  - `custom_ble_hid_dispatch_after_wake_pass.log`
  - PASS: `hidq1` produced `hid_keyboard: send_ascii done` for all 5 characters.
  - The same log shows BLE reconnection before dispatch.
- Voice/BLE audio start-stop:
  - `verify_audio_capture_session_after_wake_no_reset.log`
  - PASS: no-reset 4.1 s BLE WAV capture.
  - `received_packet_count=274`, `expected_packet_count=274`, `missing_packet_count=0`, `packet_loss_ratio=0.0000`.
- Voice cancel:
  - `verify_audio_cancel_session_after_wake.log`
  - PASS: `cancel_requested=1`, `cancel_completed=1`, `cancel_received=1`.
  - Transport summary reason was `cancel`.
- BLE audio disconnect/reconnect:
  - `matrix_t_reconnect_pass_result.json`
  - PASS: `matrix_total=3`, `matrix_failed=0`, `matrix_warning=0`.
  - Cases passed: `T1` BT restart recovery, `T2` one reset/reconnect round, `T3` host-side recovery without BT restart.
- BLE audio stream readiness after host recovery:
  - HID and audio logs both show `ble_audio_stream` returning to `stream_ready`.

Diagnostic-only failures retained in the artifact directory:

- `verify_audio_ble_product_matrix_after_wake_transport.log` failed `A14/A15` after the no-reset matrix disabled notify mid-session. The same run still passed `T1/T2/T3`, and the later targeted cancel plus clean reconnect matrix passed.
- Earlier `verify_ble_hid_*` logs failed on boot-marker assumptions or host timing. They are not used as pass evidence; `custom_ble_hid_dispatch_after_wake_pass.log` is the HID pass artifact.

## Self-review

- Acceptance criteria checked against persistent diagnostics, `~POWER:STATUS`, and post-wake functional regressions.
- Diff scope is limited to the debounce hardening plus this validation artifact; the prior power-manager and wake-source fixes remain on the branch.
- Known host/tool caveat: opening COM5 can trigger `USB_UART_CHIP_RESET` on this board/host. For the sleep/wake proof, persistent diag log and post-wake `~POWER:STATUS` are the authoritative evidence, not transient boot capture.
