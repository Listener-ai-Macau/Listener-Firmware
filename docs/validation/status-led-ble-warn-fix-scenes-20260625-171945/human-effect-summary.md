# Status LED human effect review summary

- Result: ABORTED_BY_OPERATOR
- Mode: Scenes
- Port: COM10
- Pass/Fail/Skip: 0 / 1 / 0
- LED5/6 follow reports: 0
- Irregular flicker reports: 0
- Brightness problem reports: 0
- Plan: C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware\docs\validation\status-led-ble-warn-fix-scenes-20260625-171945\human-effect-plan.md
- Session JSONL: C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware\docs\validation\status-led-ble-warn-fix-scenes-20260625-171945\human-effect-session.jsonl
- Serial log: C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware\docs\validation\status-led-ble-warn-fix-scenes-20260625-171945\human-effect-serial.log

## Human Observations

| # | id | application timing | semantic lights | result | brightness | LED5/6 follow | flicker | unexpected | observed | notes |
|---:|---|---|---|---|---|---|---|---|---|---|
| 1 | scene-ready | 设备已开机且 BLE 已连接，当前没有录音、AI 处理、错误或成功确认；PWR/BLE 用于告诉用户设备在线但不要抢注意力。 | PWR=电源在线；BLE=已连接/就绪；REC、AI、OK、WARN 都应灭。 | FAIL | 无法判断 | False | False |  | 为啥就绪是蓝牙灯双闪 |  |
| 2 | scene-pairing | 新设备首次开机、清除绑定完成、或没有可重连主机时进入 BLE 可发现配对窗口；只表达可以配对，不是错误。 | BLE=等待配对；旋钮/板框不参与；PWR 可独立表达插电/电源；REC、AI、OK、WARN 都不参与，WARN 不代表配对。 | ABORT | 无法判断 | False | False |  |  |  |
