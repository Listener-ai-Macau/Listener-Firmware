# Audio session identity across firmware restarts

## Root cause

The audio protocol carries a 32-bit `session_id`, and the Type background
listener may retain an orphan-tail quarantine while a BLE notify connection is
being recovered.  The firmware previously reset its session counter to `1`
after every USB, watchdog, or other reboot.  If the first post-reboot session
reused an id that was still quarantined by Type, its `SessionStart` and PCM were
discarded as a replay.  This presented as an intermittent "wake sometimes does
not respond" failure even though the microphone and BLE packet stream were
healthy.

## Fix

Session ids now use a boot-local random 16-bit namespace in the high word and a
16-bit counter in the low word.  The low word remains monotonic for diagnostics;
it is never allowed to wrap within the same namespace.  A new boot therefore
cannot reuse a previous boot's numeric session id except for the negligible
16-bit random collision probability, and a low-word wrap rotates the namespace
again.

This is an identity/transport fix only.  It does not change wake thresholds,
voiceprint scoring, endpoint timing, watchdog limits, audio gain, or the
recording state machine.

## Scheduler root-cause hardening

The retained September 3 incident was a separate firmware path: during an
active PDM/AFE session, the watchdog reported `task_wdt` while CPU0 was running
the audio capture task.  The old guard yielded after a fixed number of frames,
which is not a time bound when I2S/DSP calls or an empty AFE result change their
return cadence.  The capture and AFE-fetch loops now share a 20 ms runnable-time
budget and invoke the same one-tick yield after successful frames, empty/error
results, and I2S recovery.  The watchdog remains strict (`task_wdt=5 s`,
`interrupt_wdt=300 ms`); no timeout is extended or disabled.

## Verification

- `python tools/verify_pdm_afe_scheduler_static.py` passes and requires the
  boot-scoped allocator and the absence of the old `++counter` assignment.
- Firmware build and `flash.ps1 -Port COM5 -NoBuild -PreserveOtaData` completed
  with the existing OTA data and device settings preserved.
- Image SHA256 after the fix is recorded by the build output; the board's
  runtime `build_id` must be checked from a later independent diagnostic boot.
- A future reset is not considered a new watchdog incident unless a fresh boot
  segment reports `interrupt_wdt` or `task_wdt`; a serial/USB reset remains an
  observation boundary, not proof of an audio failure.
