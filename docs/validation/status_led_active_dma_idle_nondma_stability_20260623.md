# Status LED Active-DMA / Idle-Non-DMA Stability Evidence - 2026-06-23

## Problem

During plugged low-power idle, the physical status rail could leave `LED3` through `LED6` latched on while firmware reported only `LED1=PWR` as active:

- `~POWER:STATUS state=DISCONNECTED_IDLE`
- `~LED:STATUS detail=rgb status_rgb=PWR:6,6,6;BLE:0,0,0;REC:0,0,0;AI:0,0,0;OK:0,0,0;WARN:0,0,0`
- Physical observation: `LED3`-`LED6` were on.

That split made the idle failure a status-rail transport/latch problem, not a semantic renderer problem.

## Historical Constraint

Do not solve the idle latch issue by permanently disabling status-strip DMA.

The earlier advanced LED-effect work established that REC/AI status effects depend on the status-only DMA path for the no-flicker baseline. The relevant historical evidence is:

- `docs/validation/voice-keyboard-firmware-full-function-test-1.13a/ad-hoc-led-stack-disposition.md`
- `docs/validation/voice-keyboard-firmware-full-function-test-1.13a/status-only-dma-eased-dual-core-contract-20260619.log`
- `docs/validation/voice-keyboard-firmware-full-function-test-1.13a/human-ai-da-dada-1950-summary.md`

Those artifacts record the accepted status-strip-only DMA design and human checks with `LED5/6 follow reports: 0`, `Irregular flicker reports: 0`, and `Brightness problem reports: 0`.

## Final Fix Direction

The status rail keeps DMA for active/high-level status effects, but low-power latch frames avoid the DMA path:

- Active status rail: `rmt_tx_dma_strategy=status_strip_dma_full_frame_buffer`
- Active expected DMA: `rmt_tx_dma_requested=status:1,ec11:0,key:0,edge:0`
- Active expected actual: `rmt_tx_dma_actual=status:1,ec11:0,key:0,edge:0`
- Low-power transition/final latch: `low_power_status_tx=non_dma_clear_and_final_frame`
- Low-power final latch repeats: `low_power_final_latch_writes=3`
- Idle drive: `rmt_idle_drive=active_status_dma_low_power_non_dma_final_frame_then_immediate_all_quiet_suspend`

Implementation note: the backend can temporarily reconfigure the status strip transport for a non-DMA transmit. The low-power transition clear keeps the transport alive until the final PWR-only frame is latched; the final PWR-only frame is then sent three times before the quiet transport suspend. The next ordinary active status frame restores the requested DMA transport before sending advanced REC/AI frames.

Rejected implementation: do not make every transition clear frame non-DMA. In code, `status_force_non_dma` must be scoped to `low_power_active`, not `force_clear_tx || low_power_active`. Normal preview/effect switching uses transition clear frames too; sending those through the non-DMA path causes repeated status transport reconfiguration, produced `rmt_tx_wait_all_done(593): flush timeout` during the 2026-06-23 idle-preview switch test, and risks reintroducing visible status LED flashes. Active preview/effect switching must stay on the status DMA transport from the first active frame.

## Validation Evidence

The first no-DMA experiment proved the idle all-on issue stopped, but it is superseded because it risked regressing the historical advanced-effect no-flicker contract.

No-DMA experiment artifacts:

- `.cache\validation\postflash-status-rmt-nodma-20260623-132242\serial-status.txt`
- `.cache\validation\idle-status-rmt-nodma-wait-20260623-132301\serial-idle.txt`
- `.cache\validation\passive-idle-led-soak-20260623-134252\passive-serial.txt`
- `.cache\validation\idle-preview-switch-soak-20260623-134741\serial-preview-switch.txt`
- `.cache\validation\final-idle-after-restore-20260623-135000\serial-final-idle.txt`

The final accepted direction must be validated with both:

- idle transition evidence that physical `LED3`-`LED6` do not latch on;
- advanced REC/AI preview evidence that status effects do not flicker and do not make `LED5`/`LED6` follow.

Current narrowed-DMA validation artifacts:

- `.cache\validation\postflash-active-dma-idle-nondma-20260623-140927\serial-status.txt`: active boot contract reported `rmt_tx_dma_actual=status:1`.
- `.cache\validation\idle-active-dma-idle-nondma-20260623-141110\serial-idle.txt`: plugged idle entered `DISCONNECTED_IDLE`, reported `low_power_disabled=1`, `status_rgb=PWR:6,6,6;BLE:0,0,0;REC:0,0,0;AI:0,0,0;OK:0,0,0;WARN:0,0,0`, and `last_reason=low_power_off`.
- `.cache\validation\idle-preview-switch-active-dma-idle-nondma-20260623-141241\serial-preview-switch.txt`: rejected wider non-DMA scope; ordinary preview switching logged `rmt_dma=0` reconfiguration and `flush timeout`.
- `.cache\validation\active-preview-switch-dma-only-20260623-141544\serial-active-preview-switch.txt`: after narrowing non-DMA to `low_power_active`, ordinary active preview switching had `flush timeout|rmt_dma=0|transport ready` count `0` and five status queries with `rmt_tx_dma_actual=status:1`.

2026-06-23 follow-up from live low-power acceptance: the operator still saw physical status `LED3`-`LED6` latch on during idle entry after the first active-DMA/idle-non-DMA fix. The firmware-side mitigation is to avoid suspending the RMT transports immediately after the low-power zero-clear frame, keep the clear-to-final gap on the same low-power transition, repeat the final PWR-only latch three times, and only then suspend all transports. This preserves the active DMA contract while reducing the chance that the clear/final reconfiguration edge becomes a visible all-on latch.

The same follow-up adjusted idle PWR visibility: low-power single-channel PWR colors use 12%, while external-power white PWR uses 4% per RGB channel so white has the same approximate total drive as a single green/orange channel at 12%.

The long-press shutdown cue was also checked during this follow-up. Pending shutdown keeps the amber PWR plus full amber EC11 ring cue. Final hardware-shutdown confirmation remains PWR-only, but its visible window is 1200 ms so it is not missed by human observation while still avoiding the full-ring/all-on appearance during automatic shutdown.

Key feedback follow-up: local key press/release feedback stays white, while confirmed gesture feedback is purple. The intended visible language is press detected = white acknowledgement, single click = one purple flash, double click = two purple flashes, and long press = longer purple confirmation. Low-power raw key wake uses the same white press cue, then the confirmed gesture remains purple once debounce/gesture classification completes. The confirmed gesture window owns the key LED for its full duration so the white release tail cannot reappear between purple flashes or immediately after a single-click confirmation.

## Guardrail

Future changes must not choose between these two requirements. Keep both:

- active status effects use the status-only DMA path;
- low-power clear/final latch frames use the non-DMA path before all LED transports are suspended and data GPIOs are driven low.
- normal active preview/effect transition clear frames remain DMA, even when they are clearing stale REC/AI/OK/WARN pixels.
- low-power transition clear frames must not suspend transports before the repeated final PWR-only latch.
