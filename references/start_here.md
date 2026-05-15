# Voice Keyboard Firmware 快速入口

这是当前仓库的快速恢复入口，供 repo skill 和新接手的人优先阅读。

## 当前项目事实

- 当前固件主平台：`ESP32-S3`
- 当前产品主线：`BLE HID 键盘 + 语音采集上传`
- 当前音频上传主链路：`BLE session notify -> Windows 主机重组 -> wav`
- 当前上层约束：保持 `components/` / `protocols/` 尽量不绑死 `ESP-IDF`，为未来 `STM32` 迁移保留边界

## 新机器 / 环境入口

- 缺少 `ESP-IDF` 或首次在 Windows 上拉起环境时，先运行：
  `powershell -ExecutionPolicy Bypass -File .\tools\setup_windows.ps1`

## 当前推荐阅读顺序

1. [CLAUDE.md](../../CLAUDE.md)
2. [README.md](../../README.md)
3. [!docs/README.md](../../!docs/README.md)
4. 涉及语音上传时看 [!docs/features/audio_capture_ble_upload.md](../../!docs/features/audio_capture_ble_upload.md)
5. 涉及产品范围时看 [!docs/product_solutions.md](../../!docs/product_solutions.md)
6. 问“下一步”前先看 `!docs/plans/` 和 `!docs/fixes/` 是否已有 active plan

## 当前目录边界

- `main/`：只放薄的 `app_main()` 和初始化调度
- `components/`：跨平台产品逻辑
- `protocols/`：协议、消息结构、编解码
- `drivers/`：语义化外设驱动
- `ports/esp32/`：`ESP-IDF` 绑定
- `ports/stm32/`：未来平台预留

不要新建泛化顶层目录，例如 `services/`、`platform/`、`common/`、`misc/`。

## 当前常用命令

```powershell
# 构建
idf.py build

# 烧录
powershell -ExecutionPolicy Bypass -File .\tools\flash.ps1 -Port COM3

# 非交互式串口抓取
powershell -ExecutionPolicy Bypass -File .\tools\capture_serial.ps1 -Port COM3 -ResetBeforeRead

# 直接抓一段 BLE 录音
python .\tools\capture_audio_ble_wav.py --port COM3 --capture-seconds 5

# 物理 KEY1 模式抓音
python .\tools\capture_audio_ble_wav.py --port COM3 --capture-seconds 5 --trigger-mode physical-key --no-reset-before-capture

# 标准端到端回归 R1
python .\tools\verify_audio_ble_upload_end_to_end.py --port COM3 --capture-seconds 5 --no-reset-before-capture
```

## 当前关键约束

- 不要把当前 `audio_data` 语义改回旧的 `chunk + fragment`
- 不要改主机侧 `CCCD notify -> ValueChanged` 的主订阅顺序
- 不要删设备端 `subscribe 早于 connect` 的兼容
- 文档正文默认中文
- 有 active plan / fix 时，先按 plan 回答和推进

## 当前文档入口

- 功能总结：`!docs/features/`
- 活跃计划：`!docs/plans/`
- 活跃修复：`!docs/fixes/`
- 产品边界：`!docs/product_solutions.md`
