# 蓝牙语音键盘 v1 功能总览方案

## 目标

把当前 `voice-keyboard-firmware` 从“BLE HID 验证 demo”推进到“蓝牙语音键盘 v1 功能闭环”的计划范围定义清楚，并拆成一组可逐步批准、逐步实施的子方案。

## 问题

当前仓库已经完成的稳定能力主要是：

- BLE HID 键盘脚本输入验证链路

对应稳定总结已在：

- [ble_hid_keyboard_input.md](../features/ble_hid_keyboard_input.md)

但按产品文档，蓝牙语音键盘 `v1` 还缺少多项关键能力：

- BLE 键盘产品化能力
- 实体按键输入
- 语音键触发控制
- `Windows` 中文文本注入
- 音频采集
- 音频上传
- 日志与自检

## 范围

- 为蓝牙语音键盘 `v1` 剩余主要功能建立子方案
- 明确各功能之间的依赖顺序
- 明确哪些内容当前可直接做，哪些受硬件或后端约束
- 为“无后端先验证麦克风 / 喇叭是否正常”的主机侧保存与回放路径预留独立方案入口
- 明确哪些能力属于设备侧，哪些能力必须由 `Windows` 主机伴随程序承担

## 不在本次范围内

- 直接开始实现这些功能
- 离线语音识别
- `2.4G dongle`
- 完整 OTA 闭环
- 极致低功耗优化

## 前提与依赖

- 当前产品边界以 [product_solutions.md](../product_solutions.md) 为准
- 当前仓库仍是 `ESP32-S3` bring-up 阶段，不是量产键盘固件
- 当前开发板原理图为：
  [schematic_v1.pdf](../hardware/esp32s3_board_v1/schematic_v1.pdf)
- 如果最终目标提高到“类似闪电说的嵌入式语音助手”，应先参考：
  [shandianshuo_like_embedded_voice_assistant_plan.md](./shandianshuo_like_embedded_voice_assistant_plan.md)
- 如果当前先要把 `Windows` 中文输出能力独立打通，应先参考：
  [windows_unicode_text_injection_plan.md](./windows_unicode_text_injection_plan.md)
- 如果当前先要确认麦克风和喇叭链路是否真的好，应先参考：
  [audio_input_output_host_capture_plan.md](./audio_input_output_host_capture_plan.md)

## 步骤

### 第一步：完成 BLE 键盘产品化能力方案

状态：`pending`

工作内容：

- 建立 BLE 键盘产品化相关方案
- 范围包括配对、绑定、重连、电量、主机输出状态等

验收方式：

- [ble_keyboard_productization_plan.md](./ble_keyboard_productization_plan.md) 存在且内容完整

人工检查点：

- 人确认 BLE 产品化范围可接受后，AI 才能进入该子方案实现

### 第二步：完成实体按键输入方案

状态：`pending`

工作内容：

- 建立实体按键输入与 HID 映射方案

验收方式：

- [keyboard_matrix_input_plan.md](./keyboard_matrix_input_plan.md) 存在且内容完整

人工检查点：

- 人确认实体按键输入方案可接受后，AI 才能进入该子方案实现

### 第三步：完成语音键控制方案

状态：`pending`

工作内容：

- 建立语音键按下、保持、释放与取消的状态控制方案

验收方式：

- [voice_key_recording_control_plan.md](./voice_key_recording_control_plan.md) 存在且内容完整

人工检查点：

- 人确认语音键控制方案可接受后，AI 才能进入该子方案实现

### 第四步：完成 `Windows` 中文文本注入方案

状态：`pending`

工作内容：

- 建立 `Windows` 主机侧的 Unicode 文本注入路线
- 先验证 mock 文本 `测试` 能稳定进入当前输入框

验收方式：

- [windows_unicode_text_injection_plan.md](./windows_unicode_text_injection_plan.md) 作为当前有效方案保留

人工检查点：

- 人确认当前先做中文输出主链路可接受后，AI 才能进入该子方案实现

### 第五步：完成无后端音频输入占位方案

状态：`pending`

工作内容：

- 保留并推进当前“无后端先打通占位链路”的方案

验收方式：

- [audio_input_placeholder_plan.md](./audio_input_placeholder_plan.md) 作为当前有效方案保留

人工检查点：

- 人确认当前先走占位链路的策略可接受后，AI 才能进入该子方案实现

### 第六步：完成正式音频采集方案

状态：`pending`

工作内容：

- 建立 `ES8311 + MIC + I2S` 正式音频采集方案

验收方式：

- [audio_capture_bringup_plan.md](./audio_capture_bringup_plan.md) 存在且内容完整

人工检查点：

- 人确认音频采集方案可接受后，AI 才能进入该子方案实现

### 第七步：完成音频上传方案

状态：`pending`

工作内容：

- 建立音频分片上传与协议对接方案

验收方式：

- [audio_stream_upload_plan.md](./audio_stream_upload_plan.md) 存在且内容完整

人工检查点：

- 人确认上传方案和协议依赖可接受后，AI 才能进入该子方案实现

### 第八步：完成日志与自检方案

状态：`pending`

工作内容：

- 建立日志、自检、版本与故障定位方案

验收方式：

- [device_logging_selftest_plan.md](./device_logging_selftest_plan.md) 存在且内容完整

人工检查点：

- 人确认日志与自检方案可接受后，AI 才能进入该子方案实现

### 第九步：完成实体按键输入方案实现前准备

状态：`pending`

工作内容：

- 建立日志、自检、版本与故障定位方案

验收方式：

- [device_logging_selftest_plan.md](./device_logging_selftest_plan.md) 存在且内容完整

人工检查点：

- 人确认日志与自检方案可接受后，AI 才能进入该子方案实现

## 当前阻塞项

- 当前只是完成方案梳理，还没有开始任何一个子方案的实现审批
- 当前“中文输出”如果要求稳定支持 `Windows` 输入框，不应继续走“纯 BLE HID 直接注入中文”的假设
- 当前还没有主机侧 Unicode 注入工具实现

## 备注

- 当前建议的执行顺序是：
  1. `windows_unicode_text_injection_plan`
  2. `audio_input_placeholder_plan`
  3. `audio_input_output_host_capture_plan`
  4. `voice_key_recording_control_plan`
  5. `ble_keyboard_productization_plan`
  6. `audio_capture_bringup_plan`
  7. `audio_stream_upload_plan`
  8. `device_logging_selftest_plan`
  9. `keyboard_matrix_input_plan`

- 其中最容易被外部依赖卡住的是：
  `audio_stream_upload_plan` 和 `Windows` 主机侧中文注入方案

- 其中最适合当前立即推进的是：
  `windows_unicode_text_injection_plan`

- 如果问题是“现在要不要先把中文输出搞定”，当前建议是：
  - 先把 `Windows` 主机侧 Unicode 中文输出打通
  - 先用 mock 文本 `测试` 验证
  - 再做设备侧语音输入
  - 后续后端返回什么字符串，就复用当前主机注入链路输出什么字符串

- 如果目标从“BLE 语音键盘 v1”升级为“类似闪电说的嵌入式语音助手”，
  当前这份 roadmap 只覆盖设备侧子集，不覆盖主机伴随程序、上下文理解和技能执行。
