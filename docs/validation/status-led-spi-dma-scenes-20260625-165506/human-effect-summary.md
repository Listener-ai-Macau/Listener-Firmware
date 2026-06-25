# Status LED human effect review summary

- Result: HUMAN_REVIEW_FAIL
- Mode: Scenes
- Port: COM10
- Pass/Fail/Skip: 15 / 2 / 0
- LED5/6 follow reports: 0
- Irregular flicker reports: 0
- Brightness problem reports: 0
- Plan: C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware\docs\validation\status-led-spi-dma-scenes-20260625-165506\human-effect-plan.md
- Session JSONL: C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware\docs\validation\status-led-spi-dma-scenes-20260625-165506\human-effect-session.jsonl
- Serial log: C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware\docs\validation\status-led-spi-dma-scenes-20260625-165506\human-effect-serial.log

## Human Observations

| # | id | application timing | semantic lights | result | brightness | LED5/6 follow | flicker | unexpected | observed | notes |
|---:|---|---|---|---|---|---|---|---|---|---|
| 1 | scene-ready | 设备已开机且 BLE 已连接，当前没有录音、AI 处理、错误或成功确认；PWR/BLE 用于告诉用户设备在线但不要抢注意力。 | PWR=电源在线；BLE=已连接/就绪；REC、AI、OK、WARN 都应灭。 | PASS | 无法判断 | False | False |  |  |  |
| 2 | scene-pairing | 新设备首次开机、清除绑定完成、或没有可重连主机时进入 BLE 可发现配对窗口；只表达可以配对，不是错误。 | BLE=等待配对；旋钮/板框不参与；PWR 可独立表达插电/电源；REC、AI、OK、WARN 都不参与，WARN 不代表配对。 | PASS | 无法判断 | False | False |  |  |  |
| 3 | scene-reconnecting | 设备从睡眠/断链恢复，正在尝试找回已绑定主机；这是暂态连接状态，不应该亮错误灯。 | BLE=找回已绑定主机；旋钮/板框不参与；PWR 可独立表达插电/电源；WARN/错误灯不亮，因为重连不是错误。 | PASS | 无法判断 | False | False |  |  |  |
| 4 | scene-ble-repairing | 用户双击 EC11 旋钮或发 ~VREC:RECOVERY 清除旧绑定并重新进入配对；这是用户主动重配确认，不是故障。 | BLE=用户主动重新配对确认；EC11=低亮蓝色同步闪烁确认；板框不参与；AI、OK、WARN 必须灭。 | FAIL | 无法判断 | False | False |  |  | 不对, 开始规则性全亮了, 之前不是修好了吗, 你改了状态等? |
| 5 | scene-charging | USB/VBUS 在位、充电芯片显示正在充电且未确认满电；只由 PWR 表达外接电源/充电。 | PWR=插电充电；BLE、REC、AI、OK、WARN 都不用于表达充电。 | PASS | 无法判断 | False | False |  |  |  |
| 6 | scene-full | USB/VBUS 在位，充电完成信号和电量经过 debounce 后本轮插电锁定为满电；只由 PWR 表达满电。 | PWR=插电满电；OK 绿灯不亮，避免把满电误读为会话成功。 | PASS | 无法判断 | False | False |  |  |  |
| 7 | scene-low-battery | 未插电且电量进入低电阈值；由 PWR 用 amber/red 系提示电源风险，不代表录音或 BLE 状态。 | PWR=低电量提示；WARN 不亮，除非进入真实错误/安全保护。 | PASS | 无法判断 | False | False |  |  |  |
| 8 | scene-critical-battery | 未插电且电量进入严重低电阈值；这是电源安全提示，优先于普通氛围灯。 | PWR=严重低电提示；其它语义灯保持灭，避免和录音/BLE/错误混在一起。 | PASS | 无法判断 | False | False |  |  |  |
| 9 | scene-recording-processing-live | 设备正在录音并且桌面端已经发 VREC:PROCESSING:START；PWR/BLE 仍表达电源和连接，REC/AI 同时表达工作状态。 | PWR/BLE=基础在线状态；REC=正在录音且只做慢呼吸；AI=host-confirmed processing/OTA；OK/WARN 灭。 | PASS | 无法判断 | False | False |  |  |  |
| 10 | scene-processing-live | 桌面端进入 ASR/AI/OTA 处理阶段但当前没有本地录音；AI 灯只由 host-confirmed processing/OTA 开始触发。 | AI=host-confirmed processing/OTA；PWR/BLE 保持基线；旋钮/边框做顺时针紫色辅助；REC、OK、WARN 灭。 | PASS | 无法判断 | False | False |  |  |  |
| 11 | scene-ok | 本地录音停止/会话完成，或桌面端发 VREC:PROCESSING:DONE；只表示成功确认，持续约 2.0 秒。 | OK=成功短确认，只在录音/处理完成时出现；PWR/BLE 保持基线；WARN 灭。 | PASS | 无法判断 | False | False |  |  |  |
| 12 | scene-rec-not-available | 用户请求录音但当前没有可用录音源、权限/传输不满足或录音被拒绝；这是错误/警告语义，只用 WARN。 | WARN=录音不可用/被拒绝/权限或传输不满足；REC、AI、OK、旋钮、边框都不参与。 | PASS | 无法判断 | False | False |  |  |  |
| 13 | scene-shutdown-confirm | 用户长按 EC11 到达关机确认阈值但尚未真正断电；用于告诉用户继续按住会关机。 | PWR/旋钮=长按关机确认；板框不参与；BLE、REC、AI、OK、WARN 不抢占。 | PASS | 无法判断 | False | False |  |  |  |
| 14 | scene-ec11-short-press | 点击确定后马上短按一次 EC11 旋钮；这是用户输入反馈，不代表 OK 成功、错误或 BLE 状态。 | EC11=短按输入白色确认；OK/WARN 不参与；PWR/BLE 仍按自身状态显示。 | FAIL | 无法判断 | False | False |  |  | 全亮了 |
| 15 | scene-ec11-rotate | 点击确定后先顺时针旋转 EC11 一格，再逆时针旋转一格；这是音量/亮度等 HID 动作成功排队后的输入反馈。 | EC11=旋转输入方向性白色确认；OK/WARN 不参与；PWR/BLE 仍按自身状态显示。 | PASS | 无法判断 | False | False |  |  |  |
| 16 | scene-key-feedback | 点击确定后依次短按 KEY1、KEY2、KEY3、KEY4；这是本地按键输入反馈，不代表录音/处理/错误。 | KEY=本地按键短白色反馈；REC、AI、OK、WARN、EC11、板框不参与。 | PASS | 无法判断 | False | False |  |  | 另外貌似状态灯会影响到旋钮灯, 应该是相互不影响才对 |
| 17 | scene-sleep | 空闲超时、低功耗策略或硬件关机准备阶段；除非有唤醒/错误/充电状态，灯应进入明确的低功耗熄灭。 | 睡眠/低功耗=全灭；BLE、AI、OK、WARN 都不应残留。 | PASS | 无法判断 | False | False |  |  |  |
