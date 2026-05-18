# 麦克风选型与新原理图软件迁移计划

## 总体判断

建议新板优先采用 `SPH0645LM4H-B` 这类 `I2S` 数字麦方案。

原因：

- 当前产品只需要设备端采音上传，不需要耳机、喇叭、line-in、line-out 这些完整 codec 能力
- `SPH0645LM4H-B` 可直接输出 `I2S` 数字音频，省掉 `ES8311` 外部 ADC / codec 初始化链路
- 固件可以继续使用 `ESP-IDF` 官方 `i2s_std` 驱动，不需要自定义底层音频驱动
- 软件边界更干净：把麦克风差异限制在 `ports/esp32/audio_capture/`，上层仍然只处理 `16kHz / mono / int16 PCM`

如果另一个待比较对象是“当前 `ES8311 + 模拟麦` 方案”，结论是：

- 语音键盘 / 便携录音上传场景：`SPH0645LM4H-B` 更合适
- 需要模拟输入调音、耳机 / 喇叭、line-in/out、复杂音频 codec 能力：`ES8311 + 模拟麦` 更合适
- 当前项目不需要完整 codec 能力，所以 `SPH0645LM4H-B` 是更轻、更省料、更贴近产品目标的方案

## 当前链路

当前仓库记录的采音链路是：

```text
MIC -> ES8311 ADC -> I2S -> ESP32-S3
```

当前软件位置：

- `ports/esp32/audio_capture/audio_capture_esp32.c`
  - 当前包含 `esp_codec_dev` / `ES8311` 初始化
  - 当前通过 `esp_codec_dev_read()` 读取 `int16` PCM
  - 当前 I2S 配置为 `16-bit / mono`
- `ports/esp32/audio_capture/CMakeLists.txt`
  - 当前依赖 `esp_codec_dev`
- `ports/esp32/audio_capture/idf_component.yml`
  - 当前声明 `espressif/esp_codec_dev`
- `ports/esp32/audio_capture/include/audio_capture_platform.h`
  - 当前暴露 `audio_capture_get_i2c_bus_handle()`
- `ports/esp32/voice_key_input/voice_key_input_esp32.c`
  - 当前会复用 `audio_capture_get_i2c_bus_handle()` 作为按键扩展器的共享 I2C bus 候选

## `SPH0645LM4H-B` 关键事实

来自本地 datasheet：`C:\Users\Billy\Desktop\SPH0645LM4H-B.pdf`

- 类型：`I2S` 数字麦
- 主从关系：麦克风是 `I2S slave`，主控必须提供 `BCLK` 和 `WS`
- 数据格式：`I2S`、`24-bit`、二进制补码、`MSB first`
- 有效精度：`18-bit`，低位补 `0`
- 采样率关系：`WS = BCLK / 64`
- 支持时钟：`1.024 MHz` 到 `4.096 MHz`
- 对应采样率：`16 kHz` 到 `64 kHz`
- 电源：`1.62 V` 到 `3.6 V`
- 典型电流：约 `600 uA`
- SNR：典型 `65 dB(A)`
- 灵敏度：典型 `-26 dBFS`
- `SELECT`：
  - `SELECT = LOW` 时，麦克风在 `WS = LOW` 的 slot 驱动 `DATA`
  - `SELECT = HIGH` 时，麦克风在 `WS = HIGH` 的 slot 驱动 `DATA`
- 单麦接入时，`DATA` 建议加 `100 kOhm` 下拉，避免非驱动 slot 漂浮

## 两个方案对比

| 项目 | `SPH0645LM4H-B` I2S 数字麦 | 当前 `ES8311 + 模拟麦` |
|---|---|---|
| 适配当前产品 | 更适合 | 能用但偏重 |
| 外围器件 | 少，不需要外部 ADC / codec | 多，需要 codec、I2C 控制、模拟前端 |
| 固件复杂度 | 中低，直接走官方 I2S RX | 中，需要 codec 初始化和增益配置 |
| 官方路径 | ESP-IDF `i2s_std` 直接读取 | `esp_codec_dev + ES8311` |
| 上层协议影响 | 无，仍输出 `16kHz mono int16 PCM` | 无 |
| 调音空间 | 主要靠数字增益 / 滤波 / 结构 | codec 侧模拟 / 数字增益空间更大 |
| 抗模拟噪声 | 更好，数字输出离开麦克风 | 更依赖 PCB 模拟前端和 codec 供电 |
| 量产风险 | 声学结构、I2S slot、时钟、供电 | 模拟噪声、codec 初始化、增益一致性 |
| 未来 STM32 迁移 | 更好，STM32 I2S/SAI 可直接接 | 需要迁 codec 控制和模拟链路 |

结论：

- 当前产品优先选 `SPH0645LM4H-B`
- 保留 `ES8311 + 模拟麦` 的唯一强理由，是未来硬件还需要播放、耳机、line-in/out 或非常细的模拟增益控制

## 新原理图需要确认的硬件项

软件改动必须以新原理图为准，至少确认这些 net：

- `MIC_BCLK`
- `MIC_WS` / `MIC_LRCLK`
- `MIC_DATA`
- `MIC_SELECT` / `L/R`
- `MIC_VDD`
- `MIC_GND`
- 是否有 `MIC_EN` / 电源开关 GPIO
- 是否仍保留 `ES8311`
- 是否仍保留 `I2C0 SDA/SCL` 给按键扩展器
- `MIC_DATA` 是否有 `100 kOhm` 下拉
- 麦克风电源是 `3.3 V` 还是 `1.8 V`

硬件建议：

- 如果 `DATA/BCLK/WS` 直连 `ESP32-S3 3.3 V GPIO`，优先让 `SPH0645LM4H-B` 也跑 `3.3 V`
- `VDD` 旁边放近端去耦，至少 `0.1 uF`
- `BCLK/WS/DATA` 走线尽量短，必要时按 datasheet 建议加 `27-51 Ohm` 串阻做阻尼
- 底部进音孔和结构开孔需要按声学设计确认，这部分比软件更容易影响最终听感
- 如果 RF 环境复杂，按 datasheet 预留 `20-200 pF` RF filter 电容位置

## 软件迁移原则

优先使用官方 / 现有机制：

- 使用 `ESP-IDF` 官方 `i2s_std` RX
- 保持 `ble_audio_stream`、`listener_audio_proto.h`、Windows 接收端不变
- 保持输出契约为 `16kHz / mono / 16-bit / PCM little-endian`
- 不新增自定义底层 I2S 驱动
- 不把当前 `audio_data` 重新改回旧 `chunk + fragment` 模型

自定义逻辑只保留在必要位置：

- `24/32-bit I2S sample -> int16 PCM` 转换
- 可选 DC-block / 高通
- 可选数字增益 / 限幅
- slot 选择与新原理图 `SELECT` 绑定

## 预计代码改动

### 1. `ports/esp32/audio_capture/audio_capture_esp32.c`

类型：`firmware-only`

改动：

- 按新原理图更新 I2S GPIO：
  - `BCLK`
  - `WS`
  - `DIN`
  - 可选 `MIC_EN`
- 不再初始化 `ES8311`
- 不再调用 `esp_codec_dev_read()`
- 改为调用 `i2s_channel_read()`
- I2S 建议配置：
  - `I2S_ROLE_MASTER`
  - `sample_rate = 16000`
  - `slot_bit_width = 32`
  - `slot_mode = I2S_SLOT_MODE_MONO` 或 `I2S_SLOT_MODE_STEREO + 软件取指定 slot`
  - 根据 `SELECT` 选择 `I2S_STD_SLOT_LEFT` 或 `I2S_STD_SLOT_RIGHT`
- 将读取到的 `32-bit` sample 转成当前链路需要的 `int16`
- 初版建议保守处理音量：
  - 先只右移缩放，不加过激增益
  - 通过实测 WAV 峰值和听感再调数字增益

风险点：

- 如果 `SELECT` 与 slot mask 配反，会出现静音或极小噪声
- 如果 `slot_bit_width` 配成 `24`，`16 kHz` 下 BCLK 可能变成 `768 kHz`，不满足 datasheet 的 `BCLK/64 = 16 kHz` 目标
- `24-bit` 数据在 `32-bit` DMA buffer 里的对齐方式需要用实测确认

验收：

- 构建通过
- 日志出现 `i2s start ok`
- 不再依赖 `codec init ok`
- 抓取 WAV 为 `16000 Hz / mono / 16-bit`
- 人声录音有明显波形，不是全 0、全满幅或固定噪声

### 2. `ports/esp32/audio_capture/CMakeLists.txt`

类型：`firmware-only`

改动：

- 移除不再使用的 `esp_codec_dev`
- 保留：
  - `esp_driver_i2s`
  - `esp_driver_i2c`
  - `ble_audio_stream`

验收：

- `idf.py build` 不再需要 `esp_codec_dev` 才能编译 `audio_capture`

### 3. `ports/esp32/audio_capture/idf_component.yml`

类型：`firmware-only`

改动：

- 如果确认新硬件完全去掉 `ES8311`，移除 `espressif/esp_codec_dev`
- 如果短期需要兼容旧板和新板，可先保留依赖，等旧板分支关闭后再删

验收：

- 新板目标构建通过
- 依赖清单不再引入无用 codec 组件，或在兼容期明确标注保留原因

### 4. `audio_capture_get_i2c_bus_handle()` 边界

类型：`firmware-only`

当前 `voice_key_input` 会复用 audio capture 暴露的 I2C bus。换成数字麦后，麦克风不需要 I2C，但按键扩展器可能仍需要。

建议分两阶段：

- 阶段 1：为了少改动，保留 `audio_capture_i2c_init()`，只移除 `ES8311` codec 初始化
- 阶段 2：把共享 I2C bus 从 `audio_capture` 迁到更合理的 board / esp32 platform 层

验收：

- `voice_key_input` 仍能 probe 到按键扩展器，或明确进入 direct GPIO fallback
- `KEY1` 真实按键路径仍能触发录音

### 5. `!docs/features/audio_capture_ble_upload.md`

类型：`docs-only`

改动：

- 把采音路径从：

```text
MIC -> ES8311 ADC -> I2S -> ESP32-S3
```

更新为：

```text
SPH0645LM4H-B -> I2S -> ESP32-S3
```

验收：

- 文档与新原理图、新代码一致

## 验收矩阵

### Bring-up 必跑

```powershell
idf.py build
powershell -ExecutionPolicy Bypass -File .\tools\flash.ps1 -Port COM3
python .\tools\capture_audio_ble_wav.py --port COM3 --capture-seconds 5 --no-reset-before-capture
```

通过标准：

- 构建成功
- 烧录成功
- 录音文件可生成
- WAV 是 `16kHz / mono / 16-bit`
- 人声听感可辨认
- 日志没有持续 `overflow` / `underrun` / `dropped frame`

### 产品回归必跑

```powershell
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --realistic-usage-profile --random-seed 20260525
```

通过标准：

- `P1-P10` 全部 `pass`
- `failed=0`
- `warning=0`

### 真实按键必跑

```powershell
python .\tools\capture_audio_ble_wav.py --port COM3 --capture-seconds 10 --trigger-mode physical-key --no-reset-before-capture --timeout-seconds 120
```

通过标准：

- 固件日志包含 `recording start source=key1`
- 固件日志包含 `recording stop source=key1`
- WAV 内容与实际说话一致
- 无明显削波、静音、周期噪声

## 决策建议

建议硬件定版前按这个顺序走：

1. 原理图确认 `SPH0645LM4H-B` 的 `BCLK/WS/DATA/SELECT/VDD` 连接
2. 软件先做最小迁移：官方 I2S RX + 32-bit slot + int16 输出
3. 跑单轮录音，确认 slot、对齐、音量
4. 跑 `P1-P10 realistic` 矩阵，确认 BLE 上传没有被采音改动拖垮
5. 跑 `P11` 真实按键听感
6. 再决定是否做 DC-block、高通、数字增益和限幅

如果新原理图已经确认会去掉 `ES8311`，这件事不建议再继续维护双 codec 路径。双路径会提高测试矩阵复杂度，也更容易让量产问题被旧板兼容逻辑掩盖。

## 当前结论

- 软件改动量：中等偏小
- 硬件 / 声学验证风险：中等
- 推荐选型：`SPH0645LM4H-B`
- 不建议继续使用完整 codec 方案，除非产品后续明确需要播放 / 模拟音频接口
- 迁移后对 `protocols/` 和 Windows 接收端应保持零影响
