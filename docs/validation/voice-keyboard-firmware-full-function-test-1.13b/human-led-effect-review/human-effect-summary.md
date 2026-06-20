# Status LED human effect review summary

- Result: HUMAN_REVIEW_FAIL
- Mode: Full
- Port: COM10
- Pass/Fail/Skip: 33 / 3 / 1
- LED5/6 follow reports: 0
- Irregular flicker reports: 0
- Brightness problem reports: 0
- Plan: C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware-pwr-shutdown-new-board\docs\validation\voice-keyboard-firmware-full-function-test-1.13b\human-led-effect-review\human-effect-plan.md
- Session JSONL: C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware-pwr-shutdown-new-board\docs\validation\voice-keyboard-firmware-full-function-test-1.13b\human-led-effect-review\human-effect-session.jsonl
- Serial log: C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware-pwr-shutdown-new-board\docs\validation\voice-keyboard-firmware-full-function-test-1.13b\human-led-effect-review\human-effect-serial.log

## Human Observations

| # | id | application timing | semantic lights | result | brightness | LED5/6 follow | flicker | unexpected | observed | notes |
|---:|---|---|---|---|---|---|---|---|---|---|
| 1 | foundation-off |  | 基础/诊断步骤：按本步骤预期判断，不代表产品常态语义。 | PASS | 无法判断 | False | False |  |  |  |
| 2 | status-led1-white-35 |  | 基础/诊断步骤：按本步骤预期判断，不代表产品常态语义。 | PASS | 无法判断 | False | False |  |  |  |
| 3 | status-led2-blue-35 |  | 基础/诊断步骤：按本步骤预期判断，不代表产品常态语义。 | PASS | 无法判断 | False | False |  |  |  |
| 4 | status-led3-white-10 |  | 基础/诊断步骤：按本步骤预期判断，不代表产品常态语义。 | PASS | 无法判断 | False | False |  |  |  |
| 5 | status-led3-white-35 |  | 基础/诊断步骤：按本步骤预期判断，不代表产品常态语义。 | PASS | 无法判断 | False | False |  |  |  |
| 6 | status-led3-white-70 |  | 基础/诊断步骤：按本步骤预期判断，不代表产品常态语义。 | PASS | 无法判断 | False | False |  |  | 为什么测试白色 |
| 7 | status-led4-blue-35 |  | 基础/诊断步骤：按本步骤预期判断，不代表产品常态语义。 | PASS | 无法判断 | False | False |  |  |  |
| 8 | status-led5-green-35 |  | 基础/诊断步骤：按本步骤预期判断，不代表产品常态语义。 | PASS | 无法判断 | False | False |  |  |  |
| 9 | status-led6-red-35 |  | 基础/诊断步骤：按本步骤预期判断，不代表产品常态语义。 | PASS | 无法判断 | False | False |  |  |  |
| 10 | recording-processing-tail |  | 基础/诊断步骤：按本步骤预期判断，不代表产品常态语义。 | PASS | 无法判断 | False | False |  |  |  |
| 11 | key-led11-white-35 |  | 基础/诊断步骤：按本步骤预期判断，不代表产品常态语义。 | PASS | 无法判断 | False | False |  |  |  |
| 12 | ec11-led7-white-35 |  | 基础/诊断步骤：按本步骤预期判断，不代表产品常态语义。 | PASS | 无法判断 | False | False |  |  | 为啥要测试这些静态的, 不用再测试静态, 直接测试场景 |
| 13 | edge-led17-white-35 |  | 基础/诊断步骤：按本步骤预期判断，不代表产品常态语义。 | PASS | 无法判断 | False | False |  |  |  |
| 14 | foundation-final-off |  | 基础/诊断步骤：按本步骤预期判断，不代表产品常态语义。 | PASS | 无法判断 | False | False |  |  |  |
| 15 | scene-ready | 设备已开机且 BLE 已连接，当前没有录音、AI 处理、错误或成功确认；PWR/BLE 用于告诉用户设备在线但不要抢注意力。 | PWR=电源在线；BLE=已连接/就绪；REC、AI、OK、WARN 都应灭。 | PASS | 无法判断 | False | False |  |  | 这个就绪, 应该是正常电量加蓝牙连接正常吧, 和蓝牙用一个亮度, 另外需要遵从自定义亮度 |
| 16 | scene-pairing | 新设备首次开机、清除绑定完成、或没有可重连主机时进入 BLE 可发现配对窗口；只表达可以配对，不是错误。 | BLE=等待配对；旋钮/板框不参与；PWR 可独立表达插电/电源；REC、AI、OK、WARN 都不参与，WARN 不代表配对。 | PASS | 无法判断 | False | False |  |  |  |
| 17 | scene-reconnecting | 设备从睡眠/断链恢复，正在尝试找回已绑定主机；这是暂态连接状态，不应该亮错误灯。 | BLE=找回已绑定主机；旋钮/板框不参与；PWR 可独立表达插电/电源；WARN/错误灯不亮，因为重连不是错误。 | PASS | 无法判断 | False | False |  |  |  |
| 18 | scene-ble-repairing | 用户双击 EC11 旋钮或发 ~VREC:RECOVERY 清除旧绑定并重新进入配对；这是用户主动重配确认，不是故障。 | BLE=用户主动重新配对确认；EC11=低亮蓝色用户确认；板框不参与；AI、OK、WARN 必须灭。 | PASS | 无法判断 | False | False |  |  |  |
| 19 | scene-charging | USB/VBUS 在位、充电芯片显示正在充电且未确认满电；只由 PWR 表达外接电源/充电。 | PWR=插电充电；BLE、REC、AI、OK、WARN 都不用于表达充电。 | PASS | 无法判断 | False | False |  |  |  |
| 20 | scene-full | USB/VBUS 在位，充电完成信号和电量经过 debounce 后本轮插电锁定为满电；只由 PWR 表达满电。 | PWR=插电满电；OK 绿灯不亮，避免把满电误读为会话成功。 | PASS | 无法判断 | False | False |  |  |  |
| 21 | scene-low-battery | 未插电且电量进入低电阈值；由 PWR 用 amber/red 系提示电源风险，不代表录音或 BLE 状态。 | PWR=低电量提示；WARN 不亮，除非进入真实错误/安全保护。 | FAIL | 无法判断 | False | False |  |  | 这里为什么led3也跟着亮红灯? |
| 22 | scene-critical-battery | 未插电且电量进入严重低电阈值；这是电源安全提示，优先于普通氛围灯。 | PWR=严重低电提示；其它语义灯保持灭，避免和录音/BLE/错误混在一起。 | PASS | 无法判断 | False | False |  |  |  |
| 23 | scene-recording-processing-live | 设备正在录音并且桌面端已经发 VREC:PROCESSING:START；PWR/BLE 仍表达电源和连接，REC/AI 同时表达工作状态。 | PWR/BLE=基础在线状态；REC=正在录音且只做慢呼吸；AI=host-confirmed processing/OTA；OK/WARN 灭。 | PASS | 无法判断 | False | False |  |  |  |
| 24 | scene-processing-live | 桌面端进入 ASR/AI/OTA 处理阶段但当前没有本地录音；AI 灯只由 host-confirmed processing/OTA 开始触发。 | AI=host-confirmed processing/OTA；PWR/BLE 保持基线；旋钮/边框做顺时针紫色辅助；REC、OK、WARN 灭。 | PASS | 无法判断 | False | False |  |  |  |
| 25 | scene-ok | 本地录音停止/会话完成，或桌面端发 VREC:PROCESSING:DONE；只表示成功确认，持续约 2.0 秒。 | OK=成功短确认，只在录音/处理完成时出现；PWR/BLE 保持基线；WARN 灭。 | PASS | 无法判断 | False | False |  |  |  |
| 26 | scene-rec-not-available | 用户请求录音但当前没有可用录音源、权限/传输不满足或录音被拒绝；这是错误/警告语义，只用 WARN。 | WARN=录音不可用/被拒绝/权限或传输不满足；REC、AI、OK、旋钮、边框都不参与。 | PASS | 无法判断 | False | False |  |  |  |
| 27 | scene-shutdown-confirm | 用户长按 EC11 到达关机确认阈值但尚未真正断电；用于告诉用户继续按住会关机。 | PWR/旋钮=长按关机确认；板框不参与；BLE、REC、AI、OK、WARN 不抢占。 | PASS | 无法判断 | False | False |  |  |  |
| 28 | scene-ec11-short-press | 点击确定后马上短按一次 EC11 旋钮；这是用户输入反馈，不代表 OK 成功、错误或 BLE 状态。 | EC11=短按输入白色确认；OK/WARN 不参与；PWR/BLE 仍按自身状态显示。 | FAIL | 无法判断 | False | False |  |  | 没有任何灯效 |
| 29 | scene-ec11-rotate | 点击确定后先顺时针旋转 EC11 一格，再逆时针旋转一格；这是音量/亮度等 HID 动作成功排队后的输入反馈。 | EC11=旋转输入方向性白色确认；OK/WARN 不参与；PWR/BLE 仍按自身状态显示。 | FAIL | 无法判断 | False | False |  |  | 没有任何灯效 |
| 30 | scene-key-feedback | 点击确定后依次短按 KEY1、KEY2、KEY3、KEY4；这是本地按键输入反馈，不代表录音/处理/错误。 | KEY=本地按键短白色反馈；REC、AI、OK、WARN、EC11、板框不参与。 | PASS | 无法判断 | False | False |  |  | 应该是有灯效的, 我忘记按了, 旋钮那里搞错了 |
| 31 | scene-sleep | 空闲超时、低功耗策略或硬件关机准备阶段；除非有唤醒/错误/充电状态，灯应进入明确的低功耗熄灭。 | 睡眠/低功耗=全灭；BLE、AI、OK、WARN 都不应残留。 | PASS | 无法判断 | False | False |  |  |  |
| 32 | complex-recording-processing-product | 调校录音+AI 同时存在时的高级灯效，不让 PWR/BLE/按键参与；用于验收 REC/AI、旋钮底座、板框三者组合是否不闪、不串色。 | REC=录音暖金；AI=处理紫色语义；旋钮/边框保持低亮暖金慢速流动；OK/WARN 必须灭。 | SKIP | 无法判断 | False | False |  |  | 这个亮度要跟着type里面的自定义亮度走 |
| 33 | complex-recording-processing-status-only | 隔离检查状态灯 LED3/REC 与 LED4/AI 的动态本体，排除旋钮/板框/按键干扰，用于判断 LED5/6 跟闪根因。 | 按本步骤应用时机判断语义归属；未在预期中点名的灯都不应乱入。 | PASS | 无法判断 | False | False |  |  |  |
| 34 | complex-capture-only | 只调录音中的高级灯效；真实产品中对应用户按下录音后、AI 还未开始处理时的 REC/旋钮/板框表达。 | REC=录音高级灯效；旋钮/边框做暖金辅助；PWR/BLE/AI/OK/WARN/按键不参与。 | PASS | 无法判断 | False | False |  |  |  |
| 35 | complex-processing-only | 只调 AI/处理中的高级灯效；真实产品中对应 host-confirmed processing/OTA 阶段的 AI 紫色语义和旋钮/板框辅助。 | AI=处理/OTA 紫色语义；旋钮/边框做低亮紫色顺时针辅助；REC、OK、WARN、按键不参与。 | PASS | 无法判断 | False | False |  |  |  |
| 36 | complex-ai-status-only | 隔离检查 AI 单灯高级灯效；不让 PWR/BLE、REC、旋钮、按键、板框参与。 | 按本步骤应用时机判断语义归属；未在预期中点名的灯都不应乱入。 | PASS | 无法判断 | False | False |  |  |  |
| 37 | complex-shutdown-confirm | 调校长按关机确认的高级提示；真实产品中对应 EC11 长按超过确认阈值、尚未进入最终断电。 | PWR/旋钮=长按关机确认；板框不参与；BLE、REC、AI、OK、WARN 不抢占。 | PASS | 无法判断 | False | False |  |  | 可以了, 你看看还有什么地方是没有灯效的, 我觉得按键的灯效你可以再打磨一下, 现在有点没有高级灯效的感觉 |
