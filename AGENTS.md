# Voice Keyboard Firmware - Agent Guide

This repository contains ESP32-S3 firmware for the Listener voice keyboard.

## 开发优先硬约束（2026-10-07）

- 功能开发和用户实际故障优先；不得用冒烟测试、全量检查、验收框架、目标台账、审批表或文档整理替代开发，也不得将它们单独报为修复进度。
- 每次修复都从实际现象追查相关完整数据流、状态流和职责边界，找到首次错误决策及其上游成因，确认根因和受影响范围，再修产生问题的源头。不得止于现象位置，也不得在根因未明时靠阈值、延时、特例或回退遮住症状。
- 修复范围由根因决定，不限制为最小补丁、最少文件或最少代码。根因涉及架构、跨模块状态所有权、生命周期、协议契约或数据模型时，允许并应当进行必要的架构修复；范围须与根因有直接关系。
- 验证必须足以证明根因被消除，并覆盖原始现象、同源触发条件、受影响链路及必须保住的正常行为。不得只验证单个样例、局部补丁或为了通过而写的断言；架构修复按其影响范围验证。检查必须回答具体根因问题或交付风险，无关检查不运行。
- 已通过且仍适用于当前实现的检查结果复用；新改动使结果失效、出现新故障或上次失败需要复核时，再做对应验证。禁止每轮、每次连接或更换任务名就重复冒烟、全套矩阵、断线重连和电源验收。
- 编译、刷写/OTA及功能实机验证按修复范围执行。全量验收在用户明确要求全面验收、准备正式发布或架构修复确实影响整体行为时按需要执行，不把固定行政清单当作开发前提。纯文档、规则或进度修改不触发编译、刷写、设备重启或测试。
- AIW 合同、归档、目标管理和行政验收不是开发前提。复用现有进度文档，只记实际完成时间、功能变化、验证结果和剩余问题；不另造管理流程，不用检查通过冒充实际修复。
- 本节优先于本文及引用文档中的旧最小补丁限制和验证流程；下文命令是按根因及影响范围选择的工具，不是每轮整套执行的门槛。

## Project Map

- `main/` keeps `app_main()` thin.
- `components/` contains cross-platform product logic.
- `protocols/` contains protocol definitions, codecs, and error models.
- `ports/esp32/` contains ESP-IDF bindings.
- `tools/` contains build, flash, monitor, diagnostic, and validation helpers.
- `docs/` contains stable product, feature, maintenance, and release documentation.

## Build

```powershell
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
pwsh -NoProfile -File .\tools\monitor.ps1 -Port COMx
```

Do not run bare `idf.py` in this repo. Use `pwsh -NoProfile -File .\tools\idf.ps1 <idf.py args>`
for ad hoc ESP-IDF commands so `tools\idf_env.ps1` always loads `export.ps1` first and avoids
`No module named 'esp_idf_monitor'` from a non-IDF shell.

The project uses ESP-IDF component manager dependencies from
`idf_component.yml` files and `dependencies.lock`. Do not commit
`managed_components/` or `build/`.

## Constraints

- Keep product logic portable: ESP-IDF details stay under `ports/esp32/`.
- Do not reinterpret `audio_data` as the older `chunk + fragment` model.
- Keep the host subscription order `CCCD notify -> ValueChanged`.
- Keep subscribe-before-connect compatibility.
- Use `snake_case` for files, functions, and variables.

## Product delivery

- Follow the user's current scope and this repository's product policies. AIW contracts, archives and blocking acceptance popups are not development prerequisites.
- Preserve existing uncommitted work. Use focused behavioral checks during development; build the final candidate and deliver a matching OTA package.
- Respect the requested OTA transport. Verify the running firmware build identity after update, using serial or BLE diagnostics, and record the matching Type identity.
- Static checks and a successful build do not establish physical power-off, wake reliability or audio quality. Report real-device evidence and remaining gaps separately.

### Always-latest runtime gate (mandatory)

- 设备验收只允许使用当前 active `Listener-Firmware` 构建刷写的固件；不得把旧 OTA、旧 build 或历史串口日志当作当前版本证据。
- 刷写后必须核对串口 `Compile time`/固件版本与本次构建产物一致；Type 应同时满足其 Always-latest runtime gate。

## Validation

Choose checks sufficient to establish the causal fix and cover affected capture, protocol, lifecycle and host behavior. Expand validation when architecture changes affect those boundaries; reuse evidence that remains applicable:

```powershell
pwsh -NoProfile -File .\tools\build.ps1
python .\tools\verify_audio_ble_product_matrix.py --list-cases
pwsh -NoProfile -File .\tools\capture_serial.ps1 -Port COMx -ResetBeforeRead
```

Do not commit generated validation logs, serial captures, diagnostic packages,
build outputs, or firmware binaries.

- 2026-10-15 已确认停止/取消与底层采集完成竞态：接口报错但采集已不活跃时，不得先直接置Idle而绕过完整收尾。错误路径、同步取消与异步完成共用SESSION_INACTIVE终态所有者，结算原会话/ensure、诊断、录音输出/LED及主机语音保护后才能释放电源blocker；仍有真实采集时不得伪造结束。错误日志须在可能启动后继的终态清理之前完成，避免污染新会话状态。
