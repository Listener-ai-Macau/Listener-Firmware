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

`PowerShell 脚本 -> COM5 -> USB Serial/JTAG -> BLE HID 字符发送 -> Windows 主机收到键盘输入`

这条链路已经实机验证通过。

## 关键代码位置

- 输入读取与 BLE HID 任务：
  [ports/esp32/ble_hid/ble_hid.c](C:/Users/imhlemsman/Desktop/listener/voice-keyboard-firmware/ports/esp32/ble_hid/ble_hid.c:1)
- ASCII 到 HID 键盘 report 转换：
  [components/hid_keyboard/hid_keyboard.c](C:/Users/imhlemsman/Desktop/listener/voice-keyboard-firmware/components/hid_keyboard/hid_keyboard.c:1)
- 板级帮助信息：
  [components/board/board.c](C:/Users/imhlemsman/Desktop/listener/voice-keyboard-firmware/components/board/board.c:1)

## 关键脚本

- 固件侧快速运行验证：
  [tools/verify_ble_hid.ps1](C:/Users/imhlemsman/Desktop/listener/voice-keyboard-firmware/tools/verify_ble_hid.ps1:1)
- 主机侧端到端验证：
  [tools/verify_ble_hid_end_to_end.ps1](C:/Users/imhlemsman/Desktop/listener/voice-keyboard-firmware/tools/verify_ble_hid_end_to_end.ps1:1)
- 发送脚本输入：
  [tools/send_serial.ps1](C:/Users/imhlemsman/Desktop/listener/voice-keyboard-firmware/tools/send_serial.ps1:1)
- 非交互抓串口日志：
  [tools/capture_serial.ps1](C:/Users/imhlemsman/Desktop/listener/voice-keyboard-firmware/tools/capture_serial.ps1:1)

## 推荐验证方式

### 1. 固件侧快速验证

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify_ble_hid.ps1 -Port COM5 -Text "ab"
```

预期结果：

- 日志出现 `ble_hid: START`
- 日志出现 `ble_hid: USB SERIAL INPUT READY`
- 日志出现 `ble_hid: SCRIPT RX`
- 日志出现 `hid_keyboard: send_ascii done`

### 2. 主机侧端到端验证

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify_ble_hid_end_to_end.ps1 -Port COM5 -Text "ab" -BootCaptureSeconds 8 -PostSendCaptureSeconds 4 -HostCaptureTimeoutSeconds 12
```

预期结果：

- 返回 `verify_ble_hid_end_to_end: host received expected text and firmware logs show HID send completion`
- 主机侧实际捕获结果为 `ab`

## 当前关键约束

- 当前验证板子为 `ESP32-S3`
- 当前串口为 `COM5`
- 当前主机已可配对并连接 `Listener Keyboard`
- `idf.py monitor` 不是 AI 的主验证路径，非交互环境优先走脚本链路

## 这次收掉的关键问题

- 不再依赖 `fgetc(stdin)` 作为测试输入入口
- 改成 `USB Serial/JTAG` 直接读取脚本注入字符
- 修正 BLE report-mode 键盘输入长度

这里最关键的一点是：

- 当前键盘 report-mode 输入长度必须是 `7` 字节
- 不能按 `8` 字节发送

否则会出现：

- 固件日志显示 HID 已发送
- 但 Windows 主机侧静默收不到键盘输入

## 已知限制

- 当前还是 BLE HID seed 验证链路，不是真实语音输入链路
- 当前输入源仍然是脚本模拟字符，不是物理按键，也不是麦克风采集
- 当前还有一个非阻塞 warning：
  `BTM_BleWriteAdvData, Partial data write into ADV`

## 下一步建议

- 如果继续做真实产品功能，下一步应进入“语音输入占位链路”而不是继续扩 BLE demo
- 优先目标应是：
  `音频采集 -> 本地触发固定测试字符串 -> 复用当前 BLE HID 输出链路 -> 输出到 Windows`
