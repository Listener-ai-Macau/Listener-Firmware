# 蓝牙语音键盘 v1 功能总览方案

## 目标

把当前 `voice-keyboard-firmware` 从“BLE HID 验证 demo”推进到“蓝牙语音键盘 v1 功能闭环”的计划范围定义清楚，并拆成一组可逐步批准、逐步实施的子方案。

## 问题

当前仓库已经完成的稳定能力主要是：

- BLE HID 键盘脚本输入验证链路
- 设备端音频采集、录音控制与 BLE 音频上传链路

对应稳定总结已在：

- [ble_hid_keyboard_input.md](../features/ble_hid_keyboard_input.md)
- [audio_capture_ble_upload.md](../features/audio_capture_ble_upload.md)

但按当前讨论更新后的产品方向，蓝牙语音键盘 `v1` 还缺少多项关键能力：

- BLE 键盘产品化能力
- `4` 个实体可编程键位与 `1` 个旋钮
- 默认快捷键 / 系统键映射
- 日志与自检
- 蓝牙连接稳定性回归与测试脚本
- `Windows` 主机端常驻语音输入 / agent app

## 范围

- 为蓝牙语音键盘 `v1` 剩余主要功能建立子方案
- 明确各功能之间的依赖顺序
- 明确哪些内容当前可直接做，哪些受硬件或后端约束
- 明确设备端音频链路与 `Windows` 主机端之间的职责边界
- 明确哪些能力当前属于 `Windows` 先行主线，哪些能力降级为后续增强

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
- 如果要看当前新的 `A` 方案主线，应先参考：
  [voice_shortcut_keyboard_plan.md](./voice_shortcut_keyboard_plan.md)
- 当前已经完成的设备侧音频与上传能力总结在：
  [audio_capture_ble_upload.md](../features/audio_capture_ble_upload.md)
- 当前蓝牙连接稳定性与测试脚本边界，以：
  [ble_keyboard_productization_plan.md](./ble_keyboard_productization_plan.md)
  为准
- 当前主机端结构参考优先顺序为：
  - `Type4Me` 作为主参考
  - `Handy` 作为次参考

## 步骤

### 第一步：完成 BLE 键盘产品化能力方案

状态：`pending`

工作内容：

- 建立 BLE 键盘产品化相关方案
- 范围包括配对、绑定、重连、电量、主机输出状态、稳定性目标和测试脚本

验收方式：

- [ble_keyboard_productization_plan.md](./ble_keyboard_productization_plan.md) 存在且内容完整

人工检查点：

- 人确认 BLE 产品化范围可接受后，AI 才能进入该子方案实现

### 第二步：完成 `4` 键 + `1` 旋钮输入方案

状态：`pending`

工作内容：

- 建立少量实体键与旋钮输入方案
- 不再按完整键盘矩阵作为默认主线推进

验收方式：

- [keyboard_matrix_input_plan.md](./keyboard_matrix_input_plan.md) 存在且内容完整

人工检查点：

- 人确认实体按键输入方案可接受后，AI 才能进入该子方案实现

### 第三步：完成 BLE 键盘产品化细化

状态：`pending`

工作内容：

- 收敛 `v1` 的命名、绑定、重连、电量与连接体验
- 收敛 `v1` 的蓝牙稳定性目标和脚本化回归标准

验收方式：

- [ble_keyboard_productization_plan.md](./ble_keyboard_productization_plan.md) 作为当前有效方案保留

人工检查点：

- 人确认当前先做 BLE 产品化主链路可接受后，AI 才能进入该子方案实现

### 第四步：完成日志与自检方案

状态：`pending`

工作内容：

- 建立日志、自检、版本与故障定位方案

验收方式：

- [device_logging_selftest_plan.md](./device_logging_selftest_plan.md) 存在且内容完整

人工检查点：

- 人确认日志与自检方案可接受后，AI 才能进入该子方案实现

### 第五步：后续再单独建立 Windows 主机端语音输入 app 方案

状态：`pending`

工作内容：

- 在当前设备侧音频链路已经稳定后，再单独建立主机端语音输入 / agent app 方案
- 继续沿用：
  - `Type4Me` 主参考
  - `Handy` 次参考

验收方式：

- 新方案存在且重新过审

人工检查点：

- 人确认确实进入主机端实现阶段后，AI 才能进入这些新方案实现

## 当前阻塞项

- 当前只是完成方案梳理，还没有开始任何一个子方案的实现审批
- 当前仍需要把实体输入、默认快捷键映射和主机端 app 边界定义清楚
- 当前仍需要把 BLE 稳定性与测试脚本边界定义清楚

## 备注

- 当前建议的执行顺序是：
  1. `voice_shortcut_keyboard_plan`
  2. `ble_keyboard_productization_plan`
  3. `keyboard_matrix_input_plan`（但边界改成 `4` 键 + `1` 旋钮）
  4. `device_logging_selftest_plan`
  5. 再单独立 `Windows` 主机端语音输入 app 方案

- 其中蓝牙连接稳定性不是附属项，而是：
  - `ble_keyboard_productization_plan` 的必收内容
  - 无线音频上传前的前置门槛

- 其中最适合当前立即推进的是：
  `ble_keyboard_productization_plan`

- 如果后续问题再次回到“Type4Me / Handy 选哪个”，
  当前默认结论是：
  - 主参考选 `Type4Me`
  - 次参考保留 `Handy`

- 如果目标从“BLE 语音键盘 v1”升级为“类似闪电说的嵌入式语音助手”，
  当前这份 roadmap 只覆盖设备侧子集，不覆盖主机伴随程序、上下文理解和技能执行。
