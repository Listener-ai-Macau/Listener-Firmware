# OOBE Smoke 诊断包

生成时间：2026-05-20 19:57 CST

## 诊断包内容

| 文件 | 说明 |
|------|------|
| `diagnostic_package.json` | 结构化诊断数据（版本、BLE 状态、会话统计、错误、配置、时间线） |
| `README.md` | 本文件，人工可读摘要 |

## 最小字段覆盖检查

- [x] app/firmware 版本 → `device_info.firmware_version`, `device_info.protocol_version`
- [x] BLE 状态 → `ble_state.connection_status`, `ble_state.mtu_size`, `ble_state.pairing_state`
- [x] 最近错误 → `recent_errors`（空数组表示无错误）
- [x] 配置状态 → `config_state`
- [x] 时间线 → `timeline`
- [x] 隐私检查 → `privacy_check`（确认无音频原文、无 API key）

## 验收项状态

| 验收项 | 状态 | 证据 |
|--------|------|------|
| A1 基本捕获 | **PASS** | 5.08s WAV, 0 missing chunks |
| A7 长录音 (60s) | **PASS** | 60.14s WAV, 0 missing chunks |
| A12 取消/恢复 | **BLOCKED** → 4.2 | Listener-Type 产品链超时，固件层支持 KEY1 取消 |
| A13 多轮捕获 | **BLOCKED** → 4.2 | Listener-Type 产品链超时，固件层支持多 session |
| Clean Windows OOBE | **DEFERRED** → 5.1 | 需 3.1 Windows 安装包 + 3.5 无开发环境验证 |

## 失败项映射

- **A12/A13 产品链失败** → 步骤 4.2（BLE 自动恢复、背压和诊断增强收尾）
- **Clean Windows OOBE** → 步骤 5.1（无开发环境 OOBE 验收矩阵）
- **Windows BLE/WinRT 不稳定** → 步骤 4.1（无工具恢复与设备状态语言）
