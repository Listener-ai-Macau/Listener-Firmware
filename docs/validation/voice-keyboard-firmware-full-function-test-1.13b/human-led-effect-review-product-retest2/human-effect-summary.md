# Status LED human effect review summary

- Result: HUMAN_REVIEW_INCOMPLETE
- Mode: Product
- Port: COM10
- Pass/Fail/Skip: 7 / 0 / 1
- LED5/6 follow reports: 0
- Irregular flicker reports: 0
- Brightness problem reports: 0
- Plan: C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware-pwr-shutdown-new-board\docs\validation\voice-keyboard-firmware-full-function-test-1.13b\human-led-effect-review-product-retest2\human-effect-plan.md
- Session JSONL: C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware-pwr-shutdown-new-board\docs\validation\voice-keyboard-firmware-full-function-test-1.13b\human-led-effect-review-product-retest2\human-effect-session.jsonl
- Serial log: C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware-pwr-shutdown-new-board\docs\validation\voice-keyboard-firmware-full-function-test-1.13b\human-led-effect-review-product-retest2\human-effect-serial.log

## Human Observations

| # | id | application timing | semantic lights | result | brightness | LED5/6 follow | flicker | unexpected | observed | notes |
|---:|---|---|---|---|---|---|---|---|---|---|
| 1 | scene-charging | USB/VBUS 在位、充电芯片显示正在充电且未确认满电；只由 PWR 表达外接电源/充电。 | PWR=插电充电；BLE、REC、AI、OK、WARN 都不用于表达充电。 | PASS | 无法判断 | False | False |  |  |  |
| 2 | scene-low-battery | 未插电且电量进入低电阈值；由 PWR 用 amber/red 系提示电源风险，不代表录音或 BLE 状态。 | PWR=低电量提示；WARN 不亮，除非进入真实错误/安全保护。 | PASS | 无法判断 | False | False |  |  |  |
| 3 | scene-ble-repairing | 用户双击 EC11 旋钮或发 ~VREC:RECOVERY 清除旧绑定并重新进入配对；这是用户主动重配确认，不是故障。 | BLE=用户主动重新配对确认；EC11=低亮蓝色同步闪烁确认；板框不参与；AI、OK、WARN 必须灭。 | SKIP | 无法判断 | False | False |  |  | 不要闪三下吧, 双击删两下会好一点 |
| 4 | scene-recording-processing-live | 设备正在录音并且桌面端已经发 VREC:PROCESSING:START；PWR/BLE 仍表达电源和连接，REC/AI 同时表达工作状态。 | PWR/BLE=基础在线状态；REC=正在录音且只做慢呼吸；AI=host-confirmed processing/OTA；OK/WARN 灭。 | PASS | 无法判断 | False | False |  |  |  |
| 5 | scene-ec11-short-press | 点击确定后马上短按一次 EC11 旋钮；这是用户输入反馈，不代表 OK 成功、错误或 BLE 状态。 | EC11=短按输入白色确认；OK/WARN 不参与；PWR/BLE 仍按自身状态显示。 | PASS | 无法判断 | False | False |  |  | 过了的, 不用再测 |
| 6 | scene-ec11-rotate | 点击确定后先顺时针旋转 EC11 一格，再逆时针旋转一格；这是音量/亮度等 HID 动作成功排队后的输入反馈。 | EC11=旋转输入方向性白色确认；OK/WARN 不参与；PWR/BLE 仍按自身状态显示。 | PASS | 无法判断 | False | False |  |  |  |
| 7 | scene-key-feedback | 点击确定后依次短按 KEY1、KEY2、KEY3、KEY4；这是本地按键输入反馈，不代表录音/处理/错误。 | KEY=本地按键短白色反馈；REC、AI、OK、WARN、EC11、板框不参与。 | PASS | 无法判断 | False | False |  |  |  |
| 8 | scene-sleep | 空闲超时、低功耗策略或硬件关机准备阶段；除非有唤醒/错误/充电状态，灯应进入明确的低功耗熄灭。 | 睡眠/低功耗=全灭；BLE、AI、OK、WARN 都不应残留。 | PASS | 无法判断 | False | False |  |  |  |
