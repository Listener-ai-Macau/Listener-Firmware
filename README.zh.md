# Listener Firmware

Listener 语音键盘的固件。键盘是一台 ESP32-S3 桌面小设备(16 MB 闪存、8 MB PSRAM),带 PDM 麦克风(16 kHz 采音)、一颗能按能转的 EC11 旋钮、四颗键、六盏状态灯,锂电池 USB-C 充电。固件管收音、按键、灯、蓝牙配对、省电和 OTA 升级;把语音变成文字,是电脑上 [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type) 的事。

[English](README.md) · [繁體中文](README.zh-TW.md) · [1.0.5 发布说明](docs/release/1.0.5.md)

## 从开机到第一段字

1. 充电,按一下旋钮开机。
2. 打开电脑上的 Listener Type,点「开始配对」,在 Windows 蓝牙里选 `listener`,回来点「检查连接」。
3. 光标点进记事本,单击旋钮,说话,再单击。

REC 灯亮就是真在收音;字归 Type 打进光标。详细手册在 Type 仓库:[语音键盘手册](https://github.com/Listener-ai-Macau/Listener-Type/blob/master/docs/quickstart/voice-keyboard-readme.md)。

## 手上的动作

- 单击旋钮开始 / 停止,长按关机,转动调电脑音量——都能在 Type 里改。
- 配对乱了,双击旋钮:重置蓝牙,重新可被搜索。
- KEY1–KEY4 的单击、双击、长按各绑一个动作(Type 的设置 → 设备)。没配置的键走无害的 HID 兜底,不会乱打字。
- 不想伸手:打开「检测到人声后自动开始」,设备先等唤醒词(默认「开始录音」),听到才开录。

灯是状态,不是装饰:PWR 是电源电量,BLE 常亮蓝表示桌面端就绪,REC 亮才表示真在采音,AI 亮是传输或处理中,OK 是成功或升级中,WARN 有要处理的错误。灯全灭通常是休眠省电,不是坏了。

## 升级

在 Listener Type 里 OTA 就行:双分区,失败自动回滚,配对和设备设置都保留。想刷自己改的固件,用本仓库脚本走 USB。当前版本 1.0.5,已知问题一个:刚关机后电量读数可能不准。

## 刷自己的固件

Windows 下三条命令,ESP-IDF `release/v5.5` 由脚本装好:

```powershell
pwsh -NoProfile -File .\tools\setup_windows.ps1
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
```

别裸跑 `idf.py`,走 `tools\idf.ps1`,它先加载 IDF 环境。

## 读代码

`components/` 是产品逻辑,刻意保持跨平台;ESP-IDF 的绑定收在 `ports/esp32/`;`protocols/` 定义编解码和音频协议;`tools/` 是构建、刷写、监控脚本。GPIO 表和验收工具写在 [docs/features/firmware-feature-map.md](docs/features/firmware-feature-map.md)。

贡献见 [CONTRIBUTING.md](CONTRIBUTING.md);安全问题发 [SECURITY.md](SECURITY.md)。
