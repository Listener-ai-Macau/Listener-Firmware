# Listener Firmware

这是运行在 Listener 语音键盘上的固件。它负责麦克风收音，通过蓝牙把音频送给 Listener Type，并让旋钮、按键、灯、电池和电源管理作为一台完整设备协同工作。

[English](README.md) · [繁體中文](README.zh-TW.md) · [固件发布](https://github.com/Listener-ai-Macau/Listener-Firmware/releases) · [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type)

<p align="center">
  <img src="docs/assets/readme/keyboard-front.jpg" alt="Listener 语音键盘" width="900" />
</p>

## 它在 Listener 里负责什么

语音键盘和桌面应用是同一个产品的两部分。固件负责采集和传送音频；[Listener Type](https://github.com/Listener-ai-Macau/Listener-Type) 负责识别语音、整理文字，再把结果插入当前光标。

两边的分工是明确的。固件管理设备时序、传输、控制和恢复；桌面应用判断唤醒、可选声纹、自动结束、采用哪份识别结果、写作风格和最终输出。

## 键盘里有哪些能力

- **收音和 BLE 音频。** PDM 麦克风以 16 kHz 采集，音频按会话分帧、排队和传输，并处理重试、背压、保留包重放和尾音保护。
- **旋钮和按键。** EC11 支持单击、双击、长按和旋转。KEY1–KEY4 支持可配置的单击、双击和长按；Type 不可用时还有安全的 BLE HID 兜底动作。
- **看得懂的反馈。** 六组灯显示电源、蓝牙、录音、处理、成功、警告、充电和固件升级进度。
- **电池和功耗。** 键盘通过 BLE 报告电量，处理充电和低电保护，空闲时暂停音频工作，可由实体控制唤醒，并支持可调节的休眠与关机时间。
- **设置和恢复。** BLE 名称、灯光亮度、旋钮动作和功耗时间会保存下来，正常重启和 OTA 不会清除。普通重连无法解决问题时，还可以重置配对或有线恢复。
- **诊断。** 保存在闪存中的事件日志重启后仍然存在，可以通过 BLE 或串口导出。工厂与工程工具覆盖硬件、音频、BLE、控制、电源、OTA 和诊断包。

更详细的实现和验证入口见[固件功能映射](docs/features/firmware-feature-map.md)。

## 日常使用

1. 打开键盘，在 Listener Type 的设置 → 设备中完成配对。
2. 单击旋钮开始听写，再按一次停止。说完以后，也可以由 Listener Type 自动结束。
3. REC 表示正在收音，AI 表示桌面应用正在处理，OK 表示结果已经完成。

旋转旋钮可以调节系统音量或屏幕亮度。双击会清除蓝牙配对并重新进入可发现状态，长按会关机。KEY1–KEY4 的动作在 Listener Type 中设置。

所有灯熄灭通常表示键盘进入了低功耗空闲，下一次受支持的控制操作会将它唤醒。

<p align="center">
  <img src="docs/assets/readme/device-settings.png" alt="Listener 设备设置" width="720" />
</p>

## 硬件

当前 V2 配置使用 ESP32-S3-WROOM-1-N16R8、16 MB 闪存和 8 MB Octal PSRAM。设备包含 PDM 麦克风、带按压的 EC11 旋钮、四颗额外按键、六组状态灯、锂电池、USB-C 充电和硬件电源保持电路。

BLE 服务包括音频传输、HID、设备设置、诊断、电量报告和 OTA。语音识别不在 ESP32-S3 上运行。

## 升级和恢复

普通用户通过 Listener Type 安装发布版 OTA ZIP。固件使用两个应用分区，会校验升级包、报告进度，并在新镜像无法确认健康启动时回滚。正常升级会保留配对和设备设置。

USB 刷机、全擦、串口维护和工厂包用于开发、生产或恢复。准确的发布文件和 SHA-256 保存在 [Releases](https://github.com/Listener-ai-Macau/Listener-Firmware/releases)。

## 构建和刷机

Windows 脚本会准备项目需要的 ESP-IDF 5.5 环境：

```powershell
pwsh -NoProfile -File .\tools\setup_windows.ps1
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
pwsh -NoProfile -File .\tools\monitor.ps1 -Port COMx
```

其他 ESP-IDF 命令请通过 `tools\idf.ps1` 运行。

可复用的产品逻辑主要放在 `components/`，ESP32 绑定放在 `ports/esp32/`，共享设备消息放在 `protocols/`，启动组装放在 `main/`，构建、刷机、打包、诊断和验证工具放在 `tools/`。

修改固件前请阅读 [CONTRIBUTING.md](CONTRIBUTING.md)，报告设备问题前请阅读 [SUPPORT.md](SUPPORT.md)，安全问题请按 [SECURITY.md](SECURITY.md) 私下提交。

当前仓库还没有 `LICENSE`，因此能看到源码不代表已经获得再分发或修改授权。
