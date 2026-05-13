# 文档目录

这个目录用于存放当前仓库所有面向人阅读的说明文档。

当前约定是：

- 顶层只保留这一份 `README.md`
- `!docs` 子目录内部不再单独保留 `README.md`
- 活跃方案放到 `plans/`
- 已完成能力沉淀到 `features/`
- 板级原理图和截图资料放到 `hardware/`
- 暂不推进但保留参考价值的内容放到 `parking_lot/`

## 当前目录说明

- `ai_development_workflow.md`
  仓库内 AI 开发流程、计划与验收规则
- `development_setup.md`
  新机器环境、`build / flash` 和串口使用说明
- `plans/`
  只保留当前真的在推进的 active plans
- `features/`
  已完成功能或稳定子系统总结
- `hardware/`
  板级原理图 PDF 和相关截图资料
- `parking_lot/`
  暂不推进但保留参考的材料
- `product_solutions.md`
  当前产品方向和边界说明

## 当前推荐入口

- `BLE HID` 键盘 fallback / 验证链路：
  [features/ble_hid_keyboard_input.md](./features/ble_hid_keyboard_input.md)
- 设备端音频采集、录音控制与 `BLE` 上传链路：
  [features/audio_capture_ble_upload.md](./features/audio_capture_ble_upload.md)
- 当前仍在推进的 active plans：
  [plans](./plans)
- 当前板级原理图资料：
  [hardware/esp32s3_board_v1](./hardware/esp32s3_board_v1)
