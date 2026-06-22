# Status LED human effect review

This run is the foundation pass before more LED program changes. Human-eye observations are the source of truth for physical brightness, flicker, tail-follow, and zone independence.

## Desired Effects

- Foundation first: one commanded LED means one physical LED; all other LEDs stay off.
- Brightness must be monotonic by eye: 10% < 35% < 70%, with no saturation plateau at normal settings.
- During recording/processing, status LED3 and LED4 may use capped, slow, quantized brightness changes; LED5/OK and LED6/WARN must stay off unless success/error owns them.
- Volume mode uses real capture preview and expects only status LED3 to follow the smoothed bounded live-audio envelope; ~LED:REC_LEVEL remains only for the overlap/high-level diagnostic step.
- EC11 and edge/frame LEDs are independent accent surfaces. For this pass, REC volume changes only the status REC LED through a bounded envelope, AI stays static purple and uses a brightness-only grouped da-dada thinking beat, REC+AI overlap keeps the warm-gold recording cue instead of same-zone color mixing, and physical EC11 input uses brief white knob feedback. Key LEDs stay off unless there is a real key event or a diagnostic stress step.
- Complex mode uses effect-only preview commands for REC/AI, EC11, and edge/frame validation; ordinary product previews remain in Scenes, not in the LED-effect tuning pass.
- RootCause mode temporarily lowers global brightness to test whether the visible 5/6 flicker is brightness/electrical-threshold sensitive, then restores brightness to 50%.
- StaticRoot mode compares fixed REC+AI output with and without a status query, separating dynamic-refresh flicker from static physical bleed or query/log interference.
- TailOnly and ComboOnly collect ~LED:STATUS after the human observation result, so serial status sampling and log output do not disturb the visible effect while the operator is watching.
- Repro mode intentionally drives the status rail and key LEDs with a known-bad broad dynamic pattern while keeping software OK/WARN at zero, so human observation can separate logical status from physical cross-zone disturbance.
- IdleTransition mode isolates idle-entry validation from the full REC/AI/EC11/edge combo effect: it uses status-only REC/AI setup states, then checks that connected idle keeps only PWR/BLE active and LED3-LED6 stay off after the transition.
- TailOnly mode is a narrow 1.9 confirmation: AI-only and REC+AI status-tail effects remain visibly but gently dynamic while LED5/OK and LED6/WARN stay physically and logically off.
- ComboOnly mode is a narrow 1.9 confirmation for the combined REC+AI, EC11, and edge/frame effect without PWR/BLE/key participation.
- Product direction for this pass: quiet but alive semantic status rail; blue BLE for connected/pairing/reconnect states, user re-pair uses BLE plus a synced low blue EC11 blink with edge/frame off, recording status reacts smoothly to volume, processing shows a purple da-dada thinking beat, EC11 press/rotate uses short white feedback, green OK only for success, amber/red WARN only for errors, and warm amber PWR+EC11 for shutdown confirmation.

## Review Steps

| # | id | application timing | semantic lights | expected | human focus | commands |
|---:|---|---|---|---|---|---|
| 1 | recording-active-keeps-pwr-ble | 隔离检查从 connected idle 进入录音状态；这是产品状态预览，不是 effect-only 调灯模式。 | 按本步骤应用时机判断语义归属；未在预期中点名的灯都不应乱入。 | 先只有 LED1/PWR 与 LED2/BLE 作为在线基线；进入 recording_active 后 LED3/REC 加入，LED1/PWR 和 LED2/BLE 必须继续亮，LED4/AI、LED5/OK、LED6/WARN 不应乱入。 | 重点看发送 recording_active 后第一秒：LED1 和 LED2 不能灭，LED3 可以亮；如果 LED1/2 灭掉或所有状态灯闪一下，点失败并写备注。 | `~LED:PREVIEW connected`<br>`~LED:STATUS`<br>`WAIT 500`<br>`~LED:PREVIEW recording_active`<br>`WAIT 900`<br>`~LED:STATUS`<br>`WAIT 1200`<br>`~LED:STATUS`<br>post-observation:<br>`~DIAGLOG:LAST:80:status_led`<br>`~LED:PREVIEW connected`<br>`~LED:STATUS` |
