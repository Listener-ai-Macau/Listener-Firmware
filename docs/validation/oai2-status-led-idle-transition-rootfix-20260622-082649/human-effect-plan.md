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
| 1 | idle-transition-status-active-to-connected | 隔离检查从工作状态进入 connected idle；前置状态只允许状态灯 LED3/REC 和 LED4/AI 参与，不让旋钮、按键、边框作为干扰源。 | 按本步骤应用时机判断语义归属；未在预期中点名的灯都不应乱入。 | 切换前只有 LED3/REC 与 LED4/AI 可有低频动态；切到 connected idle 后只保留 PWR/BLE，LED3/4/5/6 不得全亮、规律闪或拖尾；旋钮、按键、边框应保持熄灭。 | 重点看发送 connected 后第一秒，以及随后约 8 秒；切换前的 REC/AI 动态不算 idle 复现，只有切到 idle 后 LED3-LED6 再亮才算失败。 | `~LED:PREVIEW recording_processing_status_led_only`<br>`~LED:REC_LEVEL 100 60000`<br>`WAIT 1800`<br>`~LED:PREVIEW connected`<br>`WAIT 1200`<br>post-observation:<br>`~LED:STATUS`<br>`~DIAGLOG:LAST:80:status_led`<br>`~POWER:STATUS` |
| 2 | idle-transition-ble-reconnect-to-connected | 隔离检查蓝牙重连闪烁进入 connected idle；这一步不经过 REC/AI 工作态。 | 按本步骤应用时机判断语义归属；未在预期中点名的灯都不应乱入。 | 重连阶段只允许 BLE 蓝色提示；切到 connected idle 后只保留 PWR/BLE，REC、AI、OK、WARN、旋钮、按键、边框不应被带亮。 | 重点看 BLE 从闪烁到常亮/低亮在线的第一秒，确认没有后四颗状态灯一起亮。 | `~LED:PREVIEW reconnecting`<br>`WAIT 1800`<br>`~LED:PREVIEW connected`<br>`WAIT 1200`<br>post-observation:<br>`~LED:STATUS`<br>`~DIAGLOG:LAST:80:status_led`<br>`~POWER:STATUS` |
| 3 | idle-transition-status-active-to-clear | 隔离检查工作状态收尾到全灭/clear；用于排除动态状态轨残帧。 | 按本步骤应用时机判断语义归属；未在预期中点名的灯都不应乱入。 | clear 后所有状态灯、旋钮、按键、边框都应熄灭；不应周期性回亮。 | 重点看 clear 后 1 秒和随后几秒，确认 LED3-LED6 没有再规律全亮。 | `~LED:PREVIEW recording_processing_status_led_only`<br>`~LED:REC_LEVEL 100 60000`<br>`WAIT 1200`<br>`~LED:PREVIEW clear`<br>`WAIT 1200`<br>post-observation:<br>`~LED:STATUS`<br>`~DIAGLOG:LAST:80:status_led`<br>`~POWER:STATUS` |
| 4 | idle-transition-settled-connected | 确认 idle 稳态本身，不再经过任何工作态。 | 按本步骤应用时机判断语义归属；未在预期中点名的灯都不应乱入。 | 8 秒内只保留 PWR/BLE 的 idle 指示；LED3-LED6、旋钮、按键、边框都应保持灭。 | 如果这一步还会周期性亮后四颗，问题就是 idle 周期刷新；如果通过，问题在进入 idle 的路径。 | `~LED:PREVIEW connected`<br>`WAIT 8000`<br>post-observation:<br>`~LED:STATUS`<br>`~DIAGLOG:LAST:80:status_led`<br>`~POWER:STATUS` |
