# Listener Firmware

<p align="center">
  <strong>Listener 语音键盘的开源固件。</strong><br/>
  按下，说话，灯告诉你进行到哪。文字由 Listener Type 写回光标。
</p>

<p align="center">
  <a href="README.md">English</a> ·
  <a href="docs/product/features.md">产品功能</a> ·
  <a href="https://github.com/Listener-ai-Macau/Listener-Type">Listener Type</a>
</p>

这不是 ESP32 示例工程。这是你买回去那台键盘上跑的软件：麦克风、EC11、四颗键、状态灯、BLE 音频、配对恢复、低功耗、OTA。

识别、润色、插入光标在桌面应用里，请一起看 [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type)。

## 为什么要买这台键盘

Type 可以先用电脑麦克风。键盘卖的是**手上的输入设备**：

- 旋钮单击开始/停止，不必去找快捷键
- REC / AI / BLE 灯，不用猜「到底在录没有」
- 唤醒词「开始录音」，可选声纹，减少误开
- 四颗键自定义：粘贴、复制、打开 Type 页面
- 固件开源，OTA 从 Type 里走，配对坏了双击旋钮就能重来

1.0.5 是可以日常用的设备基线。和 Type 装在一起，才是完整产品。

## 30 秒

1. 充电，按 EC11 开机。
2. 打开 Listener Type，开始配对，Windows 蓝牙选 `listener`。
3. 光标放进记事本，单击 EC11，说话，再单击。
4. 看 REC 灯和 Type 胶囊；字应出现在光标。

手册在 Type 仓库：[语音键盘手册](https://github.com/Listener-ai-Macau/Listener-Type/blob/master/docs/quickstart/voice-keyboard-readme.md)

## 固件负责什么

| 能力 | 你怎么用 |
| --- | --- |
| 录音键 | EC11 单击；动作可在 Type 里改 |
| 重新配对 | EC11 双击，再在 Windows 里选 `listener` |
| 灯 | PWR 电源，BLE 连接，REC 采音，AI 处理 |
| 唤醒 | 设备端等人说「开始录音」 |
| 升级 | Type 里 OTA，或本仓库 USB 刷写 |
| 省电 | 插电/电池低功耗和空闲关机，灭灯往往是休眠 |

GPIO、HID 兜底和验收脚本不写在产品介绍里，见 [固件功能地图](docs/features/firmware-feature-map.md)。

## 开源

欢迎阅读协议、灯效和音频路径，提 issue 和 PR。
刷到设备上的应是当前 1.0.5 构建，不要用仓库里的旧 OTA 当「最新」。

构建和刷写（Windows，ESP-IDF 由脚本加载）：

```powershell
pwsh -NoProfile -File .\tools\setup_windows.ps1
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
```

不要在未加载 IDF 环境的 shell 里直接跑 `idf.py`。

仓库：[Listener-ai-Macau/Listener-Firmware](https://github.com/Listener-ai-Macau/Listener-Firmware)
