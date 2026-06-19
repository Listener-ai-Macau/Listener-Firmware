# Ad-hoc LED Stack Disposition

Date: 2026-06-19
Agent: oai1
Workflow step: `voice-keyboard-firmware-full-function-test/1.13a`
Worktree: `C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware-wt-oai1-voice-keyboard-firmware-full-function-test-1.13a`
Branch: `ai/oai1-voice-keyboard-firmware-full-function-test-1.13a`

## Included LED Work

| Source | Disposition | Evidence |
|---|---|---|
| `origin/master` through `eabcc9b` | Included by fast-forward before 1.13a LED work. | Brings the accepted base, including `83490b6 Add EC11 and edge status LED accents`, `9b52989 Fix EC11 rotation direction`, `54a837c Improve LED true-state diagnostics`, `3fa3fbf Improve status LED flicker guard and shutdown confirm`, and `eabcc9b Split device timing by power source`. |
| `adhoc/oai1/status-led-recording-effect` | Included through master ancestry; no separate cherry-pick needed. | The branch tip is an ancestor of the 1.13a branch. |
| `adhoc/oai1/led-true-state-accents` | Included through master ancestry; no separate cherry-pick needed. | The branch tip is an ancestor of the 1.13a branch. |
| `adhoc/oai1/led-flicker-guard-shutdown-confirm` commit `8ee6812` | Included as 1.13a commit `def85f4 Harden status LED tail and restore recording OK`. | Carries the REC/AI status-tail guard and restores the recording OK path. |
| `adhoc/oai1/led-flicker-guard-shutdown-confirm` commit `29403fd` | Included as 1.13a commit `c41ee3d Add flicker-safe LED zone brightness controls`. | Carries `~DEVICE:SET led_status/led_key/led_ec11/led_edge` persisted 0-100 caps, LED status brightness reporting, and human review tooling. |
| `adhoc/oai1/led-flicker-guard-shutdown-confirm` commit `d48b366` | Included as 1.13a commit `d0e3f4f Finish LED product effect pass`. | Carries the product-scene LED pass: slow low-load REC/AI, EC11/edge motion, BLE re-pair cue, EC11 input feedback, low battery/charging behavior, and 1.13a disposition scaffold. |
| `adhoc/oai1/led-flicker-guard-shutdown-confirm` commit `99fa8a1` | Included as 1.13a commit `94d9d38 Stabilize REC AI status tail for LED review`. | Carries the 1.9 root-cause fix: REC/AI overlap capped to a 14%-16% slow status-tail envelope, explicit LED5/6 zeroing, effect-only preview isolation, and the final 1.9 human PASS artifacts. |
| 2026-06-19 TailOnly rework | Included in the current 1.13a branch after human precheck. | The first TailOnly 1.13b precheck kept software and observed `LED5=OK`/`LED6=WARN` off, but failed because REC/AI motion was too subtle. The overlap envelope is now `16%-30%` on a `1.4s` processing-start phase, with the same explicit LED5/6 zeroing, guard pixels, status-strip-last transmit order, and effect-only isolation. |

## Excluded Or Preserved Work

| Source | Disposition | Reason |
|---|---|---|
| `adhoc/oai1/led-flicker-guard-shutdown-confirm` merge-only commit `52c3041` | Excluded as a separate cherry-pick. | The 1.13a branch was first fast-forwarded to `origin/master`; the merge commit carries no unique product change needed after replaying the content commits above. |
| Power-hold shutdown failsafe ad-hoc stacks | Excluded and preserved. | Out of 1.13a LED-device-settings scope; belongs to a power/shutdown workflow step. |
| Hardware-revision/current-sense ad-hoc stacks | Excluded and preserved. | Out of 1.13a LED-device-settings scope; belongs to hardware revision or current-sense integration. |
| Low-power battery sampling stacks | Excluded and preserved. | Out of 1.13a LED-device-settings scope; belongs to low-power/battery behavior work. |
| BLE recovery capsule and Listener-Type settings readback stacks | Excluded and preserved. | Cross-repo or desktop-side scope; not needed for firmware LED effect readiness. |

## Current 1.13a Scope Covered

- Slow, low-duty but human-visible REC/AI status LEDs that keep `LED5=OK` and `LED6=WARN` semantically off unless a real success/error/shutdown owner is active.
- Per-zone persisted LED caps: `led_status`, `led_key`, `led_ec11`, and `led_edge`.
- `~LED:STATUS detail=contract|brightness|state|power|rgb|summary` lines needed for 1.13b review, including RGB frames for status, key, EC11, and edge zones.
- Recording/processing product effects that avoid high-speed full-zone chasing on shared status paths while preserving visible EC11 and edge/frame motion.
- BLE re-pair confirmation, key/EC11 input feedback, low battery, plugged/charging/full, OK completion, and long-press shutdown confirmation coverage.
- Human review tooling and docs explaining effect-only preview modes and the 1.9 LED5/6 flicker root cause.

## Visual Model Artifact

`docs/validation/voice-keyboard-firmware-full-function-test-1.13a/led-effect-static-model.png` is a static model screenshot for the AI-verifiable 1.13a evidence audit. It shows the intended no-flicker baseline: REC/AI active at low duty, OK/WARN held black, EC11 all-present low warm-gold flow, and edge/frame all-present without refreshing an unchanged status rail. The current rework makes the REC/AI overlap more visible than the original static model while preserving the same OK/WARN-off invariant. It is not a substitute for the real hardware visual review in 1.13b.

## Remaining Gate

This step prepares firmware that is safe to flash for the next human/hardware visual review. The final visual judgment is intentionally left to `voice-keyboard-firmware-full-function-test/1.13b`.
