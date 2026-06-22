# firmware-charging-awake-policy / 1.2 partial hardware validation

Date: 2026-06-07
Agent: oai3
Branch: ai/oai3-firmware-charging-awake-policy-1.2
Result: PARTIAL, blocked by real hardware/manual evidence gates

## Validation runs

No-lock checks passed before hardware access:

- PASS: `pwsh -NoProfile -File .\tools\build.ps1 -Target esp32s3`
- PASS: `pwsh -NoProfile -File .\tools\verify_charging_awake_policy_static.ps1`
- PASS: `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
- PASS: PowerShell parser for `tools\verify_charging_awake_policy_hardware.ps1`
- PASS: `git diff --check`

Locked hardware command used for both hardware captures:

```powershell
pwsh -NoProfile -File ..\ai-collaboration-workflow\scripts\aiw.ps1 with-lock -Resource COMx,BLE-A4CB8FF459A6 -Run pwsh -NoProfile -File .\tools\verify_charging_awake_policy_hardware.ps1 -Port COMx -BluetoothAddress A4CB8FF459A6
```

`aiw with-lock` resolved `COMx` to `COM6`, locked `COM6` and `BLE-A4CB8FF459A6`, ran the command, and released both locks.

Local artifact directories:

- `tests\artifacts\firmware_charging_awake_policy_1.2_20260607-084941`
- `tests\artifacts\firmware_charging_awake_policy_1.2_20260607-101512`

## Passing live evidence

Across the two locked runs, the script captured:

- `~BOARD:STATUS` from the live board.
- `~POWER:STATUS` with `external_power_present=1`, `usb_power_present=1`, `charging=1`, `charge_full=0`, `shutdown_blockers=external_power`, and `hardware_shutdown_ms=1800000`.
- `~DIAGLOG:LAST:240` with 240 JSON diagnostic events.
- EC11 USB command responsiveness through `~EC11:STATUS` and `~EC11:ROTATE:CW`.
- BLE HID responsiveness: `hidawake12` reached firmware as `SCRIPT RX`, and `hid_keyboard: send_ascii done` was logged for the sent characters.
- Powered/external idle threshold evidence in the 08:49 run: the diag tail contained `DIAG_POWER_SLEEP_BLOCKED` and `DIAG_POWER_EXTERNAL_POWER` events at `idle_ms=1801384`, with external-power blocker `0x80`, at or beyond `threshold_ms=1800000`.
- Fresh 10:15 rerun confirmed the current board remained responsive over COM6/BLE-A4CB8FF459A6 and still reported V2/N16R8 external-power shutdown blocking, but its recent diag tail no longer contained the earlier threshold block event.

## Blocking gaps

The run did not satisfy the full 1.2 acceptance gate:

- Battery-only automatic sleep evidence was not present in the captured diag tail. It cannot be produced through live USB serial polling because the USB cable itself keeps `external_power_present=1`.
- The explicit `~POWER:SLEEP` manual override was not executed by default because it can leave the board asleep/off until a physical KEY4/GPIO21 wake or reset path is performed.
- The live device reports `profile=voice-keyboard-v2-n16r8`, `module=ESP32-S3-WROOM-1-N16R8`, `flash_mb=16`, `psram_mb=8`, and hardware-shutdown fields such as `shutdown_blockers`/`hardware_shutdown_ms`.
- This worktree's build profile remains `voice-keyboard-n4`, `ESP32-S3-WROOM-1-N4`, `flash_mb=4`, `psram_mb=0`. I did not flash this N4 build onto the current V2 N16R8 device.
- The unplug-after-long-charging acceptance item still needs a physical unplug/replug or post-unplug persisted diagnostic capture. The current USB serial polling path resets activity and cannot prove the post-unplug battery idle policy by itself.

## Required unblock evidence

- Provide a target device with the 1.1 firmware already available, or provide an explicit matching V2/N16R8 build/flash target for this validation.
- Run an unplugged battery-only idle soak through the configured threshold, then physically wake/reset and capture persisted `~DIAGLOG:LAST` plus `~POWER:STATUS` showing the automatic battery-only sleep path.
- After a long USB/charging idle period, physically unplug and capture persisted diagnostics showing battery-mode idle resumes without immediate deep sleep from stale plugged-in idle time.
- Run the explicit `~POWER:SLEEP`/manual override while charging only when a physical wake/reset action is available, then capture the persisted manual sleep reason.
