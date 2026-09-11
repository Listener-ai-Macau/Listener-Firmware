# Listener Firmware

这是 Listener 语音键盘上跑的固件。键盘是一台 ESP32-S3 桌面设备,带 PDM 麦克风、一颗能按能转的旋钮、四颗键和六盏状态灯;固件管收音、按键、灯、蓝牙配对、功耗和 OTA 升级。

把语音变成文字的所有环节——识别、改写、打进焦点框——都在电脑上的 [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type) 里。没有那个软件,键盘只是个守规矩的 BLE 外设;没有键盘,软件用任何麦克风都能跑。

[English](README.md) · [繁體中文](README.zh-TW.md) · [1.0.5 发布说明](docs/release/1.0.5.md)

### 按键

1.0.5 的默认行为:

- 单击旋钮开始/停止录音;双击重置蓝牙配对;长按关机;转动调电脑音量。这些都能在 Type 里改。
- KEY1–KEY4 可以绑单击、双击、长按动作(Type 的设置 → 设备)。没配置的键走无害的 HID 兜底,不会误打任何字。
- 在 Type 里打开「检测到人声后自动开始」,键盘会先等唤醒词(默认「开始录音」),听到才开录。

### 灯

PWR 是电源和电量;BLE 常亮蓝表示桌面端就绪;REC 亮表示确实在采音;AI 亮表示音频在传输或主机在处理;OK 是成功确认(或升级进行中);WARN 是有要处理的错误。灯全灭通常是设备休眠省电,不是坏了。

### 配对、恢复、升级

配对在软件里完成:点「开始配对」,在 Windows 蓝牙里选 `listener`,再点「检查连接」。配对卡死了去设置 → 关于 → 设备恢复,用不上串口线。

升级走软件里的 OTA,配对和设置都保留;用本仓库 USB 刷写也行。当前版本是 1.0.5([发布说明](docs/release/1.0.5.md)),已知 bug 是刚关机后电量读数可能不准。

### 构建和刷写

脚本只做了 Windows 版,会帮你装好 ESP-IDF `release/v5.5`:

```powershell
pwsh -NoProfile -File .\tools\setup_windows.ps1
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
```

不要裸跑 `idf.py`——走 `tools\idf.ps1`,它先加载 IDF 环境。

GPIO 表和其余工程细节在 [docs/features/firmware-feature-map.md](docs/features/firmware-feature-map.md)。贡献见 [CONTRIBUTING.md](CONTRIBUTING.md);安全问题发 [SECURITY.md](SECURITY.md)。
