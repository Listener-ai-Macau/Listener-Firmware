## Product behavior

Describe the device behavior that changes and the user-visible result.

## Validation

- [ ] `pwsh -NoProfile -File .\tools\build.ps1`
- [ ] Relevant static validation under `tools/`
- [ ] Hardware path exercised on the intended V2 profile
- [ ] Relevant audio/BLE/power/OTA counters or LED sequence recorded in this PR

## Compatibility and recovery

- Listener Type version/protocol impact:
- Pairing/settings persistence impact:
- Power and battery impact:
- OTA/rollback or wired recovery path:

## Release safety

- [ ] No build output, managed components, serial logs, diagnostic bundles, recordings, or firmware binaries were committed.
- [ ] No desktop transcript or writing policy was moved into firmware.
- [ ] `docs/features/firmware-feature-map.md` was updated when the supported surface changed.
