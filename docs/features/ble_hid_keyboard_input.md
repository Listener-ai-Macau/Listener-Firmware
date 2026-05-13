# BLE HID 键盘输入验证链路

## 目标

在当前 `ESP32-S3` 板子上，建立一条不依赖人工 `monitor` 输入的 BLE HID 键盘验证链路，让 AI 可以直接完成：

- `build`
- `flash`
- 抓启动日志
- 注入测试输入
- 验证 Windows 主机是否真正收到键盘字符

## 当前实现

当前键盘输入验证链路为：

`PowerShell 脚本 -> 当前枚举串口 -> USB Serial/JTAG -> BLE HID 字符发送 -> Windows 主机收到键盘输入`

其中：

- 固件侧链路已在 `2026-05-13` 重新实机验证通过
- 主机侧端到端链路仍然依赖 Windows 已经和 `Listener Keyboard` 建立 BLE 连接

## 关键代码位置

- 输入读取与 BLE HID 任务：
  [ports/esp32/ble_hid/ble_hid.c](../../ports/esp32/ble_hid/ble_hid.c)
- BLE GAP、NimBLE 广告、配对与回连日志：
  [ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c](../../ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c)
- ASCII 到 HID 键盘 report 转换：
  [components/hid_keyboard/hid_keyboard.c](../../components/hid_keyboard/hid_keyboard.c)
- 板级帮助信息：
  [components/board/board.c](../../components/board/board.c)

## 关键脚本

- 固件侧快速运行验证：
  [tools/verify_ble_hid.ps1](../../tools/verify_ble_hid.ps1)
- 主机侧端到端验证：
  [tools/verify_ble_hid_end_to_end.ps1](../../tools/verify_ble_hid_end_to_end.ps1)
- 发送脚本输入：
  [tools/send_serial.ps1](../../tools/send_serial.ps1)
- 非交互抓串口日志：
  [tools/capture_serial.ps1](../../tools/capture_serial.ps1)

## 推荐验证方式

### 1. 固件侧快速验证

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify_ble_hid.ps1 -Port COM3 -Text "ab"
```

预期结果：

- 日志出现 `ble_hid: START`
- 日志出现 `ble_hid: USB SERIAL INPUT READY`
- 日志出现 `ble_hid: SCRIPT RX`
- 如果 Windows 主机已经连接 `Listener Keyboard`，日志出现 `hid_keyboard: send_ascii done`
- 如果脚本输出 `connected=no` 或 `Device Not Connected`，说明固件侧链路已通，但主机侧还未建立 BLE 连接

### 2. 主机侧端到端验证

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify_ble_hid_end_to_end.ps1 -Port COM3 -Text "ab" -BootCaptureSeconds 8 -PostSendCaptureSeconds 4 -HostCaptureTimeoutSeconds 12
```

预期结果：

- 返回 `verify_ble_hid_end_to_end: host received expected text and firmware logs show HID send completion`
- 主机侧实际捕获结果为 `ab`

执行前提：

- Windows 蓝牙设置中 `Listener Keyboard` 必须已经处于已连接状态
- 迁移到 `NimBLE` 后，如果 Windows 仍保留旧 `Bluedroid` 配对记录，需要先删除 `Listener Keyboard` 并重新配对一次
- 重新配对应发生在已启用 `CONFIG_BT_NIMBLE_NVS_PERSIST=y` 的固件刷写之后，否则设备端 reset 后仍无法保存 bond key

## 本次恢复结果

- `tools/setup_windows.ps1` 已完成，`ESP-IDF` 当前可正常激活
- `tools/build.ps1` 已通过
- `tools/flash.ps1 -Port COM3` 已通过
- 当前 BLE HID host 已从 `Bluedroid` 迁移到 `NimBLE`
- 默认配置已关闭 `Bluedroid`，开启 `CONFIG_BT_NIMBLE_ENABLED=y`、`CONFIG_BT_NIMBLE_HID_SERVICE=y`、`CONFIG_BT_NIMBLE_NVS_PERSIST=y` 和 `CONFIG_BT_NIMBLE_SM_LVL=2`
- `tools/capture_serial.ps1 -Port COM3 -ResetBeforeRead` 已确认 `ble_hid: START` 与 `ble_hid: USB SERIAL INPUT READY`
- `tools/verify_ble_hid.ps1 -Port COM3 -Text "ab"` 已确认脚本输入被固件消费
- 新固件首次验证仍显示 `NimBLE bonded peers=0`，说明 Windows 旧配对缓存仍需删除并重新配对一次
- 固件显式把 BLE Battery Service 设置为 `100%` 占位值，避免 Windows 把 NimBLE BAS 默认值 `0%` 误弹成低电量提醒

## 当前关键约束

- 当前验证板子为 `ESP32-S3`
- `2026-05-13` 这台机器当前枚举串口为 `COM3`
- 端到端验证前应重新确认当前枚举串口，而不是固定假设 `COM5`
- 当前主机如果存在迁移前的 `Listener Keyboard` 配对记录，应删除并重新配对，重新建立 `NimBLE` bond
- `idf.py monitor` 不是 AI 的主验证路径，非交互环境优先走脚本链路

## 这次收掉的关键问题

- 不再依赖 `fgetc(stdin)` 作为测试输入入口
- 改成 `USB Serial/JTAG` 直接读取脚本注入字符
- 修正 BLE report-mode 键盘输入长度
- BLE HID host 从 `Bluedroid` 切换到 `NimBLE`，避免旧广告包被裁剪的问题
- 主广告包收敛为 `flags + appearance + 16-bit HID UUID + Listener Keyboard`
- 增加连接、断开、订阅、加密与 bond 数量日志，便于判断 Windows 是否真的完成回连
- 启用 NimBLE bond NVS 持久化，解决 reset 后设备端丢 bond key 的核心问题
- 设置 Battery Service 占位电量为 `100%`，避免 Windows 弹出 `0%` 电量提醒

这里最关键的一点是：

- 当前键盘 report-mode 输入长度必须是 `7` 字节
- 不能按 `8` 字节发送

否则会出现：

- 固件日志显示 HID 已发送
- 但 Windows 主机侧静默收不到键盘输入

## 已知限制

- 当前还是 BLE HID seed 验证链路，不是真实语音输入链路
- 当前输入源仍然是脚本模拟字符，不是物理按键，也不是麦克风采集
- Battery Service 当前没有接真实电量计，`100%` 是为了避免 Windows 误报的占位值
- 从 `Bluedroid` 切到 `NimBLE` 后，Windows 旧配对缓存需要人工删除并重新配对一次；后续 reset 自动回连依赖这个新 bond

## 下一步建议

- 如果继续做真实产品功能，下一步应进入“语音输入占位链路”而不是继续扩 BLE demo
- 优先目标应是：
  `音频采集 -> 本地触发固定测试字符串 -> 复用当前 BLE HID 输出链路 -> 输出到 Windows`
