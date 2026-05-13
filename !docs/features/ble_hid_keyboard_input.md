# BLE HID 键盘输入验证链路

## 使用状态

- 当前状态：`可用`
- 当前定位：`保底输入链路 / 调试验证链路`
- 当前是否主线：`不是语音主线`

说明：

- 这条链路当前已经稳定可用，适合作为设备 bring-up、主机联调、输入回路验证和 fallback 输入方式
- 但对“类似闪电说”的最终目标来说，它不是中文输出的主方案
- 后续如果进入真正的中文语音输入主线，`Windows` 主机伴随程序的 Unicode 注入能力会比这条 `BLE HID` ASCII / 键码链路更重要

## 目标

在当前 `ESP32-S3` 板子上，建立一条不依赖人工 `idf.py monitor` 输入的 BLE HID 键盘验证链路，让 AI 可以直接完成：

- `build`
- `flash`
- 抓启动日志
- 注入测试输入
- 验证 Windows 主机是否真正收到键盘字符
- 验证设备 `reset` 后是否自动恢复 BLE HID 连接

## 当前状态

当前链路已经在 `2026-05-13` 实机验证通过。

数据路径为：

`PowerShell 脚本 -> COM3 -> USB Serial/JTAG -> NimBLE BLE HID -> Windows 主机收到键盘输入`

当前关键结论：

- BLE HID host 已从 `Bluedroid` 迁移到 `NimBLE`
- `Listener Keyboard` 已建立 `NimBLE` bond，固件端启动日志显示 `NimBLE bonded peers=1`
- 设备 `reset` 后无需人工点击 Windows 蓝牙界面，约数秒内可自动恢复加密连接并重新完成 HID 订阅
- `verify_ble_hid.ps1` 已确认固件侧输入链路、BLE 连接状态和 HID report 发送完成
- `verify_ble_hid_end_to_end.ps1` 已确认 Windows 主机实际收到测试文本 `ab`

## 关键代码位置

- 输入读取与 BLE HID 任务：
  [ports/esp32/ble_hid/ble_hid.c](../../ports/esp32/ble_hid/ble_hid.c)
- BLE GAP、NimBLE 广告、配对、bond 与回连日志：
  [ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c](../../ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c)
- ASCII 到 HID 键盘 report 转换：
  [components/hid_keyboard/hid_keyboard.c](../../components/hid_keyboard/hid_keyboard.c)
- 板级帮助信息：
  [components/board/board.c](../../components/board/board.c)

## 关键配置

当前 `ESP32-S3` 默认配置已经切到 `NimBLE`：

- `CONFIG_BT_BLUEDROID_ENABLED` 关闭
- `CONFIG_BT_NIMBLE_ENABLED=y`
- `CONFIG_BT_NIMBLE_HID_SERVICE=y`
- `CONFIG_BT_NIMBLE_NVS_PERSIST=y`
- `CONFIG_BT_NIMBLE_SM_LVL=2`

迁移后的广告与安全策略：

- 主广告包收敛为 `flags + appearance + 16-bit HID UUID + Listener Keyboard`
- 不再使用旧 `Bluedroid` 路径中的 `128-bit HID UUID` 广告组合
- 启动日志不再出现 `BTM_BleWriteAdvData, Partial data write into ADV`
- 安全参数为 `bonding=1`、`mitm=0`、`sc=1`、`io_cap=NO_IO`
- bond key 通过 `CONFIG_BT_NIMBLE_NVS_PERSIST=y` 保存到 NVS，以支持 reset 后自动恢复加密连接

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

### 1. reset 后自动回连日志验证

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\capture_serial.ps1 -Port COM3 -DurationSeconds 70 -ResetBeforeRead
```

预期结果：

- 日志出现 `NimBLE advertising configured`
- 日志出现 `NimBLE advertising started`
- 日志出现 `NimBLE bonded peers=1`
- 日志出现 `connection established; status=0`
- 日志出现 `encryption change event; status=0`
- 日志出现 `subscribe event`
- 日志不再出现 `BTM_BleWriteAdvData, Partial data write into ADV`

`2026-05-13` 实测结果：

- reset 后约 `2.7` 秒出现 `connection established; status=0`
- 同次日志包含 `ble_hid: CONNECT`、`encryption change event; status=0` 和多个 `subscribe event`
- 中途一次 `disconnect; reason=546` 后设备重新广告，并在约 `2.3` 秒后再次自动连接

### 2. 固件侧快速验证

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify_ble_hid.ps1 -Port COM3 -Text "ab" -BootCaptureSeconds 10 -PostSendCaptureSeconds 5
```

预期结果：

- 脚本输出 `verify_ble_hid: boot ok, script input consumed, HID report dispatch completed`
- 日志出现 `ble_hid: START`
- 日志出现 `ble_hid: USB SERIAL INPUT READY`
- 日志出现 `ble_hid: SCRIPT RX`
- 日志出现 `hid_keyboard: send_ascii done`
- HID 发送日志显示 `connected=yes`

`2026-05-13` 实测结果：

- `ab` 两个字符均被固件消费
- 两个字符均出现 `hid_keyboard: send_ascii done`
- `notify_tx event` 返回 `status=0`

### 3. 主机侧端到端验证

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify_ble_hid_end_to_end.ps1 -Port COM3 -Text "ab" -BootCaptureSeconds 10 -PostSendCaptureSeconds 5 -HostCaptureTimeoutSeconds 15
```

预期结果：

- 返回 `verify_ble_hid_end_to_end: host received expected text and firmware logs show HID send completion`
- 主机侧实际捕获结果为 `ab`

`2026-05-13` 实测结果：

- 脚本成功返回
- `CAPTURED_TEXT` 为 `ab`
- 固件日志同时包含 `SCRIPT RX`、`connected=yes`、`notify_tx event status=0` 和 `hid_keyboard: send_ascii done`

## 迁移注意事项

从旧 `Bluedroid` 固件迁到 `NimBLE` 固件后，Windows 可能仍保留旧的 `Listener Keyboard` 配对缓存。这个缓存不等于新的 `NimBLE` bond。

如果迁移后看到以下现象：

- 固件日志显示 `NimBLE bonded peers=0`
- Windows UI 显示“已连接”，但固件没有 `connection established`
- `verify_ble_hid.ps1` 输出 `connected=no` 或 `Device Not Connected`

处理方式：

- 确认固件已经启用 `CONFIG_BT_NIMBLE_NVS_PERSIST=y`
- 在 Windows 蓝牙设置中删除旧的 `Listener Keyboard`
- 重新配对一次 `Listener Keyboard`
- 重新运行 reset 自动回连和端到端验证

完成这次重新配对后，后续 reset 不应再需要人工重连。

## 这次收掉的关键问题

- 不再依赖 `fgetc(stdin)` 作为测试输入入口
- 改成 `USB Serial/JTAG` 直接读取脚本注入字符
- 修正 BLE report-mode 键盘输入长度
- BLE HID host 从 `Bluedroid` 切换到 `NimBLE`
- 避免旧 `Bluedroid` 广告包被裁剪导致主机无法稳定识别或回连
- 增加连接、断开、订阅、加密和 bond 数量日志
- 启用 `NimBLE` bond NVS 持久化，解决 reset 后设备端丢 bond key 的核心问题
- 设置 Battery Service 占位电量为 `100%`，避免 Windows 弹出默认 `0%` 电量提醒

这里最关键的一点是：

- 当前键盘 report-mode 输入长度必须是 `7` 字节
- 不能按 `8` 字节发送

否则会出现：

- 固件日志显示 HID 已发送
- 但 Windows 主机侧静默收不到键盘输入

## 当前关键约束

- 当前验证板子为 `ESP32-S3`
- `2026-05-13` 这台机器当前枚举串口为 `COM3`
- 端到端验证前应重新确认当前枚举串口，而不是固定假设 `COM5`
- `idf.py monitor` 不是 AI 的主验证路径，非交互环境优先走脚本链路
- Windows 蓝牙 UI 的“已连接”状态不一定可靠，应以固件日志中的 `connection established`、`encryption change event; status=0`、`subscribe event` 和脚本端到端结果为准

## 已知限制

- 当前还是 BLE HID seed 验证链路，不是真实语音输入链路
- 当前输入源仍然是脚本模拟字符，不是物理按键，也不是麦克风采集
- Battery Service 当前没有接真实电量计，`100%` 是为了避免 Windows 误报的占位值
- `NimBLE` 迁移和 ESP-IDF 初始化细节集中在 `ports/esp32/`，未来迁移到 `STM32` 时应替换平台层实现，尽量保留 `components/hid_keyboard/` 的 HID report 转换逻辑

## 下一步建议

- 如果继续做真实产品功能，下一步应进入“语音输入占位链路”而不是继续扩 BLE demo
- 优先目标应是：
  `音频采集 -> 本地触发固定测试字符串 -> 复用当前 BLE HID 输出链路 -> 输出到 Windows`

## 目前不再单独使用的历史背景

- `Bluedroid` 路径已经被 `NimBLE` 替换，不再作为当前默认实现继续推进
- `GitHub` 上传与首次 `ESP-IDF` 环境恢复属于一次性 bring-up 背景，后续不再单独作为活跃方案维护
- 相关稳定结果已经并入本文档，不再保留对应的已完成 plan
