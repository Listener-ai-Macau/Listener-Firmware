# 文档目录

这个目录用于存放当前仓库所有面向人阅读的说明文档。

当前约定是：

- 顶层只保留这一份 `README.md`
- `!docs` 子目录内部不再单独保留 `README.md`
- 活跃方案放到 `plans/`
- 当前正在处理的 bug fix 放到 `fixes/`
- 已完成能力沉淀到 `features/`
- 板级原理图资料放到 `schematics/`

## 当前目录说明

- `development_setup.md`
  新机器环境、`build / flash` 和串口使用说明
- `plans/`
  只保留当前真的在推进的 active plans
- `fixes/`
  只保留当前真的在推进的 bug fix / bug 调查文档
- `features/`
  已完成功能或稳定子系统总结
- `schematics/`
  板级原理图 PDF 和必要参考资料
- `product_solutions.md`
  当前产品方向和边界说明

## 当前推荐入口

- `v1` 产品需求与下一步主线：
  [plans/voice_shortcut_keyboard_plan.md](./plans/voice_shortcut_keyboard_plan.md)
- `BLE HID` 键盘 fallback / 验证链路：
  [features/ble_hid_keyboard_output.md](./features/ble_hid_keyboard_output.md)
- 设备端音频采集、录音控制与 `BLE` 上传链路：
  [features/audio_capture_ble_upload.md](./features/audio_capture_ble_upload.md)
- 面向后端联调的语音输入契约：
  [features/voice_input_backend_contract.md](./features/voice_input_backend_contract.md)
- 当前仍在推进的 active plans：
  [plans](./plans)
- 当前仍在推进的 bug fixes：
  [fixes](./fixes)
- 当前板级原理图资料：
  [schematics/esp32s3_board_v1](./schematics/esp32s3_board_v1)
