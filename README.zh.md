# Listener Firmware

Listener 语音键盘的固件——真正跑在设备上的那份代码。ESP32-S3、PDM 麦克风、一颗能按的旋钮、四颗键、六盏灯、蓝牙音频、OTA 升级。

键盘只管听和亮灯。把语音变成文字是电脑那边的事,归 [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type) 管。

[English](README.md) · [繁體中文](README.zh-TW.md) · [发布说明](docs/release/1.0.5.md)

## 键盘怎么用

- 单击旋钮:开始、停止听写。
- 双击:重新配对;长按:关机;转动:调电脑音量(都能在 Type 里改)。
- 四颗键随你绑,在 Type 里设置:粘贴、复制、打开应用,想绑什么绑什么。
- 灯会说话:电源、蓝牙、录音、处理各占一盏。全黑多半是睡着了,不是坏了。
- 懒得伸手也行:说一句「开始录音」,它自己开始。

第一次用:充电,按旋钮开机,在 Type 里发起配对,Windows 蓝牙里选 `listener`。完整步骤看[语音键盘手册](https://github.com/Listener-ai-Macau/Listener-Type/blob/master/docs/quickstart/voice-keyboard-readme.md)。

## 升级

最省事是在 Listener Type 里 OTA,设置都会保留。当前版本 1.0.5([发布说明](docs/release/1.0.5.md))。已知问题一个:刚关机后的电量读数可能不准。

## 自己构建刷机

脚本是 Windows 的,会帮你装好 ESP-IDF v5.5:

```powershell
pwsh -NoProfile -File .\tools\setup_windows.ps1
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
```

别直接跑 `idf.py`——用 `tools\idf.ps1`,它会先加载 IDF 环境。

有意思的部分都能读:音频链路、灯效逻辑、蓝牙协议。GPIO 细节在 [docs/features/firmware-feature-map.md](docs/features/firmware-feature-map.md)。欢迎报 bug、提 PR——先翻翻 [CONTRIBUTING.md](CONTRIBUTING.md);安全问题发 [SECURITY.md](SECURITY.md)。
