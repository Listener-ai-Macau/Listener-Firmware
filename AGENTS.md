# Listener Firmware 项目约定

- 诊断和开发不得随意关闭用户的 Type；必要更新后立即恢复。
- ESP-IDF 命令使用 `tools/idf.ps1`；串口控制使用 `tools/send_serial_and_capture.ps1` 并带 `~` 前缀，避免被当作键盘输入。
- 防回退索引沿用配套 Type 的 `scripts/regression-guard-commits.json`；待办与完整时间线沿用 `C:/Users/Billy/Documents/Codex/2026-10-04/listener-repair-handoff/outputs/Listener-修复进度.md`。
