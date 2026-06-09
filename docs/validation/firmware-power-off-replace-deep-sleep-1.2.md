# firmware-power-off-replace-deep-sleep/1.2 Validation Artifact

> Superseding polarity note, 2026-06-09: hardware/operator feedback corrected the V2 `PWR_HOLD/GPIO11` contract to low-active hold and high-release shutdown. Historical sections below that discuss active-high hold were evidence from the bad interim firmware and must not be used as current implementation guidance.

Captured by: oai1
Captured at: 2026-06-07 06:12-06:16 Asia/Shanghai
Resumed/rechecked at: 2026-06-07 06:22 Asia/Shanghai
Rechecked again at: 2026-06-07 06:28 Asia/Shanghai
Worktree: `C:\Users\Billy\Desktop\listener\voice-keyboard-firmware-wt-oai1-firmware-power-off-replace-deep-sleep-1.2`
Branch: `ai/oai1-firmware-power-off-replace-deep-sleep-1.2`
Commit flashed: `6b827b69de02099d8dc158c1b915e74cbc7c9069`
Device: ESP32-S3 on `COM6`, MAC `a4:cb:8f:f4:59:a4`

## Commands Run

- PASS: `git diff --check`
- PASS: `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
  - Build transcript: `tests/artifacts/firmware_power_off_1_2/build_20260607-061735.log`
- PASS: `aiw with-lock -Resource COMx -Owner oai1 -TimeoutMinutes 12 -Run pwsh -NoProfile -File .\tests\artifacts\firmware_power_off_1_2\capture_power_off_1_2.ps1 -Port COMx`
  - `COMx` resolved to `COM6`
  - Flash transcript: `tests/artifacts/firmware_power_off_1_2/flash_20260607-061238.log`
  - Lock transcript: `tests/artifacts/firmware_power_off_1_2/with_lock_capture_20260607-061237.log`
  - Hardware manifest: `tests/artifacts/firmware_power_off_1_2/hardware_capture_manifest_20260607-061238.json`
- PASS: `aiw with-lock -Resource COMx -Owner oai1 -TimeoutMinutes 3 -Run pwsh -NoProfile -File .\tests\artifacts\firmware_power_off_1_2\query_status_1_2.ps1 -Port COMx`
  - `COMx` resolved to `COM6`
  - Lock transcript: `tests/artifacts/firmware_power_off_1_2/with_lock_status_query_20260607-061624.log`
  - Status query transcript: `tests/artifacts/firmware_power_off_1_2/status_query_20260607-061625.log`
- PASS: resumed no-lock checks after `refresh-waiting` reopened this step:
  - `git diff --check`
  - `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
- PASS: resumed short status query:
  - `aiw with-lock -Resource COMx -Owner oai1 -TimeoutMinutes 3 -Run pwsh -NoProfile -File .\tests\artifacts\firmware_power_off_1_2\query_status_1_2.ps1 -Port COMx`
  - `COMx` resolved to `COM6`
  - Lock transcript: `tests/artifacts/firmware_power_off_1_2/with_lock_status_query_resume_20260607-062236.log`
  - Status query transcript: `tests/artifacts/firmware_power_off_1_2/status_query_20260607-062238.log`
- PASS: repeated no-lock and short-lock checks after the step was reopened again:
  - `git diff --check`
  - `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
  - `aiw with-lock -Resource COMx -Owner oai1 -TimeoutMinutes 3 -Run pwsh -NoProfile -File .\tests\artifacts\firmware_power_off_1_2\query_status_1_2.ps1 -Port COMx`
  - `COMx` resolved to `COM6`
  - Lock transcript: `tests/artifacts/firmware_power_off_1_2/with_lock_status_query_rerun_20260607-062807.log`
  - Status query transcript: `tests/artifacts/firmware_power_off_1_2/status_query_20260607-062808.log`

## Evidence Summary

- Flash succeeded with verified writes for bootloader, app, partition table, and OTA data. The device hard-reset through RTS after flash.
- Firmware identity logs report `fw_version=v1002.0.0-ota-test-133-g6b827b6`, matching the flashed commit.
- `~BOOT:STATUS` reported `reset_reason=usb(11)`, not `deep_sleep`.
- `~POWER:STATUS` reported:
  - `shutdown_blockers=0x00000080`
  - `shutdown_blocker_names=external_power`
  - `external_power_present=1`
  - `usb_power_present=1`
  - `hardware_shutdown_ms=1800000`
  - `pwr_hold_gpio=11`
  - `pwr_hold_level=low`
  - `pwr_hold_configured=1`
- `~BOARD:STATUS` also reported `pwr_hold_gpio=11 pwr_hold_level=low pwr_hold_configured=1`.
- The resumed 06:22 and 06:28 status queries repeated the same finding: both `~POWER:STATUS` and `~BOARD:STATUS` reported `pwr_hold_gpio=11 pwr_hold_level=low pwr_hold_configured=1` while USB/external power was present.
- V2 telemetry capture passed and found both expected current telemetry branches:
  - `TPS63020_input_branch` on GPIO10
  - `SY7088_input_branch` on GPIO9
- Post-query logs show BLE/HID/audio diagnostics still alive after the status queries.

## Acceptance Matrix

1. Hardware validation inside shortest feasible `aiw with-lock`, with actual resources and transcript.
   - PASS. Two short `COMx` locks were used, both resolved to `COM6`, and both released cleanly.

2. Device runs accepted 1.1 firmware; flash or flashed commit recorded.
   - PASS. Flash transcript recorded commit `6b827b69de02099d8dc158c1b915e74cbc7c9069`, and runtime identity reports `g6b827b6`.

3. Short-press boot shows `~BOARD:STATUS` or equivalent PWR_HOLD/GPIO11 level HIGH and cold boot/reset reason, not Deep Sleep wake.
   - NOT SATISFIED in this automated run. The available USB serial setup captured `reset_reason=usb(11)`, which is not Deep Sleep, but it was not a physical short-press cold boot. Both `~POWER:STATUS` and `~BOARD:STATUS` reported `pwr_hold_level=low` in the initial, resumed, and repeated captures, while this acceptance item requires HIGH.

4. Automatic long-idle hardware shutdown artifact captures pre-shutdown diag/serial, BLE disconnect/I2S/LED shutdown evidence, and complete power-off evidence.
   - BLOCKED by hardware setup. The current automation path keeps USB serial attached, and the device reports `shutdown_blocker_names=external_power`. Proving complete automatic power-off requires battery-only/no-USB operation plus BLE/current/power telemetry while serial disappears.

5. After auto power-off, short-press cold boot recovery works and basic BLE/key/diagnostic paths work.
   - BLOCKED by the same hardware/physical gate. A true power-off recovery requires a physical short press after PWR_HOLD release and then BLE/key/diagnostic recheck.

6. USB/charging/external power prevents automatic hardware shutdown.
   - PARTIAL PASS. With USB connected, `~POWER:STATUS` reports `external_power_present=1`, `usb_power_present=1`, and `shutdown_blocker_names=external_power`. `automatic_shutdown_blocked_by_external_power=0` because this capture did not wait until the 30-minute hardware shutdown threshold.

7. If hardware shutdown/recovery/pin evidence fails, artifact records failure symptoms/resource/commit/rework or rollback.
   - PASS. This artifact records the resource (`COM6`), commit (`6b827b69de02099d8dc158c1b915e74cbc7c9069`), observed PWR_HOLD low symptom, and remaining battery-only/physical recovery gate.

## Current Decision

Do not submit this step for review as PASS. The AI-side static/build/flash/serial evidence has been collected, but 1.2 still needs a battery-only or otherwise non-USB power setup that can record:

- short-press cold boot with GPIO11/PWR_HOLD HIGH, or a clear rework decision if it remains LOW;
- automatic long-idle hardware shutdown without USB/external power;
- BLE disconnect or current/power/serial-disappearance proof of complete power-off;
- physical short-press cold boot recovery and basic BLE/key/diagnostic responsiveness after that power-off.

## 2026-06-08 Follow-up Triage

Assisted triage on 2026-06-08 found additional artifacts under `tests/artifacts/firmware_power_off_1_2/`. They do not change the decision above; they make the blocker more concrete.

Additional observed commit/build context:

- Latest validation manifests record commit `45bde78b151ffd061bc7f14af2f300aecb522cb2` with `CONFIG_POWER_MANAGER_HARDWARE_SHUTDOWN_MS=60000`.
- Latest flash manifest: `tests/artifacts/firmware_power_off_1_2/power_off_validation_manifest_60000ms_20260608-122249.json`.
- Latest quiescent status capture: `tests/artifacts/firmware_power_off_1_2/status_query_20260608-122648.log`.
- Latest decoded diag bundle: `tests/artifacts/firmware_power_off_1_2/diag_log_quiesce_20260608-122657.bundle.json`.
- Latest BLE advertisement observation: `tests/artifacts/firmware_power_off_1_2/ble_adv_shutdown_retest_quiesce_20260608-122331.jsonl`.

Key findings:

- Boot readback still reports `PWR_HOLD/GPIO11 hold-high configured readback: requested_level=1 actual_level=0`, followed by `readback mismatch`. Evidence: `tests/artifacts/firmware_power_off_1_2/boot_readback_esptool_reset_20260608-121447.log`.
- `~POWER:STATUS` and `~BOARD:STATUS` still report `pwr_hold_gpio=11 pwr_hold_level=low pwr_hold_configured=1` after reboot/status query. Evidence: `tests/artifacts/firmware_power_off_1_2/status_query_20260608-122648.log`.
- USB status still reports `external_power_present=1`, `usb_power_present=1`, and `shutdown_blocker_names=external_power`, so USB serial remains insufficient to prove automatic battery-only hardware shutdown.
- BLE advertisement captures are intermittent rather than permanently gone. The latest 50-sample quiesce capture saw the target 8 times and missed it 42 times, which is not proof of complete power removal.
- The decoded diag bundle has two boot segments and power events, but it does not prove a successful hardware-off interval followed by physical short-press cold boot.

Current interpretation:

- 1.2 should remain blocked. The latest evidence suggests either the PWR_HOLD/GPIO11 latch path is wired/pulled low in this setup, the firmware-facing GPIO is not actually driving the latch line, or the test still includes a power source that masks the intended hardware-off path.
- Do not approve this step with static or USB-only evidence.

Minimal human/operator retest:

1. Flash or keep the latest 60s validation firmware recorded above.
2. Disconnect USB completely; power the board from battery/no-USB only.
3. Physically short-press the power key to cold boot.
4. Reconnect only if needed for a short status read, then capture `~BOARD:STATUS` and `~POWER:STATUS`.
5. The required pass signal is `pwr_hold_gpio=11 pwr_hold_level=high` after short-press boot. If it remains low, record that as a hardware/rework decision rather than continuing the idle shutdown test.
6. If PWR_HOLD is high, disconnect USB again and wait for the 60s validation shutdown. Capture BLE disappearance and, if available, current/power telemetry. Then short-press to cold boot and verify BLE/key/diagnostic responsiveness.

## 2026-06-09 Latest Contract / Software Rework

Billy asked to re-check the latest power-off contract because the board appeared not to auto power off. Local hardware interface context now says `PWR_HOLD/GPIO11` is an active-high firmware hold after boot, with `EC11-KEY_IO/GPIO18` as the encoder push input. The previous firmware branch had regressed into active-low hold / release-high behavior, which explains a likely "shutdown entered but power never disappears" symptom: releasing the latch HIGH would keep the active-high hold asserted.

Software correction made in this step worktree:

- `components/board/board.c` now preloads and drives `PWR_HOLD/GPIO11` HIGH during boot/runtime, drives it LOW for hardware shutdown, and logs readback mismatch evidence.
- `components/power_manager/power_manager.c` now describes shutdown as `release-low`, keeps long-idle shutdown quiescent if power does not disappear, and prevents BLE connection callbacks from resuming `ACTIVE` while already in `HARDWARE_SHUTDOWN`.
- USB/VBUS is the automatic-shutdown external-power blocker. Charger `CHG/STD` pins remain diagnostics and do not independently block battery-only long-idle shutdown after USB/VBUS is gone.
- Static verification, feature docs, telemetry self-test strings, and BLE/power-off validation helper tools were updated to the same active-high hold / release-low contract.

Validation run after the software correction:

- PASS: `git diff --cached --check`
- PASS: `python .\tools\verify_power_manager_static.py`
- PASS: `pwsh -NoProfile -File .\tools\verify_v2_board_profile_static.ps1`
- PASS: `pwsh -NoProfile -File .\tools\verify_charging_awake_policy_static.ps1`
- PASS: `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
- PASS: `python -m py_compile .\tools\export_ble_diag_log.py .\tools\monitor_ble_advertisements.py`
- PASS: `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`

Current decision after this correction:

- Do not approve 1.2 from static/build evidence alone.
- Next hardware run should flash this corrected step branch, confirm short-press boot reports `pwr_hold_gpio=11 pwr_hold_level=high`, then test no-USB/battery-only long-idle shutdown and short-press cold-boot recovery.

## 2026-06-09 Main Worktree Hardware Recheck

Billy requested that `oai1` continue 1.2 in the main firmware worktree:

- Worktree: `C:\Users\Billy\Desktop\listener\voice-keyboard-firmware`
- Branch: `master`
- Software correction commit: `5af5c874f2b9beb2b35da32e1bc1502069557c99`
- Hardware interface context checked: local hardware docs report `PWR_HOLD/GPIO11` as signed off for active-high firmware hold after boot; `EC11-KEY_IO/GPIO18` remains the encoder push input.

No-lock software validation before hardware:

- PASS: `git diff --cached --check`
- PASS: `python .\tools\verify_power_manager_static.py`
- PASS: `pwsh -NoProfile -File .\tools\verify_v2_board_profile_static.ps1`
- PASS: `pwsh -NoProfile -File .\tools\verify_charging_awake_policy_static.ps1`
- PASS: `pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1`
- PASS: `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
- PASS: `python -m py_compile .\tools\export_ble_diag_log.py .\tools\monitor_ble_advertisements.py`
- PASS: `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
- PASS: 60s validation firmware build after default `build` directory tooling failure was isolated by using a fresh temp build dir:
  - Failed tooling attempt: `tests/artifacts/firmware_power_off_1_2/power_off_validation_build_60000ms_20260609-163420.log`
  - PASS build manifest: `tests/artifacts/firmware_power_off_1_2/power_off_validation_manifest_60000ms_20260609-163533.json`
  - Build dir: `C:\Users\Billy\AppData\Local\Temp\listener-power-off-5af5c87-60000-20260609163509`

Short hardware lock:

- Command: `pwsh -NoProfile -File C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COMx -Owner oai1 -TimeoutMinutes 12 -Run pwsh -NoProfile -File .\tests\artifacts\firmware_power_off_1_2\run_locked_power_off_1_2.ps1 -Port COMx -BuildDir C:\Users\Billy\AppData\Local\Temp\listener-power-off-5af5c87-60000-20260609163509 -ShutdownMs 60000`
- `COMx` resolved to `COM7`.
- Lock was released after the command.
- Locked manifest: `tests/artifacts/firmware_power_off_1_2/power_off_1_2_locked_manifest_20260609-163902.json`
- Flash transcript: `tests/artifacts/firmware_power_off_1_2/power_off_validation_flash_60000ms_20260609-163904.log`
- Serial transcript: `tests/artifacts/firmware_power_off_1_2/power_off_1_2_serial_20260609-163902.log`
- BLE advertisement observation: `tests/artifacts/firmware_power_off_1_2/power_off_1_2_ble_adv_20260609-163902.jsonl`

Hardware results:

- Flash PASS: ESP32-S3 on `COM7`, MAC `a4:cb:8f:f1:43:58`; app version `v1002.0.0-ota-test-210-g5af5c87`; `CONFIG_POWER_MANAGER_HARDWARE_SHUTDOWN_MS=60000`.
- Firmware attempted the latest active-high hold contract, but readback failed:
  - `PWR_HOLD/GPIO11 hold-high configured readback: gpio=11 requested_level=1 actual_level=0 policy=v2_gpio11_power_latch_hold_high_release_low_for_hardware_shutdown`
  - `PWR_HOLD/GPIO11 readback mismatch: requested_level=1 actual_level=0`
  - `board profile ... pwr_hold=11 pwr_hold_level=low pwr_hold_configured=1`
  - `power cold-boot status: reset_reason=11 pwr_hold_gpio=11 pwr_hold_level=0 configured=1`
  - `power manager init ... pwr_hold_gpio=11 pwr_hold_level=low`
- USB/external blocker evidence was present while attached:
  - `power source changed: usb_det=high ... pwr_hold=low usb_power_present=1 ... external_power_present=1`
- After `~BOOT:STATUS`, the serial port returned `ClearCommError` / `WriteFile failed` and the active COM device disappeared from `Win32_SerialPort`; `Get-PnpDevice -Class Ports` listed prior ESP32 serial devices only as `Status=Unknown`.
- BLE advertisement scan by name `listener` saw zero matching advertisements during the 24s window. This is not complete power-off proof by itself because the serial log also shows the device connected to a bonded BLE peer before the serial port disappeared.

Acceptance status from this run:

1. Short `aiw with-lock` resource evidence: PASS, `COMx` resolved to `COM7`, lock released.
2. Flashed firmware commit recorded: PASS, `5af5c874f2b9beb2b35da32e1bc1502069557c99`.
3. Short-press/cold-boot PWR_HOLD HIGH evidence: FAIL, the firmware requested HIGH but hardware read back LOW.
4. Automatic long-idle complete power-off: NOT RUN after pin evidence failed; USB/external power was present during the locked capture.
5. Cold-boot recovery after automatic off: NOT RUN because automatic-off evidence did not pass.
6. USB/external blocker: PARTIAL PASS, USB attach reported `external_power_present=1`; charger-only/no-USB scenario remains untested.
7. Failure artifact: PASS, this section records resource, commit, symptoms, and next action.

Historical decision from the bad active-high run, superseded below:

- Do not submit 1.2 as PASS.
- The interim firmware drove the wrong active-high hold contract, while the actual GPIO11/PWR_HOLD line remained LOW after boot.
- This section is retained as failure evidence only. The current contract is low-active hold and high-release shutdown.

## 2026-06-09 Polarity Correction After Hardware Feedback

Billy corrected the hardware contract after the failed active-high run: the V2 `PWR_HOLD/GPIO11` line must be held LOW during boot/runtime, and driven/released HIGH for hardware shutdown. Driving it LOW for shutdown can make the board restart instead of staying off. This supersedes the earlier local hardware handoff wording that described active-high firmware hold.

Firmware rework in progress:

- `components/board/board.c` policy is now `v2_gpio11_power_latch_hold_low_release_high_for_hardware_shutdown`.
- `board_configure_power_hold_latch()` preloads and drives GPIO11 LOW for runtime hold.
- `board_set_power_hold_enabled(false)` now drives GPIO11 HIGH for shutdown release.
- `components/power_manager/power_manager.c` shutdown messages now say `release-high` and restore `hold low` if power is not removed.
- `docs/features/low_power_wake_policy.md`, `docs/features/firmware-feature-map.md`, `tools/ai/repo_features.ps1`, telemetry self-tests, and static verification were updated so future AI work sees low-active hold/high-release as the current contract.

Current minimum pass signal after this correction:

- Boot/runtime status should report `pwr_hold_gpio=11 pwr_hold_level=low`.
- A real shutdown attempt should drive/release `PWR_HOLD/GPIO11` HIGH and then remove power without reboot looping.
- Until that hardware run is captured, 1.2 remains unsubmitted.
