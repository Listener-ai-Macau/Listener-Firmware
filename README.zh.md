# Listener Firmware

Listener 语音键盘的固件。它运行在 ESP32-S3 桌面设备上，采集语音并通过蓝牙送给 Listener Type，把录音控制和产品状态放在手边。

[English](README.md) · [繁體中文](README.zh-TW.md) · [固件发布](https://github.com/Listener-ai-Macau/Listener-Firmware/releases) · [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type)

<p align="center">
  <img src="docs/assets/readme/keyboard-front.jpg" alt="带按键、旋钮和状态灯的 Listener 语音键盘" width="900" />
</p>

## 一个完整的 Listener 产品

| 仓库 | 负责什么 |
| --- | --- |
| [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type) | 语音识别、文字清理与风格、翻译、历史、光标插入、设置界面和桌面端更新 |
| Listener Firmware | 麦克风采集、BLE 音频/HID、实体控制、灯、电池与电源、设备设置、诊断和固件在线升级（OTA） |

固件本身不会把语音变成文字。Listener Type 接收音频、生成最终文字并插入当前应用。

## 硬件一览

| 部分 | 当前 V2 配置 |
| --- | --- |
| 主控 | ESP32-S3-WROOM-1-N16R8，16 MB 闪存，8 MB Octal PSRAM |
| 音频 | PDM 麦克风，16 kHz 采集，分帧 BLE 传输和保留/重放包 |
| 控制 | 可按压、可旋转的 EC11 旋钮和 KEY1–KEY4 |
| 反馈 | 六组状态灯：PWR、BLE、REC、AI、OK、WARN |
| 无线 | BLE 音频、BLE HID 键盘、设置、诊断、电量服务和 OTA |
| 电源 | 锂电池、USB-C 充电、电量检测、低功耗休眠、按键唤醒和定时关机 |

## 固件包含的完整能力

| 范围 | 功能 |
| --- | --- |
| 语音采集 | 开始/停止设备录音、PDM 采音、PCM 分帧排队、保留尾包并报告传输状态 |
| BLE 音频 | 可订阅音频流、按速率通知、重试与背压、保留包重放和会话身份 |
| 键盘控制 | EC11 单击/双击/长按/旋转；KEY1–KEY4 单击/双击/长按；安全 BLE HID 兜底 |
| 产品反馈 | 录音、传输、处理、成功、警告、配对、充电、电量和低功耗灯效 |
| 设备设置 | 持久化蓝牙名称、分区灯光亮度、旋钮动作、插电/电池休眠时间、低功耗与关机时间 |
| 电池与电源 | 标准 BLE 电量上报、充电/充满状态、低电保护、空闲麦克风休眠、按键唤醒和硬件关机 |
| 配对与恢复 | 可发现配对、清除绑定、重连、串口维护和显式恢复出厂路径 |
| OTA | 双应用分区、包验证、进度状态、待验证启动、失败回滚，并在正常升级中保留配对/设置 |
| 诊断 | Flash 事件日志、健康心跳、BLE/串口导出、来源开关、有限长度报告和机器可读诊断包 |
| 工程工具 | 可重复的安装/构建/刷写/监控，以及主板、音频、BLE、电源、按键和 OTA 验证 |

## 从开机到文字

1. 给键盘充电，单击旋钮开机。
2. 在 Listener Type 中进入设置 → 设备 → 开始配对；Windows 蓝牙选择 `listener`，再回 Type 检查连接。
3. 光标点进输入框，单击旋钮，说话，再单击。
4. REC 表示正在采音，AI 表示传输或处理，OK 表示完成；文字由 Listener Type 放回光标。

语音自动开始在 Listener Type 中配置。固件维持低功耗人声活动路径并传输候选音频；桌面端检查唤醒词和可选声纹后，才接受正式听写会话。

## 灯在说什么

键盘用光说话：六颗状态灯、一圈旋钮灯环、每颗按键下面一盏灯。词汇表学会一次，以后扫一眼就知道设备在做什么。

| 灯 | 它在说什么 |
| --- | --- |
| **PWR** | 绿色：电量充足。琥珀色：该充电了。红色：低电量。红色双闪：快没电了。插上 USB-C 后变白色——呼吸是充电中，常亮是充满了 |
| **BLE** | 蓝色闪烁：正在配对或回连。低亮度双闪：蓝牙连上了，但电脑上的 Listener Type 还没打开。常亮：就绪，可以说话 |
| **REC** | 金色，随你的声音起伏。灯亮着，就是在认真听 |
| **AI** | 紫色「哒-哒哒」的节拍：录音收到了，正在转写 |
| **OK** | 绿色亮两秒：文字送到了。固件升级时变青绿色显示进度，旋钮环跟着一起填满 |
| **WARN** | 红色或琥珀色：有状况需要处理。所有错误只由这一颗灯报告 |

绿是好消息，金是在听，紫是在想，蓝是蓝牙，白是充电，红是有问题。

按键也会回应：按下先亮白色，动作生效闪紫色——单击一次，双击两次，长按期间常亮。

## 键怎么用

| 操作 | 默认行为 |
| --- | --- |
| 单击旋钮 | 开始 / 停止听写 |
| 双击旋钮 | 清除蓝牙绑定，重新可被搜索 |
| 长按旋钮 | 关机——旋钮周围亮起琥珀色确认环，环转满后继续按住即关机 |
| 旋转旋钮 | 系统音量；可改成屏幕亮度或禁用 |
| KEY1–KEY4 | 在 Listener Type「设置 → 设备」里自定义单击、双击、长按动作。KEY3 出厂是 Ctrl+V |

没配置的键只会发出 F13–F24 这类无害按键，旋钮的兜底是 Shift+F13——不会自己往你的文档里打字。电脑上的 `Esc` 随时可以取消录音。

## 电源，简单说

旋钮是唯一的开机键。放着不用，键盘会打个盹——电池约一分钟，插电约三分钟——只留 PWR 一颗灯。灯全灭不是坏了，是睡着了；按任意键或转一下旋钮就醒。电池下闲置约十分钟会真正关机，再按一下旋钮开机。

状态灯、键位灯、旋钮环、边框四个分区的亮度都能在 Listener Type 里分别调节，也有几档预设。就算把灯效整个关掉，低电量和错误提示依然会亮——重要的消息不缺席。Windows 可以通过标准蓝牙电量服务读取当前电量。

<p align="center">
  <img src="docs/assets/readme/device-settings.png" alt="Listener Type 中的设备设置" width="720" />
</p>

## 升级与恢复

普通用户在 Listener Type 中选择正式 OTA ZIP。OTA 使用两个固件分区，未验证的新固件可以回滚；正常升级保留蓝牙绑定和设备设置。配对出错时双击旋钮恢复。USB 刷写和整片擦除属于工程/恢复操作，可能清除保存的状态。

最新标记版本在 [Releases](https://github.com/Listener-ai-Macau/Listener-Firmware/releases)。尽量与同版本 Listener Type 配套，并核对发布页的 SHA-256。

## 版本脉络

| 版本 | 固件进展 |
| --- | --- |
| 1.0.1 | 建立 Listener 固件发布包 |
| 1.0.3 | 完成 BLE 传输速度、OTA/启动和状态灯约束 |
| 1.0.4 | 收拢录音、唤醒、自动结束、配对恢复、电源和产品灯效 |
| 1.0.5 | 让当前语音键盘控制与反馈配套 Type 1.0.5 日常使用链路 |

准确 OTA 包与校验值以 [GitHub Releases](https://github.com/Listener-ai-Macau/Listener-Firmware/releases) 为准。

## 构建与刷写

Windows 脚本会准备 ESP-IDF `release/v5.5` 并保持环境一致：

```powershell
pwsh -NoProfile -File .\tools\setup_windows.ps1
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
pwsh -NoProfile -File .\tools\monitor.ps1 -Port COMx
```

临时运行 ESP-IDF 命令时使用 `tools\idf.ps1`，不要直接运行 `idf.py`。

## 仓库结构

- `components/` — 控制、电源、设置、健康、OTA 和诊断等可移植产品逻辑
- `ports/esp32/` — 主板、音频、BLE、存储和硬件服务的 ESP-IDF 绑定
- `protocols/` — 共享设备协议和编解码
- `main/` — 启动与子系统装配
- `tools/` — 环境、构建、刷写、监控、打包、诊断和验证
- `docs/features/firmware-feature-map.md` — 完整实现与验证地图

贡献前请阅读 [CONTRIBUTING.md](CONTRIBUTING.md)，安全问题见 [SECURITY.md](SECURITY.md)。本仓库以 [Apache-2.0 许可证](LICENSE) 开源。
