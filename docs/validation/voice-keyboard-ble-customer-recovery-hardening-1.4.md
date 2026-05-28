# voice-keyboard-ble-customer-recovery-hardening 1.4 Validation

Date: 2026-05-28
Agent: tai1
Repo: voice-keyboard-firmware
Branch: ai/tai1-voice-keyboard-ble-customer-recovery-hardening-1.4

## Scope

- Firmware now publishes separate HID, audio, OTA, and diagnostic readiness masks instead of collapsing subsystem failures into one global state.
- DIS identity is explicitly completed for manufacturer, serial, model, hardware revision, firmware revision, software/protocol revision, and HID VID/PID/version logging.
- OTA status and the OTA GATT service expose readiness/capabilities so desktop recovery can still read identity through the OTA path when audio is degraded.
- BLE Service Changed persistence now includes GATT schema revision `ota_identity_v2`, so a GATT-shape change can request host refresh even when the firmware version string is unchanged.
- BLE recovery actions add serial and diag_log evidence for clear bonds, connection terminate, advertising restart, completion, and failures.

## Automated Validation

All commands below passed from the claimed firmware worktree unless noted.

```powershell
python .\tools\verify_ble_ota_gatt_contract.py
pwsh -NoProfile -File .\tools\verify_diagnostic_log_coverage.ps1
pwsh -NoProfile -File .\tools\verify_power_manager_static.ps1
python -m compileall -q tools
pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check
git diff --check
```

Build validation was run from a short-path temporary worktree because the claimed worktree path is long enough to trip Windows dependency-file path limits in ESP-IDF managed components.

```powershell
pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3
```

Result from `C:\lfw14` / `C:\lfw14hw`: PASS. `voice-keyboard-firmware.bin` built successfully; final hardware validation build produced binary size `0xa58c0`, smallest app partition `0x1b0000`, `0x10a740` bytes (62%) free.

## Hardware Validation

Hardware lock was used for real device operations.

```powershell
pwsh -NoProfile -File C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COM5 -Run ...
```

PASS:

- Built and flashed the final diff to ESP32-S3 over `COM5`.
- Flash target reported ESP32-S3 USB-Serial/JTAG, MAC `14:c1:9f:48:fe:70`.
- Serial `~POWER:STATUS` returned power state, blockers, battery, wake policy, and wake limitation.
- Serial `~OTA:STATUS` returned readiness/capabilities with `ready_mask=0x0000000f`, `degraded_mask=0x00000000`, `hid_ready`, `audio_ready`, `ota_ready`, `diagnostic_ready`, and `firmware_ota_v1`.
- Serial `~DIAGLOG:COUNT` returned `5360`.
- Serial `~OTA:GATT` showed OTA service registered with readiness and capabilities handles: `readiness_rc=0`, `readiness_val=15`, `capabilities_rc=0`, `capabilities_val=17`.
- Serial `~DIS:GATT` showed DIS firmware, hardware, model, serial, software, manufacturer, and PnP handles all discoverable.

Artifacts:

- Local raw serial logs were captured under ignored `tests/artifacts/`:
  `ble_recovery_1_4_serial_20260528-113207.log`,
  `ble_recovery_1_4_gatt_20260528-114020.log`, and
  final evidence `ble_recovery_1_4_serial_20260528-114849.log`.

Raw evidence excerpts:

```text
~POWER:STATUS state=ACTIVE blockers=0x00000000 blocker_names=none idle_ms=0 ble_connected=1 battery_mv=4130 battery_level=94 battery_valid=1 last_sleep_reason=none last_wake_source=power_on guard=1 audio_idle_ms=5000 connected_idle_ms=30000 disconnected_idle_ms=30000 overnight_sleep_ms=900000 wake_policy=key4_only wake_gpio_mask=0x0000000000200000 wake_capable_keys=KEY4/GPIO21 wake_key_gpio=21 wake_key_rtc_capable=1 voice_key_gpio=35 voice_key_rtc_capable=0 voice_key_deep_sleep_wake=0 voice_key_limitation="GPIO35 voice key is not RTC deep-sleep wake capable on V1" wake_user_action="press KEY4/GPIO21 after deep sleep"
I (5918) firmware_ota: OTA STATUS running=ota_0 running_offset=0x00020000 boot=ota_0 boot_offset=0x00020000 update=ota_1 update_offset=0x001d0000 update_size=1769472 version=v1002.0.0-ota-test-dirty target=none active=0 pending_verify=0 state=2 bytes=0 expected=0 blocker=none ready_mask=0x0000000f degraded_mask=0x00000000 readiness=factory_ready;pairable_on_boot;post_degraded_boot;hid_ready;audio_ready;ota_ready;diagnostic_ready capabilities=ble_hid_keyboard;ble_audio_vka1;usb_serial_text;key1_record_toggle;post_status;firmware_ota_v1;hid_ready;audio_ready;ota_ready;diagnostic_ready
I (5978) diag_log: DIAGLOG COUNT: 5360
I (5988) ble_firmware_ota: firmware OTA GATT state: registered=1 svc_rc=0 svc_handle=9 control_rc=0 control_def=10 control_val=11 data_rc=0 data_def=12 data_val=13 readiness_rc=0 readiness_def=14 readiness_val=15 capabilities_rc=0 capabilities_def=16 capabilities_val=17
I (6018) ble_hid: DIS GATT state: svc_rc=0 svc_handle=40 model_rc=0 model_def=41 model_val=42 serial_rc=0 serial_def=43 serial_val=44 firmware_rc=0 firmware_def=47 firmware_val=48 hardware_rc=0 hardware_def=45 hardware_val=46 software_rc=0 software_def=49 software_val=50 manufacturer_rc=0 manufacturer_def=51 manufacturer_val=52 pnp_rc=0 pnp_def=55 pnp_val=56
```

## BLE Preflight Note

Windows PnP cache shows paired device `listener` at BLE address `14C19F48FE72` and cached OTA/DIS services. After the `ota_identity_v2` service-changed revision was flashed, a final locked Windows PowerShell 5.1 run of `tools\verify_ble_ota_gatt_discovery.ps1 -BluetoothAddress 14C19F48FE72 -TimeoutSeconds 25` could open the device address but WinRT uncached GATT discovery still returned `0x80070016` ("The device does not recognize the command"). Earlier cached discovery showed the existing OTA control/data characteristics but not the newly added readiness/capabilities characteristics, which is consistent with stale Windows GATT cache after the service shape changed.

This step confirms the firmware registers and serially exposes the required GATT handles and readiness fields. End-to-end Listener-Type preflight refresh on Windows remains a real-device cache/recovery matrix item for dependent step 1.5.
