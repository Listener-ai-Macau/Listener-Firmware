# OOBE Smoke 诊断包 v1.1

生成时间：2026-05-20 20:10 CST

## 诊断包内容

| 文件 | 说明 |
|------|------|
| `diagnostic_package.json` | 结构化诊断数据（设备、App、BLE、会话、错误、配置、时间线） |
| `README.md` | 本文件，人工可读摘要 |

## 最小字段覆盖检查

### 固件侧
- [x] firmware 版本 → `device_info.firmware_version`, `device_info.protocol_version`
- [x] BLE 状态 → `ble_state.connection_status`, `ble_state.mtu_size`, `ble_state.pairing_state`
- [x] 最近错误 → `recent_errors`（空数组表示无错误）
- [x] 配置状态 → `config_state`
- [x] 时间线 → `timeline`

### App 侧
- [x] app 版本 → `app_info.app_version` (1.3.3), `app_info.app_lib_version` (3.6.3)
- [x] app 平台 → `app_info.platform` (Windows)
- [x] app 仓库状态 → `app_info.repo_branch`, `app_info.repo_commit`
- [x] app OOBE 时间线 → `app_oobe_timeline`（6 步，26 秒，2 个已知失败点）

### 隐私
- [x] 隐私检查 → `privacy_check`（确认无音频原文、无 API key、无 PII）

## 验收项状态

| 验收项 | 状态 | 证据 |
|--------|------|------|
| A1 基本捕获 | **PASS** | 5.08s WAV, 0 missing chunks |
| A7 长录音 (60s) | **PASS** | 60.14s WAV, 0 missing chunks |
| A12 取消/恢复 | **BLOCKED** → 4.2 | Listener-Type 产品链超时，固件层支持 KEY1 取消 |
| A13 多轮捕获 | **BLOCKED** → 4.2 | Listener-Type 产品链超时，固件层支持多 session |
| Clean Windows OOBE | **DEFERRED** → 5.1 | 需 3.1 Windows 安装包 + 3.5 无开发环境验证 |

## App 侧 OOBE 时间线

| 步骤 | 动作 | 耗时 | 状态 |
|------|------|------|------|
| 1 | 启动 app | 5s | PASS |
| 2 | BLE 扫描/连接 | 10s | PASS |
| 3 | 首次捕获触发 | 3s | PASS |
| 4 | 音频流接收 | 5s | PASS |
| 5 | ASR 转写 | 2s | CONDITIONAL_PASS* |
| 6 | 文本插入 | 1s | PASS |

**总计**: ~26 秒

*CONDITIONAL_PASS: 需要有效 API key；无 key 时走离线 demo 回退（2.3 已交付）

## 已知失败点

| 步骤 | 失败 | 频率 | 缓解措施 | 映射 |
|------|------|------|----------|------|
| 2 | BLE 扫描超时 | 间歇性 | 重试扫描或手动选择 | 4.1 |
| 5 | ASR API 错误/无 key | 新用户预期 | Demo 内容回退 | 2.3 |
