# 文档目录

这个目录用于存放当前仓库所有面向人阅读的稳定说明文档。

当前约定是：

- 活跃计划和修复统一放到共享目录：
  `C:\Users\Billy\Desktop\listener\docs\plans\`
  `C:\Users\Billy\Desktop\listener\docs\fixes\`
- 当前仓库不再保留仓库级任务入口目录。
- 已完成能力沉淀到 `features/`。
- 板级原理图资料放到 `schematics/`。
- 有长期价值的历史证据和硬件决策记录保留在 `archive/`。

## 当前目录说明

- `development_setup.md`
  新机器环境、`build / flash` 和串口使用说明
- `features/`
  已完成功能或稳定子系统总结
- `schematics/`
  板级原理图 PDF 和必要参考资料
- `archive/`
  已完成功能证据历史和仍有价值的硬件决策记录
- `product_solutions.md`
  当前产品方向和边界说明

## 当前推荐入口

- 当前协作任务、计划和状态：
  `C:\Users\Billy\Desktop\listener\docs\plans\`
- 当前修复方案：
  `C:\Users\Billy\Desktop\listener\docs\fixes\`
- `BLE HID` 键盘 fallback / 验证链路：
  [features/ble_hid_keyboard_output.md](./features/ble_hid_keyboard_output.md)
- 设备端音频采集、录音控制与 `BLE` 上传链路：
  [features/audio_capture_ble_upload.md](./features/audio_capture_ble_upload.md)
- 面向后端联调的语音输入契约：
  [features/voice_input_backend_contract.md](./features/voice_input_backend_contract.md)
- 历史证据和硬件决策记录：
  [archive](./archive)
- 当前板级原理图资料：
  [schematics/esp32s3_board_v1](./schematics/esp32s3_board_v1)
