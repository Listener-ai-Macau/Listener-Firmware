# Listener Firmware 项目约定

- 诊断和开发不得随意关闭用户的 Type；必要更新后立即恢复。
- 蓝牙修复须实际使用 nRF52840 Dongle 抓包器，与 Windows HCI、固件日志对齐定位；先确认故障段捕获有效，不把漏抓当设备丢包，不盲调参数。覆盖吞吐、音频、改名、连接、重连、状态同步；已通过的无关项目不重复测。
- ESP-IDF 命令使用 `tools/idf.ps1`；串口控制使用 `tools/send_serial_and_capture.ps1` 并带 `~` 前缀，避免被当作键盘输入。
- 防回退索引沿用配套 Type 的 `scripts/regression-guard-commits.json`；待办与完整时间线沿用 `C:/Users/Billy/Documents/Codex/2026-10-04/listener-repair-handoff/outputs/Listener-修复进度.md`。

- 蓝牙防回退死约束：尝试前先查既有索引和时间线；已否决方案只有实际条件出现新证据才可重试，并写明差异。未触发、未生效或采集不完整必须保留为未验证，不能算修好或提速；当前工作点及时更新，旧失败不删除。
