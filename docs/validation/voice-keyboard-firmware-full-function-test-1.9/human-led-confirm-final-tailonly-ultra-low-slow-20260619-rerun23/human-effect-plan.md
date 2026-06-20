# Status LED human effect review

This run is the foundation pass before more LED program changes. Human-eye observations are the source of truth for physical brightness, flicker, tail-follow, and zone independence.

## Desired Effects

- Foundation first: one commanded LED means one physical LED; all other LEDs stay off.
- Brightness must be monotonic by eye: 10% < 35% < 70%, with no saturation plateau at normal settings.
- During recording/processing, status LED3 and LED4 may use capped, slow, quantized brightness changes; LED5/OK and LED6/WARN must stay off unless success/error owns them.
- Volume mode still sends ~LED:REC_LEVEL review commands, but current product rendering treats recording as a deterministic low-load effect instead of PCM-driven brightness; this pass uses effect-only preview commands so PWR/BLE, live reconnects, and charge-state changes do not participate in the judged effect.
- EC11 and edge/frame LEDs are independent accent surfaces. For this pass, REC uses a low-load warm-gold base with slow fixed flow, AI-only uses a violet orbit, REC+AI overlap keeps the warm-gold recording cue instead of same-zone color mixing, and physical EC11 input uses brief white knob feedback. Key LEDs stay off unless there is a real key event or a diagnostic stress step.
- Complex mode uses effect-only preview commands for REC/AI, EC11, and edge/frame validation; ordinary product previews remain in Scenes, not in the LED-effect tuning pass.
- RootCause mode temporarily lowers global brightness to test whether the visible 5/6 flicker is brightness/electrical-threshold sensitive, then restores brightness to 50%.
- StaticRoot mode compares fixed REC+AI output with and without a status query, separating dynamic-refresh flicker from static physical bleed or query/log interference.
- Repro mode intentionally drives the status rail and key LEDs with a known-bad broad dynamic pattern while keeping software OK/WARN at zero, so human observation can separate logical status from physical cross-zone disturbance.
- TailOnly mode is a narrow 1.9 confirmation: REC+AI status-tail remains subtly dynamic while LED5/OK and LED6/WARN stay physically and logically off.
- ComboOnly mode is a narrow 1.9 confirmation for the combined REC+AI, EC11, and edge/frame effect without PWR/BLE/key participation.
- Product direction for this pass: quiet but alive semantic status rail; blue BLE for connected/pairing/reconnect states, user re-pair uses BLE plus a low blue EC11 confirmation with edge/frame off, recording uses low-load warm-gold fixed flow, processing accents move clockwise, EC11 press/rotate uses short white feedback, green OK only for success, amber/red WARN only for errors, and warm amber PWR+EC11 for shutdown confirmation.

## Review Steps

| # | id | application timing | semantic lights | expected | human focus | commands |
|---:|---|---|---|---|---|---|
| 1 | tail-only-recording-processing-dynamic |  | 基础/诊断步骤：按本步骤预期判断，不代表产品常态语义。 | 专用 effect-only 状态轨：只有 LED3/REC 暖金和 LED4/AI 紫色/蓝紫做极低幅慢动态；PWR/BLE、旋钮、按键、边框都应熄灭；LED5/OK 和 LED6/WARN 在软件状态和肉眼观察中都必须保持熄灭。 | 重点看 LED3/4 是否有一点柔和慢变化且不抖，以及 LED5/6 是否不再跟着 3/4 闪同色、绿/红或随机闪。 | `~LED:BRIGHTNESS 100`<br>`~LED:PREVIEW recording_processing_status_led_only`<br>`~LED:STATUS`<br>`WAIT 1600`<br>`~LED:STATUS`<br>`WAIT 1600`<br>`~LED:STATUS` |
