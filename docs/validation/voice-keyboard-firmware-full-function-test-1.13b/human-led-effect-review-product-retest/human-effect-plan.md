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
- TailOnly mode is a narrow 1.9 confirmation: AI-only and REC+AI status-tail effects remain visibly but gently dynamic while LED5/OK and LED6/WARN stay physically and logically off.
- ComboOnly mode is a narrow 1.9 confirmation for the combined REC+AI, EC11, and edge/frame effect without PWR/BLE/key participation.
- Product direction for this pass: quiet but alive semantic status rail; blue BLE for connected/pairing/reconnect states, user re-pair uses BLE plus a low blue EC11 confirmation with edge/frame off, recording status reacts smoothly to volume, processing shows a purple da-dada thinking beat, EC11 press/rotate uses short white feedback, green OK only for success, amber/red WARN only for errors, and warm amber PWR+EC11 for shutdown confirmation.

## Review Steps

| # | id | application timing | semantic lights | expected | human focus | commands |
|---:|---|---|---|---|---|---|
| 1 | scene-charging | USB/VBUS 在位、充电芯片显示正在充电且未确认满电；只由 PWR 表达外接电源/充电。 | PWR=插电充电；BLE、REC、AI、OK、WARN 都不用于表达充电。 | PWR 应是白色、慢速、成熟的充电呼吸；亮度范围要看得出来但不能像警告闪烁；BLE、REC、AI、OK、WARN 不应乱入。 | 重点看充电呼吸是否舒服：最暗和最亮之间要有可感知差异，节奏要慢；如果人眼看到绿灯或蓝灯，请记录是哪一颗。 | `~LED:PREVIEW charging`<br>`WAIT 1700`<br>`~LED:STATUS`<br>`WAIT 900`<br>`~LED:STATUS` |
| 2 | scene-ble-repairing | 用户双击 EC11 旋钮或发 ~VREC:RECOVERY 清除旧绑定并重新进入配对；这是用户主动重配确认，不是故障。 | BLE=用户主动重新配对确认；EC11=低亮蓝色用户确认；板框不参与；AI、OK、WARN 必须灭。 | LED2/BLE 较明显蓝色双脉冲；EC11 旋钮有低亮蓝色用户重配确认；板框不参与；REC、AI、OK、WARN 必须灭。 | 确认这是用户主动重配：BLE 语义灯明确，旋钮有低亮确认，板框保持灭，不要误读为错误或录音。 | `~LED:PREVIEW repairing`<br>`WAIT 80`<br>`~LED:STATUS`<br>`WAIT 570`<br>`~LED:STATUS`<br>`WAIT 850`<br>`~LED:STATUS` |
| 3 | scene-recording-processing-live | 设备正在录音并且桌面端已经发 VREC:PROCESSING:START；PWR/BLE 仍表达电源和连接，REC/AI 同时表达工作状态。 | PWR/BLE=基础在线状态；REC=正在录音且只做慢呼吸；AI=host-confirmed processing/OTA；OK/WARN 灭。 | 真实产品预览：PWR/BLE 应保持独立可读，REC 暖金和 AI 紫色同时存在；旋钮/边框用低亮暖金底光加慢速低幅流动参与，不跟 PCM 变亮、不高频跳；LED5/OK 和 LED6/WARN 不应出现。 | 这一步重点复查 5/6 闪烁：状态 REC 可随音量变化，AI 固定紫色并只做哒  停顿  紧凑哒哒的思考节奏；旋钮/边框应有一点固定变化，OK/WARN 必须保持灭。 | `~LED:PREVIEW recording_processing`<br>`~LED:REC_LEVEL 75 60000`<br>`~LED:STATUS`<br>`~LED:STATUS`<br>`~LED:STATUS` |
| 4 | scene-ec11-short-press | 点击确定后马上短按一次 EC11 旋钮；这是用户输入反馈，不代表 OK 成功、错误或 BLE 状态。 | EC11=短按输入白色确认；OK/WARN 不参与；PWR/BLE 仍按自身状态显示。 | EC11 旋钮出现一次短白色确认；PWR/BLE 保持自己的状态；LED5/OK、LED6/WARN、板框和按键不应被点亮。 | 点确定后立刻短按旋钮一次，确认旋钮有干净短反馈，不能像成功 OK 或错误 WARN。 | `WAIT 2200`<br>`~LED:STATUS` |
| 5 | scene-ec11-rotate | 点击确定后先顺时针旋转 EC11 一格，再逆时针旋转一格；这是音量/亮度等 HID 动作成功排队后的输入反馈。 | EC11=旋转输入方向性白色确认；OK/WARN 不参与；PWR/BLE 仍按自身状态显示。 | EC11 旋钮出现短白色方向性反馈，顺/逆方向可区分；PWR/BLE 保持自己的状态；LED5/OK、LED6/WARN、板框和按键不应乱入。 | 点确定后马上顺时针、逆时针各转一下，确认有方向感但不抢眼，不能触发 OK/WARN。 | `WAIT 2600`<br>`~LED:STATUS` |
| 6 | scene-key-feedback | 点击确定后依次短按 KEY1、KEY2、KEY3、KEY4；这是本地按键输入反馈，不代表录音/处理/错误。 | KEY=本地按键短白色反馈；REC、AI、OK、WARN、EC11、板框不参与。 | 只有被按下的 KEY 灯有短白色反馈；PWR/BLE 保持自己的状态；REC、AI、OK、WARN、EC11、板框不应被错误点亮。 | 点确定后依次按四个按键，确认每颗按键反馈位置正确、时间短、不会带动 5/6 或旋钮/板框。 | `WAIT 3000`<br>`~LED:STATUS` |
| 7 | scene-sleep | 空闲超时、低功耗策略或硬件关机准备阶段；除非有唤醒/错误/充电状态，灯应进入明确的低功耗熄灭。 | 睡眠/低功耗=全灭；BLE、AI、OK、WARN 都不应残留。 | 所有状态灯、旋钮灯、按键灯、边框灯都应熄灭；不应残留蓝牙、AI、OK 或错误灯。 | 确认睡眠不是暗闪或随机残光，尤其确认 BLE/AI/OK/WARN 都灭。 | `~LED:PREVIEW sleep`<br>`~LED:STATUS` |
