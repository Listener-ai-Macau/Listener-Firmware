# voice-keyboard-ota-update/1.4 release gate follow-up

Date: 2026-05-28
Agent: codex

## Result

PASS. The successful OTA path, rollback path, transfer-interruption retry path, recording-active blocker, final manual UI reflash, and several failure/core-path checks passed on the COM5 board. The final Listener-Type OTA panel follow-ups were rebuilt and accepted for 1.4 review.

## Device Baseline

- Port: COM5
- BLE address: 14C19F48FE72
- Current firmware after manual OTA: `v1.1.0-manual-ota`
- OTA status after smoke checks: `running=ota_1`, `boot=ota_1`, `pending_verify=0`, `blocker=none`
- Power/status path: `ble_connected=1`, battery reported, no blockers

Artifact:

- `tests/artifacts/ota_release_gate_20260528/final_status_after_smoke.txt`
- `tests/artifacts/ota_release_gate_20260528/pre_rollback_serial_status_blocked_by_ble_lock.txt`
- `tests/artifacts/ota_release_gate_20260528/final_status_after_retry_rollback.txt`
- `tests/artifacts/ota_release_gate_20260528/manual_ui_upgrade_status_after_user_run.txt`

## Rollback Test Image

A dedicated rollback validation OTA image was built with `LISTENER_OTA_FORCE_PENDING_VERIFY_FAIL=1`.
The default production build is unchanged unless that environment variable is set.

- Version: `v1.2.0-rollback-test`
- Channel: `internal-test`
- BLE write chunk: `500`
- Firmware size: `678208` bytes
- SHA256: `82fd64874d20277c7e41e8ca85154aad8e2f1e3a82395a308e25deb3aabad4c3`
- Package: `C:\Users\Billy\Desktop\listener\voice-keyboard-firmware-wt-codex-ota-rollback-test\.cache\ota_rollback_test_20260528\listener-ota-v1.2.0-rollback-test-20260528-141148`

The rollback transfer wrote the full image and the test image deliberately failed pending-verify self-check. The bootloader returned to `v1.1.0-manual-ota`; final serial status reported `active=0`, `pending_verify=0`, and `blocker=none`.

During this validation, the first retry exposed two desktop-side issues:

- The old release binary still selected a stale Windows BLE service path that parsed as `D0216C0D05DF`; rebuilding from `Listener-Type` commit `54c1399` picked up the VID/PID service-id address parser and selected `14C19F48FE72`.
- OTA prepare still opened the BLE audio notify keepalive and failed with `BLE OTA keepalive CCCD notify write returned status=GattCommunicationStatus(2)`. `Listener-Type` was changed to keep OTA on the independent BLE OTA GATT service and stop opening the BLE audio notify keepalive.

Artifacts:

- `tests/artifacts/ota_release_gate_20260528/rollback_transfer_headless.txt`
- `tests/artifacts/ota_release_gate_20260528/pre_rollback_serial_status_blocked_by_ble_lock.txt`
- `tests/artifacts/ota_release_gate_20260528/ble_host_recovery_before_rollback.txt`
- `tests/artifacts/ota_release_gate_20260528/rollback_transfer_headless_after_ble_recovery.txt`
- `tests/artifacts/ota_release_gate_20260528/rollback_final_status_after_finish_timeout.txt`
- `tests/artifacts/ota_release_gate_20260528/ble_host_recovery_after_keepalive_removal.txt`
- `tests/artifacts/ota_release_gate_20260528/transfer_retry_after_interruption_no_keepalive.txt`
- `tests/artifacts/ota_release_gate_20260528/final_status_after_retry_rollback.txt`

## Transfer Interruption And Retry

A transfer was interrupted by killing the headless desktop process after 20 seconds. The board stayed on `v1.1.0-manual-ota`; serial status showed the partial OTA session still active with `bytes=143000`, `expected=678208`, and `blocker=ota_in_progress`.

A subsequent desktop OTA retry recovered by issuing the normal pre-begin abort, transferred the full `678208` bytes, booted the rollback-test image, and returned to `v1.1.0-manual-ota`. Final serial status confirmed `target=none`, `active=0`, `pending_verify=0`, and `blocker=none`.

Artifacts:

- `tests/artifacts/ota_release_gate_20260528/transfer_interruption_kill_after_20s_no_keepalive.txt`
- `tests/artifacts/ota_release_gate_20260528/transfer_interruption_kill_after_20s_no_keepalive.txt.with_lock.txt`
- `tests/artifacts/ota_release_gate_20260528/transfer_interruption_status_after_kill.txt`
- `tests/artifacts/ota_release_gate_20260528/transfer_retry_after_interruption_no_keepalive.txt`
- `tests/artifacts/ota_release_gate_20260528/final_status_after_retry_rollback.txt`

## Successful OTA Path Already Covered

Manual 1.0 to 1.1 update was confirmed earlier by serial `~OTA:STATUS`:

- From: `v1.0.0-manual-base`
- To: `v1.1.0-manual-ota`
- Confirmed from device, not UI cache: `version=v1.1.0-manual-ota`

Artifacts:

- `tests/artifacts/manual_upgrade_1_0_to_1_1/v1_0_ota_status_single_session.log`
- `tests/artifacts/manual_upgrade_1_0_to_1_1/v1_1_ota_status_after_manual_update.log`
- `tests/artifacts/ota_release_gate_20260528/manual_ui_upgrade_status_after_user_run.txt`

## Manual UI Reflash And Recording Blocker

The user ran a manual UI OTA reflash from Listener-Type on 2026-05-28. Serial evidence after the run showed:

- Current running/boot partition: `ota_0`
- Current firmware: `v1.1.0-manual-ota`
- OTA state: `target=none`, `active=0`, `pending_verify=0`, `bytes=0`, `expected=0`, `blocker=none`
- Recent diagnostic events include OTA begin, full write, finish, reboot into the updated partition, and app-valid confirmation.

The user also verified the recording-active blocker from the desktop UI. With recording state `Listening`, the OTA panel blocked update start and showed the localized Chinese blocker. A small UI follow-up was found and fixed in Listener-Type: the panel no longer repeats the raw English backend message, and same-version packages remain allowed while showing a localized reflash warning.

Final Listener-Type UI follow-ups:

- `5378432` keeps OTA on the independent BLE OTA service instead of opening BLE audio notify keepalive.
- `83a8d43` localizes/simplifies blocker and same-version warning text; same-version reflash remains allowed.
- `54641e1` makes the firmware-version query stay in `查询中` until a device firmware version is read or the timeout expires.
- `7881d51` sets that firmware-version query timeout to 15 seconds.
- `f8def7b` limits the 15-second firmware-version wait to the manual query button and immediately exits `查询中` when a firmware version is already present.
- `b0f6083` fixes the Windows BLE metadata read path: when the OTA device handle exposes the OTA service but returns empty DIS metadata, Listener Type falls back to the discovered DIS service and reads hardware revision, firmware revision, and battery level from there.
- Rebuilt and launched `src-tauri\target\release\listener-type.exe` after the final UI changes; latest exe timestamp: 2026-05-28 21:18.

Final UI validation:

- `npm run test:firmware-ota`: PASS.
- `npm run build`: PASS.
- `cargo build --release --manifest-path src-tauri\Cargo.toml`: PASS, with the pre-existing unused-import warning in `src\coordinator.rs`.
- Before `b0f6083`, headless preflight connected to the OTA service but returned `hardwareRevision=null`, `firmwareVersion=null`, and `batteryPercent=null`, causing the UI query to time out.
- After `b0f6083`, the same headless preflight returned PASS with `hardwareRevision=keyboard-v1`, `firmwareVersion=v1.1.0-manual-ota`, and `batteryPercent=85`.

Artifact:

- `tests/artifacts/ota_release_gate_20260528/manual_ui_upgrade_status_after_user_run.txt`

## Failure Paths Covered

- Hash mismatch package check failed as expected with `Firmware SHA256 does not match ota_manifest.json.`
- Hardware mismatch package check failed as expected with `Hardware revision mismatch: package=keyboard-v2, expected=keyboard-v1.`

Artifacts:

- `tests/artifacts/ota_release_gate_20260528/hash_mismatch_check.txt`
- `tests/artifacts/ota_release_gate_20260528/hardware_mismatch_check.txt`

## Core Paths After OTA

- HID smoke passed after OTA. Serial text `ota14` reached firmware and HID dispatch completed with `connected=yes`.
- BLE audio/product-chain A1 smoke passed after OTA: `full_chain_status=PASS`, `missing_packets=0`, transcript matched with `cer=0.0`.
- Diagnostic log dump passed after OTA and exported 691 events.

Artifacts:

- `tests/artifacts/ota_release_gate_20260528/hid_smoke_after_ota.txt`
- `tests/artifacts/ota_release_gate_20260528/audio_A1_smoke_after_ota.txt`
- `tests/artifacts/ota_release_gate_20260528/diag_dump_after_ota.txt`
- `tests/artifacts/diag_log_20260528-140017.jsonl`
- `tests/artifacts/ble_product_matrix/matrix_result.json`

## Findings

- Headless preflight currently returns PASS for the same-version package because the headless CLI sets `current_firmware_version: None` before package validation. The UI can read the device firmware version, but this headless path does not feed it into same-version validation.
- Rollback must be validated with a dedicated pending-verify failure image; `~BOOT:CRASH` after a normal successful boot is not a valid rollback test because the app confirms pending verify early once POST/BLE/keyboard checks pass.
- A hard process-kill interruption leaves the firmware OTA session active until the next abort/retry. The retry path recovered successfully, but a product cancel/disconnect UX should surface "retrying/recovering" rather than leaving the user guessing.

## Remaining For 1.4 Review

- No open 1.4 implementation item remains after the user accepted the manual UI behavior and requested review submission.
