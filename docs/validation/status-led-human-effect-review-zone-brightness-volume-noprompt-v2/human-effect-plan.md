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
| 1 | volume-capture-sweep | 专用 effect-only 预览：LED3/REC、KEY1 低亮暖金辅助、旋钮暖金音量弧、边框暖金侧轨应按 0/30/65/100 四档明显变亮、变长；高音量时有小范围同色高光顺滑移动；PWR/BLE 不参与；LED5/6 必须灭。 | 这是动态音量验收：点确定后马上盯着灯，每档约 2 秒；重点看旋钮/边框是否有可接受的音量跟随，按键辅助是否不抢眼，颜色是否是暖金而不是绿闪，高音量不能像完全静态或一开始跳闪；允许不是全环全框都亮，因为当前硬件上 broad 动态会触发 5/6；~LED:STATUS 必须显示 preview_effect_only=1。 | `~LED:PREVIEW capture_led_only`<br>`~LED:REC_LEVEL 0 2200`<br>`WAIT 1300`<br>`~LED:REC_LEVEL 30 2200`<br>`WAIT 1300`<br>`~LED:REC_LEVEL 65 2200`<br>`WAIT 1300`<br>`~LED:REC_LEVEL 100 60000`<br>`WAIT 2200`<br>`~LED:STATUS` |
| 2 | volume-overlap-high | 专用 effect-only 预览：REC 暖金高音量反馈和 AI 紫色语义可同时存在；PWR/BLE 不参与；KEY1/KEY2 允许低亮工作态辅助；状态 LED4 仍显示 AI 紫色，但旋钮/边框应由录音暖金优先，带同相位的小范围高光/扫动，不做同区金紫混色；LED5/6 必须灭，不应跟 LED3/4 闪。 | 这是最接近之前闪烁痛点的高级效果验收：重点看暖金是否稳定保持、开始阶段是否跳闪、按键辅助是否克制、板框是否是低亮支撑而不是乱闪；还要看是否有 5/6 跟闪或随机绿闪；~LED:STATUS 必须显示 preview_effect_only=1。 | `~LED:PREVIEW recording_processing_led_only`<br>`~LED:REC_LEVEL 100 60000`<br>`WAIT 1800`<br>`~LED:STATUS`<br>`WAIT 700`<br>`~LED:STATUS`<br>`WAIT 700`<br>`~LED:STATUS` |
