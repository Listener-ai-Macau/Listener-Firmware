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

### Windows 终端

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

### Git Bash（CI 环境）

`idf.py` 检测到 MSys 会静默退出。用 `pwsh` 调 `esp_idf_ci.ps1` 绕过，直接调底层工具：

```bash
# 构建
pwsh -NoProfile -File ./tools/esp_idf_ci.ps1 build

# 烧录（需要设备连接）
pwsh -NoProfile -File ./tools/esp_idf_ci.ps1 flash -Port COM3

# 擦除 flash
pwsh -NoProfile -File ./tools/esp_idf_ci.ps1 erase-flash -Port COM3

# 串口监视器
pwsh -NoProfile -File ./tools/esp_idf_ci.ps1 monitor -Port COM3

# 固件大小
pwsh -NoProfile -File ./tools/esp_idf_ci.ps1 size

# 切换目标芯片
pwsh -NoProfile -File ./tools/esp_idf_ci.ps1 set-target -Target esp32s3

# 清理 + 重新配置
pwsh -NoProfile -File ./tools/esp_idf_ci.ps1 reconfigure

# 查看所有命令
pwsh -NoProfile -File ./tools/esp_idf_ci.ps1 help
```

## 文档约定

- `C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\docs\plans\` — 跨仓库共享任务状态（JSON 为唯一状态源）和参考文档
- `C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\docs\fixes\` — 跨仓库活跃 bug 修复/调查计划
- `C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\docs\ai_collaboration_protocol.md` — 三体 AI 协作完整协议（所有 AI 读同一个文件）
- `!docs/features/` — 已完成的功能总结（仓库级，留在各自仓库）
- `!docs/product_solutions.md` — 产品范围与架构决策（仅涉及范围时读）
- 人类文档默认中文
- 计划完成后将持久知识移入 `!docs/features/` 并清理 `docs/plans/` 中对应文件

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

- 功能开发、架构变更、板级调试、重要 bug 修复前，先写计划文档到 `C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\docs\plans\` 或 `C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\docs\fixes\`
- 计划的核心价值是**并行分工**和**不重复造轮子**，不是写八股文
- 计划只需包含：
  - **目标**：一句话说清楚要达成什么
  - **Precedent Review**：哪些能直接复用，不要重复造轮子
  - **步骤**：可并行分工的独立步骤，标注 repo 和 depends_on
  - **每步验收**：客观可测试的验收条件和命令
- 不需要写：当前问题、超出范围项、假设与依赖表、涉及的文件列表、人工检查点、已知阻碍
  - 如果某个信息对分工或验收有实际价值，就写；否则不要为了模板而写

### Bug 工作流

- 重要 bug 遵循相同计划流程，计划文档放 `C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\docs\fixes\`
- 计划需包含：观察到的行为、期望行为、复现步骤、疑似范围、根因调查步骤、修复步骤、验收检查
- 仅在用户明确要求快速路径且 bug 显而易见、低风险时才跳过专门计划

### 步骤门控

- 只执行当前步骤，不提前开后续步骤
- 每步完成后跑验收，验收通过即可标记完成
- 不需要人工确认的步骤直接做，只有必须人工操作的才算阻塞（物理操作、人工决策）；推送 origin 先跑推送前审核 gate，通过后直接推送
- 现实变化时更新计划

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

## AI 协作

完整协议见 `C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\docs\ai_collaboration_protocol.md`。
工具脚本在 `C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\scripts\`；本仓库 `tools\*` 协作脚本仅作兼容代理。

## 当前关键约束

- 不要将 `audio_data` 重新解释为旧的 `chunk + fragment` 模型
- 不要改变主机端订阅顺序：`CCCD notify -> ValueChanged`
- 不要移除设备对 `subscribe` 在 `connect` 之前到达的兼容性
- 文档默认中文
- 有活跃计划时，遵循计划而非临时编造下一步

## 上下文恢复阅读顺序

按需取最小集合，优先顺序：

1. `CLAUDE.md`
2. `C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\docs\plans\*_status.json`（当前步骤状态）
3. `C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\docs\ai_collaboration_protocol.md`（多 AI 协作时必读）
4. `README.md`
5. `C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\docs\plans\*.md`（步骤定义和验收标准）
6. `C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\docs\fixes\`（活跃 bug 修复计划）
7. `!docs/features/` 下已完成功能
8. `!docs/product_solutions.md`（仅涉及产品范围时）
9. 源代码（改实现细节时直接读）

## 扩展命令

```powershell
# 串口输入注入
powershell -ExecutionPolicy Bypass -File .\tools\send_serial.ps1 -Port COM3 -Text "abc123"

# BLE 音频采集
python .\tools\capture_audio_ble_wav.py --port COM3 --capture-seconds 5

# 物理 KEY1 音频采集
python .\tools\capture_audio_ble_wav.py --port COM3 --capture-seconds 5 --trigger-mode physical-key --no-reset-before-capture

# BLE 音频产品矩阵验证（A=自动, H=手动）
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --capture-seconds 5 --long-capture-seconds 30 --round-count 3 --idle-seconds 30

# 模拟真实使用场景
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --realistic-usage-profile --random-seed 20260525

# 只跑特定 case
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases A1,A3,A6

# 矩阵 case 说明（按 Bluetooth SIG 分类顺序）
# A1: GAP基线连接 | A2: GATT连续传输 | A3: GAP断连重连 | A4: GAP多轮独立重连
# A5: 主机端恢复 | A6: 空闲后首录 | A7: 取消+恢复 | A8: 静音负向
# A9: 快速启停压力 | A10: 并发BLE客户端
# H1: 物理KEY1（半自动，等按钮后自动继续）
# H2: RF干扰/距离 | H3: 后端ASR集成

# 校验计划状态（所有 AI 完成步骤后跑一遍）
pwsh -NoProfile -ExecutionPolicy Bypass -File .\tools\validate_plan_status.ps1
# 自动修复可修复的问题
pwsh -NoProfile -ExecutionPolicy Bypass -File .\tools\validate_plan_status.ps1 -Fix
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

