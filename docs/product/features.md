# Listener 语音键盘能做什么（1.0.5）

这是桌上那台键盘的固件，不是开发板示例。
它负责：收音、按键、灯、配对、低功耗和升级。文字识别和插入在 [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type)。

产品怎么写，见 Type 仓库 [怎么写产品功能](https://github.com/Listener-ai-Macau/Listener-Type/blob/master/docs/product/writing.md)。两边讲的是同一条用户路径。

## 你买键盘，图的是这些

| 手上的事 | 固件做什么 | 你看到什么 |
| --- | --- | --- |
| 开始 / 停止录音 | EC11 单击走录音动作 | REC 灯；Type 胶囊进入 Recording |
| 不用碰电脑唤醒 | 等唤醒词，默认识别「开始录音」 | 正式会话从唤醒词之后开始 |
| 看设备醒着没有 | PWR / BLE / REC / AI / OK / WARN | 蓝灯就绪，录音亮 REC，处理亮 AI |
| 配对乱了 | EC11 双击重新可被发现 | Windows 里重新选 `listener` |
| 键按自己的习惯 | KEY1–KEY4 单击/双击/长按 | Type「设置 → 设备」里改动作 |
| 插电或电池 | 低功耗、亮度、空闲关机 | 灭灯往往是省电，不是坏了 |
| 升级 | Type 里的 OTA，或 USB 刷写 | 一次重启进新版本，配对还在 |

软件功能（插入光标、风格、词库）写在 Type 的 [产品功能](https://github.com/Listener-ai-Macau/Listener-Type/blob/master/docs/product/features.md)。

## 按键

| 控件 | 1.0.5 默认 |
| --- | --- |
| EC11 单击 | 开机后：开始或停止录音 |
| EC11 双击 | 重置蓝牙，重新进入可配对 |
| EC11 长按 | 关机 |
| EC11 旋转 | 默认电脑音量，可在 Type 里改 |
| KEY1–KEY4 | 自定义；未配置时走安全兜底，不会乱打字 |

录音键以 Type 里当前的设备自定义为准。出厂常见是旋钮管录音。

## 灯

| 灯 | 含义 |
| --- | --- |
| PWR | 电源和电池 |
| BLE | 蓝牙。Type 就绪后稳定蓝 |
| REC | 设备确实在采音 |
| AI | 停止后传输或主机正在处理 |
| OK | 成功，或升级进行中 |
| WARN | 需要处理的错误 |

低功耗灭灯是预期行为。亮度只是上限，不代表音量或声纹分数。

## 和 Type 怎么分工

```text
键盘：听到、按下、亮灯、把音频送到电脑
Type：转写、整理、插入光标、OTA、配对引导
```

没有 Type，键盘仍是一块带灯的 BLE 设备，不能完成「字出现在光标」。
没有键盘，Type 仍可用电脑麦克风试用。

完整硬件音频路径以 Windows 为主。装哪个 OTA、哈希见 [1.0.5 发布说明](../release/1.0.5.md)。刷写和工程地图见仓库根 README 与 `docs/features/firmware-feature-map.md`。
