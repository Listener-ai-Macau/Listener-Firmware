# Tests

这里先保留自动测试目录入口。

当前已经具备一个最小测试脚本入口：

- `tools/test.ps1`
  - 重新构建工程
  - 检查 `bin`、`bootloader.bin`、`partition-table.bin`

当前阶段最先建议继续补的是：

- BLE HID 手工联调步骤
- 串口日志关键字检查脚本
- 烧录后冒烟检查脚本
