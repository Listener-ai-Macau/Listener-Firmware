# Listener

## 放在桌边的 AI 入门小工具 · 49 美元

一个麦克风、一个录音旋钮、四颗可自定义按键。配合 Windows Type，用说话起草消息、邮件或笔记，文字进入正在使用的输入框。

**49 美元为硬件价格，Type 软件免费开源。**

| 用途 | 直接下载 |
| --- | --- |
| **Windows 软件** | **[下载 MSI · Type build221](https://github.com/Listener-ai-Macau/Listener-Type/releases/download/v1.0.6-rc1/ListenerType_1.0.6-rc1_x64_en-US.msi)** |
| **固件：USB 烧写 / 蓝牙 OTA** | **[下载固件 ZIP · 两种方式共用](https://github.com/Listener-ai-Macau/Listener-Firmware/releases/download/v1.0.6-rc1/ListenerFirmware_1.0.6-rc1_ESP32S3.zip)** |

<!-- listener-local-text-entry -->
**没有硬件或云 Key，也能先试：** [下载 Windows MSI](https://github.com/Listener-ai-Macau/Listener-Type/releases/download/v1.0.6-rc1/ListenerType_1.0.6-rc1_x64_en-US.msi)，打开「概览 → 本机中英翻译」，首次明确下载所需组件即可。文字翻译无逐次 API 费用；语音识别另行配置。[具体设置](https://github.com/Listener-ai-Macau/Listener-Type/blob/master/docs/USAGE.md)。

[English](README.md) · **简体中文** · [繁體中文](README.zh-TW.md)

<img src="docs/assets/readme/keyboard-front.jpg" alt="Listener 实物：四颗透明按键、黑色旋钮与六颗状态灯" width="720">

*Listener 实物照片。计划交付外观基本如图：透明外壳、透明键帽、黑色旋钮；此图不是操作录像。*

- **说出来：** 旋钮开停录音，电脑上的 Type 完成识别与写入。
- **按需要整理：** 默认保留原文，也可在配置文字服务后选择风格或翻译。
- **设成自己的：** 自定义四颗按键、灯光和电源设置，每台设备独立保存。

**[听一条消息变成文字 · 约25秒软件示例](https://github.com/Listener-ai-Macau/Listener-Type/blob/master/docs/VOICE_EXAMPLE.zh-CN.md)。** 预录合成人声与 Type 实际结果，未展示硬件操作；原音为英文。

**开始前需要：** Windows Type，以及已配置的识别服务或本地模型。云端用量单独计费；当前版本不附带开箱可用的官方云账号或识别额度。

**RC1 仍为候选版本。** 强干扰分离尚未完善，偶发提前结束仍在跟进，详见[当前限制与发布说明](https://github.com/Listener-ai-Macau/Listener-Type/releases/tag/v1.0.6-rc1)。硬件购买入口尚未公布。

[首次设置](https://github.com/Listener-ai-Macau/Listener-Type/blob/master/README.zh-CN.md#先完成一次输入) · [需要时再查使用指南](https://github.com/Listener-ai-Macau/Listener-Type/blob/master/docs/USAGE.md)

<details>
<summary>已有设备：固件、设置与本次版本</summary>

固件适用 **ESP32-S3 N16R8**，构建 `094f5a60049f59f4`。USB 和蓝牙更新选择同一个 ZIP，无需再猜下载哪一个。
Type 默认使用 **USB 有线刷机**，刷机能力已内置；客户无需另装 Python、ESP-IDF 或其它刷机软件。
设备设置各自独立：选择一台有线／蓝牙设备并读取；完成编辑后自动保存明确修改，读取或切换设备不会写入电脑默认值。名称、灯光、电源、按键和旋钮设置保存在所选设备中。请配套使用本页 MSI 与固件 ZIP。

Type build221 修复一种“预览已有尾句、结束成稿却少了”的问题：后续复核把同段返回为空时，程序会保留已有对应本人说话证据、已接纳且坐标对齐的识别段，同时继续接受正常非空纠错和执行旁人过滤。轻量翻译及已有保存稿保留，固件38不变。

[Release notes / 发布说明](https://github.com/Listener-ai-Macau/Listener-Type/releases/tag/v1.0.6-rc1) · [Firmware / 固件](https://github.com/Listener-ai-Macau/Listener-Firmware/releases/tag/v1.0.6-rc1)

</details>

---

## 从开机到第一句话

配套 Type 软件免费开源；云端识别或文字服务按提供方计费。没有云端密钥，可先用 Windows 本地模型与原文模式（Raw）试录；详见[软件配置说明](https://github.com/Listener-ai-Macau/Listener-Type/blob/master/README.zh-CN.md#软件免费服务费用分别计算)。

1. 先安装并打开 [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type/releases)，配置识别服务凭据或准备本地模型。
2. USB-C 充电，单击旋钮开机。
3. Type「设置 → 设备」开始配对，在 Windows 蓝牙选择 `listener`，返回软件检查连接。
4. 输入来源选 Listener BLE，放好光标；旋钮单击开始，说完再单击停止。

蓝牙“已配对”不等于 Type 已就绪。硬件完整音频路径当前以 Windows 为主。

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/assets/listener/keyboard-lineart-dark.png">
  <img src="docs/assets/listener/keyboard-lineart.png" alt="控件布局：四键、旋钮与六位状态区" width="720">
</picture>

## 读懂状态，操作就有把握

| 灯 | 颜色与节奏 |
| --- | --- |
| PWR | 电池绿 / 琥珀 / 红；极低电量红双闪。白色呼吸充电，常亮充满 |
| BLE | 蓝色脉冲：配对 / 重连；暗蓝双闪：Type 未就绪；蓝常亮：就绪 |
| REC | 金色随声音变化：录音 |
| AI | 紫色节拍：传输或处理 |
| OK | 绿色短亮：完成；OTA 显示青绿色进度 |
| WARN | 琥珀或红色：关注软件错误提示 |

[完整灯语](docs/features/status_led.zh-CN.md) · 不只看颜色，也看灯名与节奏。

## 默认动作与个人设置

| 控件 | 默认动作 |
| --- | --- |
| 旋钮单击 / 旋转 | 开始或停止录音 / 系统音量 |
| 旋钮双击 / 持续长按 | 重置蓝牙配对 / 等待确认完成后关机 |
| KEY1 / KEY2 / KEY3 / KEY4 | 右 Ctrl / Ctrl+C / Ctrl+V / Enter |

四键的双击和长按默认禁用；可在 Type 设备页映射听写、模板、快捷键与其他动作。状态区、按键、旋钮和板边亮度可分别调节。

同页可配置唤醒词与声纹。未录制声纹时，任何人说对唤醒词都可能触发；删除声纹不会关闭唤醒。启用唤醒时，REC 熄灭不代表麦克风完全关闭。

## 空闲时省电，需要时唤醒

电池默认空闲约 1 分钟低功耗、10 分钟关机；USB 默认约 3 分钟低功耗，不自动关机。低功耗保留电源提示，按键或旋钮可唤醒；真正关机后用旋钮开机。计时与亮度可调整。

## 更新与恢复

普通用户从发布页获取匹配的 OTA ZIP，在 Type 的设备页更新。保持供电与连接，完成重启后核对版本。正常 OTA 保留配对与设置；双击旋钮重配与工程全擦写是不同操作。

## 开发入口

这是 Listener 语音键盘的开源固件仓库。键盘通过蓝牙把声音交给 Listener Type，由电脑完成识别与文字输入；它需要配套软件。

| 目录 | 内容 |
| --- | --- |
| `components/` | 控制、灯光、电源、设置、OTA 与诊断逻辑 |
| `ports/esp32/` | 音频、BLE、存储与硬件适配 |
| `protocols/` | 设备协议与编解码 |
| `tools/` | 构建、刷写、监控、打包与验证 |

```powershell
pwsh -NoProfile -File .\tools\setup_windows.ps1
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
pwsh -NoProfile -File .\tools\monitor.ps1 -Port COMx
```

临时 ESP-IDF 命令使用 `tools/idf.ps1`。从[贡献指南](CONTRIBUTING.zh-CN.md)与[产品功能目录](docs/product/features.md)继续。

[安全](SECURITY.zh-CN.md)

[Apache-2.0](https://github.com/Listener-ai-Macau/Listener-Firmware/blob/master/LICENSE)
