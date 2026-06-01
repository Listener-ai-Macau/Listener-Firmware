# cross-repo-diagnostic-export 1.1 rework validation

## Scope

Rework for review defects on firmware BLE diagnostic log export:

- Make diagnostic notifications safe for the negotiated ATT MTU instead of assuming a high default.
- Provide locked real-device evidence that a paginated diagnostic pull completes during BLE audio capture, with count/CRC validation and no audio regression.

## Implementation notes

- Diagnostic export now tracks the active GAP connection, refreshes `ble_att_mtu(conn_handle)` on start/read, subtracts the 3 byte ATT header, and rejects chunks that would exceed the negotiated attribute value payload.
- Diagnostic event wire size is explicit as `DIAG_LOG_EVENT_WIRE_BYTES` and the ESP32 flash event layout has a compile-time size assert.
- Diagnostic chunks are capped to 4 events per notification to avoid BLE mbuf pressure while audio notifications are active.
- GAP connect/MTU/disconnect paths feed the diagnostic exporter, and disconnect aborts any active export.
- GATT schema rev is bumped to `diag_export_v1` so bonded Windows hosts are forced off stale service caches after adding the diagnostic service.
- Added `~DIAG:GATT` serial evidence command and repo-owned validation tools:
  - `tools/verify_ble_diag_log_gatt_contract.py`
  - `tools/verify_ble_diag_log_audio_concurrency.py`
  - `tools/verify_ble_diag_log_audio_concurrency.ps1`

## Hardware context

- Agent: `oai2`
- Port: `COM5`
- ESP32-S3 flash MAC: `14:c1:9f:48:fe:70`
- BLE identity used for final validation: `DE:61:FF:ED:2F:5F`
- Prior Windows cache issue: the old bonded identity `E8:0B:41:BC:C9:A1` hid the newly added diagnostic service. `~VREC:RECOVERY` cleared pairing/session state and rotated the static random identity; final evidence below uses the fresh identity.

## Validation commands

- `pwsh -NoProfile -File .\tools\build.ps1`
  - PASS after rebase to current `origin/master`; app binary `0xab7f0`, 60 percent free.
- `aiw with-lock -Resource COM5,BLE-DE61FFED2F5F -Run pwsh -NoProfile -File .\tools\flash.ps1 -Port COM5`
  - PASS; current HEAD app binary `0xab7f0`, flashed and hard reset successfully.
- `aiw with-lock -Resource COM5 -Run <serial ~DIAGLOG:CLEAR>`
  - PASS; serial log included `DIAGLOG CLEAR: all logs erased`.
- `aiw with-lock -Resource COM5,BLE-DE61FFED2F5F -Run pwsh -NoProfile -File .\tools\verify_ble_diag_log_audio_concurrency.ps1 -Port COM5 -BluetoothAddress DE61FFED2F5F -CaptureSeconds 6 -NoResetBeforeCapture`
  - PASS; see real-device evidence below.
- `python .\tools\verify_ble_diag_log_gatt_contract.py`
  - PASS: `BLE diagnostic log GATT contract covers MTU-safe chunking and GAP integration.`
- `python -m py_compile .\tools\verify_ble_diag_log_audio_concurrency.py .\tools\verify_ble_diag_log_gatt_contract.py`
  - PASS.
- `python -m compileall -q tools`
  - PASS.
- `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
  - PASS.
- `git diff --check`
  - PASS; only CRLF conversion warnings from Git on touched files.

## GATT evidence

Serial `~DIAG:GATT` after final flash:

```text
I (1374) ble_diag_log: diag log GATT state: registered=1 svc_rc=0 svc=18 control_rc=0 ctrl_def=19 ctrl_val=20 data_rc=0 data_def=21 data_val=22 count_rc=0 count_def=24 count_val=25
```

Windows BLE enumeration on `DE:61:FF:ED:2F:5F`:

```text
found_device=True address=DE:61:FF:ED:2F:5F name=listener
connected=True service_count=8
diag_service=710af845-6d9f-6583-0c4d-9e5b3bc3093a
diag_char=710af845-6d9f-6583-0c4d-9e5b3bc3093b handle=19 props=write
diag_char=710af845-6d9f-6583-0c4d-9e5b3bc3093c handle=21 props=notify
diag_char=710af845-6d9f-6583-0c4d-9e5b3bc3093d handle=24 props=read
```

## BLE audio plus diagnostic export evidence

Artifact summary:

- `tests/artifacts/ble_diag_log/ble_diag_log_audio_concurrency_summary.json`
- `tests/artifacts/ble_diag_log/ble_diag_log_export_latest.bin`
- `tests/artifacts/ble_diag_log/ble_diag_audio_concurrency_serial.log`
- `tests/artifacts/ble_diag_log/capture_ble_latest_16k_mono.wav`

Summary values from `ble_diag_log_audio_concurrency_summary.json`:

- Status: `PASS`
- Diagnostic exported event count: `46`
- Diagnostic snapshot count: `46`
- Final count after export: `47`
- Diagnostic chunk count: `12`
- Max events per chunk observed: `4`
- Max value bytes observed: `104`
- Aggregate event CRC32: `0x8512bb74`
- Event SHA256: `0493ffd34d2a2477b35d5c1349775ee95aece863a960f8e5a6c07343020eb58a`
- Diagnostic export duration: `2.204s`
- Diagnostic start after audio toggle: `0.750s`
- Diagnostic end after audio toggle: `2.954s`
- Overlap with audio capture: `true`
- Audio expected packets: `408`
- Audio received packets: `408`
- Audio missing packets: `0`
- Audio packet loss ratio: `0.0000`

Representative serial evidence:

```text
I (58544) ble_diag_log: diag export MTU updated: reason=start conn=1 mtu=517 value_max=512
I (58554) ble_diag_log: export session started, conn=1 total=46 mtu=517 value_max=512 events_per_chunk=4
I (58614) ble_diag_log: sent chunk offset=0 count=4 total=46 crc=0xf04546b5 value_len=104 value_max=512
I (59504) ble_diag_log: sent chunk offset=44 count=2 total=46 crc=0xa6b8e555 value_len=56 value_max=512
I (59524) ble_diag_log: export session stopped
I (64014) ble_audio_stream: audio session transport summary: session=1 reason=stop expected_packet_count=408 notify_sent=412 notify_failed=0 notify_retries=0 retry_mbuf=0 retry_enomem=0 retry_tx_timeout=0 retry_tx_status=0 retry_other=0 audio_sent=408 audio_failed=0 queue_jobs_purged=0 pool_high_water=17 pool_capacity=36 pool_high_water_pct=47 pool_alloc_failed=0 queue_full=0 last_drop_reason=none last_error=0
```

## Acceptance mapping

- BLE characteristic for firmware `diag_log` pull: PASS. Diagnostic service UUID `710af845-6d9f-6583-0c4d-9e5b3bc3093a` is registered and visible to Windows; control/data/count characteristics are write/notify/read.
- Pagination for large logs: PASS. The validation pulled 12 pages and verified offset/count on every notification.
- Transfer completion check: PASS. The host exported the full snapshot count and verified per-chunk firmware CRC plus aggregate CRC/SHA.
- Does not disturb normal audio flow: PASS. The diagnostic pull overlapped the active BLE audio session, and audio completed with 408/408 packets and 0 missing packets.
