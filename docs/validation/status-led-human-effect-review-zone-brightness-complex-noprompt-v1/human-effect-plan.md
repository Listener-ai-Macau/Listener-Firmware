# Status LED human effect review

This run is the foundation pass before more LED program changes. Human-eye observations are the source of truth for physical brightness, flicker, tail-follow, and zone independence.

## Desired Effects

- Foundation first: one commanded LED means one physical LED; all other LEDs stay off.
- Brightness must be monotonic by eye: 10% < 35% < 70%, with no saturation plateau at normal settings.
- During recording/processing, status LED3 and LED4 may use capped, slow, quantized brightness changes; LED5/OK and LED6/WARN must stay off unless success/error owns them.
- Volume mode drives the same recording-level renderer with ~LED:REC_LEVEL; this pass uses effect-only preview commands so PWR/BLE, live reconnects, and charge-state changes do not participate in the judged effect.
- EC11, key, and edge/frame LEDs are independent accent surfaces. For this pass, EC11 uses a knob-base warm-gold volume arc for REC and a slow violet orbit for AI-only; REC+AI overlap gives EC11/edge priority to recording warm gold with a slow same-color highlight instead of same-zone color mixing.
- Complex mode uses effect-only preview commands for REC/AI, EC11, and edge/frame validation; ordinary product previews remain in Scenes, not in the LED-effect tuning pass.
- RootCause mode temporarily lowers global brightness to test whether the visible 5/6 flicker is brightness/electrical-threshold sensitive, then restores brightness to 50%.
- StaticRoot mode compares fixed REC+AI output with and without a status query, separating dynamic-refresh flicker from static physical bleed or query/log interference.
- Repro mode intentionally drives the status rail and key LEDs with a known-bad broad dynamic pattern while keeping software OK/WARN at zero, so human observation can separate logical status from physical cross-zone disturbance.
- Product direction for this pass: quiet but alive semantic status rail; mature controller/speaker-style ring feedback on the EC11 base; restrained edge/frame support; green OK only for success; amber/red WARN only for errors; warm amber for shutdown confirmation.

## Review Steps

| # | id | expected | human focus | commands |
|---:|---|---|---|---|
| 1 | complex-recording-processing-product | 专用 effect-only 预览：状态灯只看 REC/AI，PWR/BLE 不参与；KEY1/KEY2 允许低亮工作态辅助；旋钮/边框由录音暖金优先，带同色慢速高光/扫动，不做同区金紫混色；LED5/OK 和 LED6/WARN 不应跟闪。 | 按灯效本身判断：按键辅助是否克制，旋钮底座环是否有稳定音量感且不是全静态，边框是否像环境支撑而不是乱闪；叠加态应该有同色高光运动感；~LED:STATUS 必须显示 preview_effect_only=1。 | `~LED:PREVIEW recording_processing_led_only`<br>`~LED:REC_LEVEL 100 60000`<br>`~LED:STATUS`<br>`~LED:STATUS`<br>`~LED:STATUS` |
| 2 | complex-recording-processing-status-only | 专用 effect-only 状态轨：只保留 LED3/REC 和 LED4/AI 的低频量化动态；PWR/BLE、旋钮和边框应熄灭；LED5/OK 和 LED6/WARN 不应跟闪。 | 这里验证动态状态轨本身：如果这里稳定，说明状态灯高级动态可用；如果仍闪，问题在状态灯物理链路、日志或刷新策略。 | `~LED:PREVIEW recording_processing_status_led_only`<br>`~LED:STATUS`<br>`~LED:STATUS`<br>`~LED:STATUS` |
| 3 | complex-capture-only | 专用 effect-only 预览：LED3 金色录音随音量和慢呼吸有克制亮度变化；KEY1 可有低亮暖金辅助；旋钮底座一圈应是暖金低亮底光加同色高光旋转，边框应是更低亮度的暖金框体支撑；不应靠缺几颗灯表达亮度；LED5/6 应保持熄灭。 | 看单独录音是否像旋钮底座氛围灯，按键辅助是否不抢眼，是否稳定不闪，旋钮不要像随机闪或缺灯；~LED:STATUS 必须显示 preview_effect_only=1。 | `~LED:PREVIEW capture_led_only`<br>`~LED:REC_LEVEL 75 60000`<br>`~LED:STATUS`<br>`~LED:STATUS`<br>`~LED:STATUS` |
| 4 | complex-processing-only | 专用 effect-only 预览：LED4 紫色处理可低频量化呼吸；KEY2 可有低亮紫色辅助；旋钮底座一圈应有低亮紫色底光和一个慢速环绕高光，边框应有更低亮度的紫色框体波动；LED5/6 应保持熄灭，且查询状态不应导致重启。 | 看单独处理是否有围绕旋钮的旋转感、按键辅助是否克制、边框是否好看且克制、是否稳定不闪；~LED:STATUS 必须显示 preview_effect_only=1。 | `~LED:PREVIEW processing_led_only`<br>`~LED:STATUS`<br>`~LED:STATUS`<br>`~LED:STATUS` |
| 5 | complex-shutdown-confirm | 暖琥珀 PWR + 旋钮进度/边框角落提示；应明显像关机确认，不像错误或录音。 | 确认长按关机确认灯效是否能被人眼理解。 | `~LED:PREVIEW shutdown_confirm`<br>`~LED:STATUS`<br>`~LED:STATUS`<br>`~LED:STATUS` |
