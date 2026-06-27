# 文档目录

这个目录用于存放当前仓库所有面向人阅读的稳定说明文档。

当前约定是：

- 当前仓库只保留稳定产品、开发和硬件文档。
- 本地任务计划、执行日志和验证产物不进入公开仓库。
- 已完成能力沉淀到 `features/`。
- 板级原理图资料放到 `schematics/`。

## 当前目录说明

- `development_setup.md`
  新机器环境、`build / flash` 和串口使用说明
- `features/`
  已完成功能或稳定子系统总结
- `schematics/`
  板级原理图 PDF 和必要参考资料
- `product_solutions.md`
  当前产品方向和边界说明

## 当前推荐入口

- `BLE HID` 键盘 fallback / 验证链路：
  [features/ble_hid_keyboard_output.md](./features/ble_hid_keyboard_output.md)
- 设备端音频采集、录音控制与 `BLE` 上传链路：
  [features/audio_capture_ble_upload.md](./features/audio_capture_ble_upload.md)
- 面向后端联调的语音输入契约：
  [features/voice_input_backend_contract.md](./features/voice_input_backend_contract.md)
- 当前板级原理图资料：
  [schematics/esp32s3_board_v1](./schematics/esp32s3_board_v1)

