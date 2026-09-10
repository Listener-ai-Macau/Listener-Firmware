<h1 align="center">Listener Firmware</h1>

<p align="center">
  <strong>Listener 语音键盘里的开源固件。</strong><br/>
  按一下,说话,看灯——字出现在你的电脑上。
</p>

<p align="center">
  <a href="README.md">English</a> ·
  <strong>简体中文</strong> ·
  <a href="README.zh-TW.md">繁體中文</a>
</p>

<p align="center">
  <a href="https://github.com/Listener-ai-Macau/Listener-Firmware/releases"><img src="https://img.shields.io/github/v/release/Listener-ai-Macau/Listener-Firmware" alt="Release" /></a>
  <img src="https://img.shields.io/badge/ESP--IDF-v5.5-blue" alt="ESP-IDF v5.5" />
</p>

<p align="center">
  <a href="docs/product/features.md">产品功能</a> ·
  <a href="docs/release/1.0.5.md">发布说明</a> ·
  <a href="https://github.com/Listener-ai-Macau/Listener-Type">Listener Type</a>
</p>

<!-- 头图:有键盘照片后,放在这里。 -->

这是你买回去那台键盘里跑的软件,不是 ESP32 示例工程。ESP32-S3、PDM 麦克风、EC11 旋钮、四颗键、六盏状态灯、蓝牙音频、低功耗、OTA 升级,都在这个仓库里。

听见你、亮灯、把音频送出去,是这个仓库的事;把语音变成光标处的文字,是桌面软件 [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type) 的事。

## 里面的硬件

- ESP32-S3(16 MB 闪存,8 MB PSRAM)
- PDM 数字麦克风,16 kHz 采音
- EC11 旋转编码器、四颗热键、六灯状态条
- 锂电池,USB-C 充电

## 手上能做什么

- **单击旋钮** —— 开始 / 停止听写
- **双击** —— 忘掉当前配对,重新可被搜索
- **长按** —— 关机;**旋转** —— 调电脑音量(可在 Type 里改)
- **四颗键** —— 在 Type 里给单击、双击、长按各绑一个动作:粘贴、复制、打开应用
- **六盏灯** —— PWR 电源、BLE 连接、REC 采音、AI 处理、OK、WARN。进行到哪一步一眼看清;灯灭了多半是睡着了,不是坏了
- **唤醒词** —— 说一句「开始录音」就开工;声纹可在 Type 里录入,可选

## 30 秒出声

1. 充电,单击旋钮开机。
2. 打开 Listener Type 开始配对,在 Windows 蓝牙里选 `listener`。
3. 光标点进输入框,单击旋钮,说话,再单击。
4. REC 灯亮,Type 的胶囊有反应,字落在光标处。

手册在 Type 仓库:[语音键盘手册](https://github.com/Listener-ai-Macau/Listener-Type/blob/master/docs/quickstart/voice-keyboard-readme.md)

## 谁干什么

| 键盘(本仓库) | Listener Type(桌面软件) |
| --- | --- |
| 负责听见你:麦克风、键、灯、配对、电池、OTA | 负责替你写:转写、整理、插入光标 |
| 没有 Type,它是个守规矩的蓝牙设备 | 没有键盘,Type 用电脑麦克风照样工作 |

## 升级

在 Listener Type 里 OTA——推荐,设置都会保留;或者用本仓库的脚本走 USB 刷写。当前版本 **1.0.5**([发布说明](docs/release/1.0.5.md))。已知问题:刚关机后的电量读数可能不准。

## 构建和刷写

脚本会帮你在 Windows 上装好 ESP-IDF `release/v5.5`:

```powershell
pwsh -NoProfile -File .\tools\setup_windows.ps1
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
```

不要直接裸跑 `idf.py`——用 `tools\idf.ps1`,它会先加载 IDF 环境。

协议、灯效、音频链路全部公开:欢迎阅读、提 issue、发 PR。GPIO 表和调试脚本在[固件功能地图](docs/features/firmware-feature-map.md);提交代码前请读 [CONTRIBUTING.md](CONTRIBUTING.md),安全问题请走 [SECURITY.md](SECURITY.md)。
