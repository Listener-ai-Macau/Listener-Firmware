# Tests

这里先保留自动测试目录入口。

当前目录规则：

- `tests/capture_ble_latest_16k_mono.wav`
  - 保留最新一份主机侧录音产物
- `tests/capture_ble_latest.log`
  - 保留最新一份主机侧串口 / 接收日志
- `tests/artifacts/`
  - 放自动回归时生成的临时源音频或其它中间产物
  - 这些内容默认不提交

当前已经具备一个最小测试脚本入口：

- `tools/test.ps1`
  - 重新构建工程
  - 检查 `bin`、`bootloader.bin`、`partition-table.bin`

当前阶段最先建议继续补的是：

- BLE HID 手工联调步骤
- 串口日志关键字检查脚本
- 烧录后冒烟检查脚本
