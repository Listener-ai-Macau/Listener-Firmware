# Voice Keyboard Firmware — 项目约定

## 仓库定位

- ESP32-S3 固件项目，构建系统 ESP-IDF + CMake
- 当前目标：BLE HID 键盘 + 语音采集上传
- 未来约束：上层代码保持 STM32 可移植性

## 目录与命名

- `main/` 只放 `app_main()` 和初始化调度，保持精简
- `components/` 放跨平台产品逻辑
- `protocols/` 放协议、编解码、消息结构、错误码
- `drivers/` 放语义化外设驱动
- `ports/esp32/` 放 ESP-IDF 绑定，`ports/stm32/` 预留给未来
- 不新建 `services/`、`platform/`、`common/`、`misc/` 等顶级目录
- 目录、文件、函数、变量统一 `snake_case`
- 公开函数加模块前缀：`keyboard_start()`、`ble_hid_init()`、`audio_capture_start()`

## 平台边界

- 上层避免直接调 ESP-IDF API，IDF 细节收在 `ports/esp32/`
- 移植到 STM32 时只换 `ports/stm32/*` 和部分 `drivers/*`，不动 `components/*` 和 `protocols/*`

## 构建命令

```powershell
# 新机初始化
powershell -ExecutionPolicy Bypass -File .\tools\setup_windows.ps1

# 构建
idf.py build
# 或
powershell -ExecutionPolicy Bypass -File .\tools\build.ps1

# 烧录
idf.py flash
# 或
powershell -ExecutionPolicy Bypass -File .\tools\flash.ps1 -Port COM3

# 非交互式串口抓取（替代 idf.py monitor）
powershell -ExecutionPolicy Bypass -File .\tools\capture_serial.ps1 -Port COM3 -ResetBeforeRead
```

## 文档约定

- `!docs/plans/` — 活跃的功能/实现计划
- `!docs/fixes/` — 活跃的 bug 修复/调查计划
- `!docs/features/` — 已完成的功能总结
- `!docs/product_solutions.md` — 产品范围与架构决策（仅涉及范围时读）
- 人类文档默认中文
- 计划完成后将持久知识移入 `!docs/features/` 并删除已完成的计划

## 工作流原则

### 工作流级别

- `full`：新功能、架构变更、板级调试、硬件集成、安全关键变更、重要 bug — 需完整计划文档、逐步人工审批、完成总结
- `standard`：常规功能开发、中等风险 bug — 需计划文件、逐步验收、步骤边界人工审批
- `fast`：小改动、低风险、局部修复，且用户明确要求快速路径 — 计划可内嵌在对话中，仍需定义验收
- 涉及硬件行为、架构、跨模块集成时默认 `full`

### 官方优先

- 能直接复用官方示例、官方 API、平台内置能力、已有仓库机制、成熟社区工具的，优先复用
- 只有当官方路径无法覆盖产品语义时，才在上层补自定义逻辑
- 自定义层尽量薄、尽量局部

### 计划驱动

- 功能开发、架构变更、板级调试、重要 bug 修复前，先写计划文档到 `!docs/plans/` 或 `!docs/fixes/`
- 计划需包含：目标、当前问题、范围、超出范围项、假设与依赖、步骤、每步验收、人工检查点、已知阻碍
- 写计划前先做 precedent review：是否已有官方/成熟方案可直接复用
- 计划文档聚焦自定义、有风险、需决策的部分，官方路径简要提及即可
- 每步需明确：涉及哪些文件/脚本、是固件/脚本/文档/混合、具体验收命令或产物

### Bug 工作流

- 重要 bug 遵循相同计划流程，计划文档放 `!docs/fixes/`
- 计划需包含：观察到的行为、期望行为、复现步骤、疑似范围、根因调查步骤、修复步骤、验收检查
- 仅在用户明确要求快速路径且 bug 显而易见、低风险时才跳过专门计划

### 步骤门控

- 只执行当前批准的步骤
- 每步完成后跑验收、报结果、等批准再进下一步
- 不静默继续后续步骤
- 现实变化时更新计划并重新获批

### 验收设计

- 每步验收必须客观可测试
- 优先：构建成功、烧录成功、日志含特定标记、测试通过、产物存在
- 避免"看起来可以"、"基本完成"等模糊描述

### 验证超时

- 执行前声明预期时间预算，优先用显式命令超时
- 超时即终止并报告：哪个命令、预期多久、卡在什么阶段、为何不值得继续等、下一步诊断建议
- 不要把"还在跑"当作健康的证据

### 不越权

- 不擅自扩大范围、改架构方向、提前开后续步骤、混入无关清理
- 允许：实现当前步骤、收紧细节、记录新发现、提议计划变更

### 交互规则

- 当用户问"接下来做什么"时，从已批准计划回答：当前步骤状态、验收结果、下一个阻塞/非阻塞检查点
- 已有批准计划时不切换到临时任务列表

### 完成文档

- 重要功能或 bug 完成后，在 `!docs/features/` 写简要交接文档：做了什么、代码在哪、如何验证、重要约束、已知风险和可能的下一步
- 完成后将持久知识移入 `!docs/features/`，删除已完成的活跃计划

## 执行责任划分

**AI 默认自己动手**：

- 构建、烧录、日志抓取、结果分析、迭代修复都自己做
- 优先用命令行验证，不把可执行步骤推回给人类
- 只有真正需要物理操作（USB 插拔、按 BOOT/RESET、BLE 配对、外部设备响应）时才请人介入

**人类负责决策**：

- 批准计划、批准步骤推进、产品/架构决策、最终验收

## 当前关键约束

- 不要将 `audio_data` 重新解释为旧的 `chunk + fragment` 模型
- 不要改变主机端订阅顺序：`CCCD notify -> ValueChanged`
- 不要移除设备对 `subscribe` 在 `connect` 之前到达的兼容性
- 文档默认中文
- 有活跃计划时，遵循计划而非临时编造下一步

## 上下文恢复阅读顺序

按需取最小集合，优先顺序：

1. `CLAUDE.md`
2. `README.md`
3. `!docs/README.md`
4. `!docs/plans/` 下当前活跃计划
5. `!docs/features/` 下已完成功能
6. `!docs/product_solutions.md`（仅涉及产品范围时）
7. 源代码（改实现细节时直接读）

## 扩展命令

```powershell
# 串口输入注入
powershell -ExecutionPolicy Bypass -File .\tools\send_serial.ps1 -Port COM3 -Text "abc123"

# BLE 音频采集
python .\tools\capture_audio_ble_wav.py --port COM3 --capture-seconds 5

# 物理 KEY1 音频采集
python .\tools\capture_audio_ble_wav.py --port COM3 --capture-seconds 5 --trigger-mode physical-key --no-reset-before-capture

# BLE 音频产品矩阵验证
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --capture-seconds 5 --long-capture-seconds 30 --round-count 3 --idle-seconds 30 --soak-round-count 5

# 模拟真实使用场景
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --realistic-usage-profile --random-seed 20260525

# P1 标准 BLE 音频回归
python .\tools\verify_audio_ble_upload_end_to_end.py --port COM3 --capture-seconds 5 --no-reset-before-capture
```

## 交付物

完成任务时报告：
- 哪条仓库规则最重要
- 更新了哪些文档
- 变更对 ESP32/STM32 可移植性的影响

## 脚本互操作（Windows）

- PowerShell 调 Python 时，动态内容不直接拼进源码字符串
- 用 base64 编码、JSON 序列化、命令行参数、环境变量、临时文件传递动态内容
- Windows 路径用 `pathlib.Path`、raw string `r"..."`、或正斜杠，避免 `\U` `\n` `\t` 误解析
- 文档中优先用仓库相对路径
