# P13 BLE notify ready review

Date: 2026-05-18
Step: P13 2.2
Reviewed commit: `758f296534696654afe9069ea71d1e2ae91f718b`

## Decision

No separate merge is needed for `758f296`: `master` already contains the
same patch as `bd35d10e5681e5606b1729336187a7f70c2cee13`.

## Evidence

- `git diff 758f296 bd35d10 -- tools/capture_audio_ble_wav.py` is empty.
- `git patch-id --stable` returns the same patch id for both commits:
  `6b0a599b4b9b853beaafacdc5753c9d37bb1c337`.
- `git cherry -v master ai/codex-p13` marks `758f296` with `-`, confirming
  an equivalent patch is already reachable from `master`.

## Code Review

The change is limited to `tools/capture_audio_ble_wav.py`.

- Adds `line_indicates_notify_ready()` to centralize accepted notify-ready
  serial log markers.
- Accepts the newer `audio transport state: ... notify=1` log shape in
  addition to older subscription markers.
- Makes notify-ready and packet-size serial marker waits advisory after host
  CCCD notify enable succeeds, reducing false failures when firmware logs are
  delayed or already consumed.
- Keeps reset detection checks after each advisory wait, so no-reset captures
  still fail on unexpected reboot.

No blocking issue found. The change is suitable to keep on `master`.

## Verification

- `python -m py_compile tools/capture_audio_ble_wav.py`
- `git diff --check 758f296^ 758f296 -- tools/capture_audio_ble_wav.py`
- Existing `tests/**/*.log` samples include notify-ready lines matched by
  `line_indicates_notify_ready()`.
