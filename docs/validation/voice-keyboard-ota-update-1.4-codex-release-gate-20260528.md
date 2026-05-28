# voice-keyboard-ota-update/1.4 release gate follow-up

Date: 2026-05-28
Agent: codex

## Result

Partial PASS. The successful OTA path and several failure/core-path checks passed on the COM5 board, but the full 1.4 release gate is not complete because rollback and transfer-interruption validation still need a dedicated test image or harness.

## Device Baseline

- Port: COM5
- BLE address: 14C19F48FE72
- Current firmware after manual OTA: `v1.1.0-manual-ota`
- OTA status after smoke checks: `running=ota_1`, `boot=ota_1`, `pending_verify=0`, `blocker=none`
- Power/status path: `ble_connected=1`, battery reported, no blockers

Artifact:

- `tests/artifacts/ota_release_gate_20260528/final_status_after_smoke.txt`

## Successful OTA Path Already Covered

Manual 1.0 to 1.1 update was confirmed earlier by serial `~OTA:STATUS`:

- From: `v1.0.0-manual-base`
- To: `v1.1.0-manual-ota`
- Confirmed from device, not UI cache: `version=v1.1.0-manual-ota`

Artifacts:

- `tests/artifacts/manual_upgrade_1_0_to_1_1/v1_0_ota_status_single_session.log`
- `tests/artifacts/manual_upgrade_1_0_to_1_1/v1_1_ota_status_after_manual_update.log`

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
- Rollback cannot be honestly validated by `~BOOT:CRASH` after normal boot because firmware calls `firmware_ota_confirm_pending_verify_if_ready()` immediately once POST/BLE/keyboard checks pass. A real rollback test needs a dedicated OTA image or test switch that fails pending-verify self-check before `esp_ota_mark_app_valid_cancel_rollback()`.
- Transfer interruption was not run in this pass; it needs either a controllable BLE abort harness or a desktop transfer cancel/disconnect test that preserves device recovery evidence.

## Remaining For 1.4 Review

- Build or enable a rollback-test OTA image that deliberately fails pending-verify, then verify automatic rollback to the previous partition and desktop rolled-back/failure state.
- Run a real BLE transfer interruption test and confirm retry/recovery.
- Cover recording-active OTA request blocker from the desktop path.
- Decide whether same-version headless preflight should become a blocker or remain a testing-only warning.

