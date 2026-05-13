# ESP32-S3 开发板硬件资料

这个目录用于集中存放当前 `ESP32-S3` 开发板的本地硬件参考资料，方便后续 AI 和人工直接查看，不需要每次临时重新导出截图。

## 当前文件

- [schematic_v1.pdf](./schematic_v1.pdf)
  主原理图 PDF
- `tmp_audio_page2.png`
  音频页整页截图
- `tmp_encoder_zoom2.png`
  `ES8311 + I2S` 区域放大图
- `tmp_mic_zoom2.png`
  麦克风输入区域放大图
- `tmp_audio_jack_zoom.png`
  音频座区域放大图
- `tmp_mcu_left_mid.png`
  MCU 左侧 `SDA1 / SCL1` 对应 GPIO 放大图
- `tmp_eeprom_i2c.png`
  `SDA1 / SCL1` 总线参考图

其余 `tmp_*.png` 主要是过程性裁剪图，保留用于后续交叉核对。

## 当前用途

- 核对音频输入链路
- 核对音频输出链路
- 核对 I2C / I2S / GPIO 映射
- 后续补设备驱动时作为固定参考资料
