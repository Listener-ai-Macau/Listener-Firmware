# Voice Keyboard V1.0 原理图软件适配方案

## 任务目标

把当前固件从旧假设 `ES8311 + analog mic + I2C/XL9555 key input` 适配到 `!docs/schematics/Voice Keyboard V1.0.pdf` 对应的新原理图。

本方案先作为 fix/bring-up 执行计划，不直接改代码。后续每一步完成后，应把稳定结论合并回 `!docs/features/audio_capture_ble_upload.md` 或对应功能文档，并删除本 fix 文档。

## 原理图事实

来源：`!docs/schematics/Voice Keyboard V1.0.pdf`，日期标注 `2026-05-15`，共 4 页。

### 音频输入

新原理图使用 `SPH0645LM4H-1-8` I2S 数字麦克风，不再使用 `ES8311` codec。

| 信号 | 器件引脚 | ESP32-S3 IO | 备注 |
| --- | --- | --- | --- |
| `BCLK` | SPH0645 `BCLK` | `IO39` | I2S bit clock |
| `LRCLK` / `WS` | SPH0645 `WS` | `IO40` | I2S word select |
| `DOUT` | SPH0645 `DATA_OUT` | `IO41` | ESP32 I2S DIN |
| `SELECT` | SPH0645 `SELECT` | `VSS` | 固定选择一个声道 |
| `VDD` | SPH0645 `VDD` | `3.3VDCA` | 经过磁珠/去耦 |

当前代码冲突点：

- `AUDIO_CAPTURE_I2S_MCLK_IO` 现在硬编码为 `45`，但新原理图 `IO45` 是 `KEY-1`，必须释放。
- 当前 `AUDIO_CAPTURE_I2S_WS_IO=41`、`AUDIO_CAPTURE_I2S_DIN_IO=40` 与新原理图相反，应调整为 `WS=40`、`DIN=41`。
- 当前仍初始化 `I2C + ES8311 + esp_codec_dev`，新板不需要 codec I2C 控制链路。

### 按键和旋钮

新原理图按键为直接 GPIO 输入，硬件上有上拉和 100 nF 去抖电容，按下为低电平。

| 功能 | ESP32-S3 IO | 备注 |
| --- | --- | --- |
| `KEY-1` | `IO45` | 当前产品用作录音 start/stop |
| `KEY-2` | `IO48` | 后续快捷键 |
| `KEY-3` | `IO47` | 后续快捷键 |
| `KEY-4` | `IO21` | 后续快捷键 |
| `EC11-A` | `IO36` | 旋钮 A 相 |
| `EC11-B` | `IO38` | 旋钮 B 相 |
| `EC11-C` | `IO37` | 旋钮公共/相位相关脚，需按实物确认 |
| `EC11-KEY` | `IO35` | 旋钮按键 |

当前代码冲突点：

- `voice_key_input_esp32.c` 仍尝试探测 `XL9555/TCA9555`，并有 `GPIO0` direct fallback。
- 新原理图没有 XL9555 路径，`GPIO0` 只用于 BOOT 按键，不能继续作为语音键 fallback。
- 现有 `volatile uint32_t s_toggle_event_count` 不是队列/临界区模型，量产前建议一起改掉。

### USB 和电源检测

| 信号 | ESP32-S3 IO | 软件用途 |
| --- | --- | --- |
| `USB_DP` | `IO20` | ESP32-S3 USB Serial/JTAG / native USB |
| `USB_DN` | `IO19` | ESP32-S3 USB Serial/JTAG / native USB |
| `USB_Det` | `IO9` | 可用于判断 USB 5V 是否存在 |
| `BAT_CHG_IO` | `IO3` | 充电状态输入 |
| `BAT_STD_IO` | `IO46` | 充满/待机状态输入 |
| `BAT_V_ADC` | `IO7` | 电池分压 ADC 输入，R17=68K、R18=68K |

当前代码冲突点：

- BLE HID battery level 仍是固定 `100` placeholder。
- 没有读取 `BAT_CHG_IO`、`BAT_STD_IO`、`BAT_V_ADC`、`USB_Det`。

## 官方优先 / 自定义边界

优先复用官方/成熟路径：

- 音频采集：使用 ESP-IDF `driver/i2s_std` 直接读 I2S 数字麦，不再经过 `esp_codec_dev`。
- 按键输入：使用 ESP-IDF `driver/gpio`，可先采用低风险轮询 + 软件消抖；若后续功耗需要，再切到 GPIO interrupt + FreeRTOS queue。
- 电池电压：使用 ESP-IDF `esp_adc/adc_oneshot.h` 和 ADC calibration。
- USB 串口：保留现有 ESP-IDF `usb_serial_jtag` 路线，新原理图 `IO19/IO20` 与 ESP32-S3 native USB 路线一致。

保留的自定义层：

- BLE 音频 session notify 协议和 Windows host 重组脚本继续沿用，不因原理图适配而重写。
- KEY1 start/stop 产品语义保留，但底层输入来源从旧的 XL9555/IO0 改为新板 `IO45`。

## 实施步骤

### P1 固化板级引脚映射

变更类型：固件配置 / 结构整理。

预计改动：

- 新增或整理 ESP32 板级引脚定义，例如 `ports/esp32/board_pins/` 或在现有 port 文件内集中定义。
- 避免继续在 `audio_capture_esp32.c`、`voice_key_input_esp32.c` 多处散落硬编码。

验收：

- `rg "GPIO_NUM_0|AUDIO_CAPTURE_I2S_MCLK_IO|VOICE_KEY_INPUT_DIRECT_GPIO|XL9555|TCA9555" ports components` 能清楚看出旧路径已删除或只留在明确注释的兼容分支。
- `powershell -ExecutionPolicy Bypass -File .\tools\test.ps1` 通过。

人工检查点：

- 确认 `IO45`、`IO46` 在 ESP32-S3 上作为 strapping/input 相关脚的上电约束是否满足量产要求。
- 确认用户按住 `KEY-1` 上电/复位时，不会进入异常启动模式。

### P2 适配 SPH0645 I2S 数字麦

变更类型：固件音频驱动。

预计改动：

- `ports/esp32/audio_capture/audio_capture_esp32.c`
  - 删除 `ES8311`、`esp_codec_dev`、codec I2C 初始化、MCLK 输出。
  - I2S pin 改为 `BCLK=GPIO39`、`WS=GPIO40`、`DIN=GPIO41`、`DOUT=-1`、`MCLK=I2S_GPIO_UNUSED`。
  - 按 SPH0645 数字麦使用 32-bit I2S slot 读取，再转换为当前 BLE 链路需要的 16-bit PCM。
  - 保持输出 WAV 为 `16 kHz / mono / int16`，不改变 BLE session protocol。
- `ports/esp32/audio_capture/CMakeLists.txt`
  - 移除 `esp_codec_dev` 依赖，只保留 `esp_driver_i2s` 和 `ble_audio_stream` 等必要项。
- `ports/esp32/audio_capture/idf_component.yml`
  - 移除 `espressif/esp_codec_dev` 直接依赖，如果没有其它模块使用，后续可更新 `dependencies.lock`。
- `ports/esp32/audio_capture/include/audio_capture_platform.h`
  - 如果不再共享 audio I2C，删除该接口或保留兼容 stub，并同步清理调用方。

验收：

- 构建通过：`powershell -ExecutionPolicy Bypass -File .\tools\test.ps1`。
- 烧录后日志包含 I2S/SPH0645 初始化成功标记，不再出现 codec/ES8311 初始化日志。
- `python .\tools\verify_audio_capture_session_end_to_end.py --port COM3 --capture-seconds 5 --artifacts-dir .\tests\artifacts\schematic_v1_p2 --trigger-mode serial-toggle --no-reset-before-capture --timeout-seconds 90` 通过。
- WAV 人耳听感和脚本指标都通过，不能只用“有文件”判断通过。

### P3 改 KEY1 为新板直接 GPIO 输入

变更类型：固件按键输入。

预计改动：

- `ports/esp32/voice_key_input/voice_key_input_esp32.c`
  - 删除 XL9555/TCA9555 探测路径和 I2C fallback。
  - 删除 `GPIO0` direct fallback。
  - `KEY-1=GPIO45` active-low，作为录音 toggle 事件来源。
  - 使用 FreeRTOS queue/task notification 或临界区保护，替换 `volatile s_toggle_event_count`。
  - 加软件消抖，建议先用 20 ms polling + 40 ms stable threshold，和硬件 100 nF 电容叠加。
- `ports/esp32/voice_key_input/CMakeLists.txt`
  - 移除 `esp_io_expander_tca95xx_16bit`、`esp_driver_i2c`、`audio_capture` 依赖。
- `ports/esp32/voice_key_input/idf_component.yml`
  - 移除 `espressif/esp_io_expander_tca95xx_16bit` 依赖，如果没有其它模块使用，后续可更新 `dependencies.lock`。

验收：

- 构建通过。
- 串口日志清楚打印 `voice key ready: source=gpio45 active_low debounce_ms=...`。
- P11 真实按键测试通过：按 KEY1 一次开始，再按一次停止，WAV 时长等于两次真实按键之间的时间窗口；脚本不再要求固定 `--capture-seconds` 时长。
- 快速连按、长按、上电后第一次按键都要有明确测试记录。

### P4 预留 KEY2-KEY4 与 EC11 输入层

变更类型：固件输入扩展，优先级低于 P2/P3。

预计改动：

- 新增轻量输入模块或扩展 `voice_key_input`，记录 `KEY-2/3/4` 和 `EC11` 事件，但默认不改变现有 BLE 音频行为。
- 保持上层 `voice_recording_control` 只消费 `KEY-1` toggle，避免功能串扰。

验收：

- 串口能以 debug 日志看到 KEY2-KEY4/EC11 事件。
- 不按 KEY1 时不会误触发录音。

### P5 增加电池与 USB 状态读取

变更类型：固件电源状态。

预计改动：

- 新增 `ports/esp32/power_status/` 或类似板级电源状态模块。
- `BAT_V_ADC=GPIO7` 通过 ADC oneshot + calibration 读取；分压为 1:1，电池电压约等于 ADC 电压 * 2。
- `BAT_CHG_IO=GPIO3`、`BAT_STD_IO=GPIO46` 作为输入读取充电/满电状态。
- `USB_Det=GPIO9` 可用于区分 USB 供电/电池供电。
- `ports/esp32/ble_hid/ble_hid.c` 中的固定电量 `100` placeholder 改为真实估算值或在没有校准时明确输出 unknown/保守值。

验收：

- 电池电压用万用表与 ADC 日志对比，误差目标先设为 `<= 5%`。
- 插拔 USB 时 `USB_Det` 日志状态变化正确。
- BLE HID battery level 不再是固定 placeholder。

### P6 更新测试矩阵和文档

变更类型：测试脚本 / 文档。

预计改动：

- `!docs/features/audio_capture_ble_upload.md`
  - 更新硬件事实：SPH0645、I2S pins、KEY1 pin、测试口径。
- `tests/README.md`
  - 增加新板 bring-up 证据目录规则。
- `tools/verify_audio_capture_session_end_to_end.py`
  - 保持 P11 的 physical-key 时长口径：以两次真实 KEY1 之间为准，不把 `--capture-seconds` 当硬性时长。
  - 如需要，增加 `--min-physical-key-duration-seconds`，避免误触发短片段被误判通过。

验收：

- 自动测试至少覆盖：P1 serial-toggle、P3 长录音、P10 多轮、P11 物理按键。
- 每次测试记录随机 seed、capture window、artifact 路径、packet loss、WAV 分析指标。
- 没有真实跑过的项目只能标记 `未跑` 或 `blocked`，不能写成通过。

## 已知风险

- `IO45` 是 `KEY-1`，同时当前旧代码把它当 MCLK；软件必须先释放 MCLK。硬件上还需要确认 `IO45` 上电 strap 风险。
- `IO46` 用作 `BAT_STD_IO`，应只作为输入，不应输出驱动。
- `SPH0645` 输出是数字 I2S 麦克风数据，通常需要 32-bit slot 读取后右移/缩放到 int16；缩放系数需要通过实际录音确定，不能只看编译通过。
- 旧 `esp_codec_dev` 删除后，依赖锁文件和 managed component 可在确认无其它用途后清理，避免一次性大改造成回退困难。
- 新原理图没有 XL9555，当前测试中旧 expander fallback 成功不再代表新板成功。

## 当前建议的执行顺序

1. 先做 P1 + P2，让麦克风链路在新原理图上能真实录音。
2. 再做 P3，让 KEY1 物理路径稳定。
3. 然后跑 P1/P3/P10/P11，确认 BLE 音频链路没有因为 I2S 数据格式变化退化。
4. 最后做 P5 电池状态和 P4 扩展输入，避免非核心输入影响音频 bring-up。

## 完成定义

- 新板使用 SPH0645 能录制可听、指标通过的 16 kHz mono WAV。
- KEY1 物理按键能稳定控制开始/停止，至少两轮人工听感和脚本传输指标都通过。
- 构建、flash、P1/P3/P10/P11 测试结果都有真实日志或 artifact。
- 文档更新到 feature 文件，fix 文档关闭后删除。
