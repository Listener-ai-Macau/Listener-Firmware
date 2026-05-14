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

### 官方优先

- 能直接复用官方示例、官方 API、平台内置能力、已有仓库机制、成熟社区工具的，优先复用
- 只有当官方路径无法覆盖产品语义时，才在上层补自定义逻辑
- 自定义层尽量薄、尽量局部

### 计划驱动

- 功能开发、架构变更、板级调试、重要 bug 修复前，先写计划文档到 `!docs/plans/` 或 `!docs/fixes/`
- 计划需包含：目标、当前问题、范围、步骤、验收方式、人工检查点
- 写计划前先做 precedent review：是否已有官方/成熟方案可直接复用
- 计划文档聚焦自定义、有风险、需决策的部分，官方路径简要提及即可

### 步骤门控

- 只执行当前批准的步骤
- 每步完成后跑验收、报结果、等批准再进下一步
- 不静默继续后续步骤
- 现实变化时更新计划并重新获批

### 验收设计

- 每步验收必须客观可测试
- 优先：构建成功、烧录成功、日志含特定标记、测试通过、产物存在
- 避免"看起来可以"、"基本完成"等模糊描述

### 不越权

- 不擅自扩大范围、改架构方向、提前开后续步骤、混入无关清理
- 允许：实现当前步骤、收紧细节、记录新发现、提议计划变更

## 执行责任划分

**AI 默认自己动手**：

- 构建、烧录、日志抓取、结果分析、迭代修复都自己做
- 优先用命令行验证，不把可执行步骤推回给人类
- 只有真正需要物理操作（USB 插拔、按 BOOT/RESET、BLE 配对、外部设备响应）时才请人介入

**人类负责决策**：

- 批准计划、批准步骤推进、产品/架构决策、最终验收

## 脚本互操作（Windows）

- PowerShell 调 Python 时，动态内容不直接拼进源码字符串
- 用 base64 编码、JSON 序列化、命令行参数、环境变量、临时文件传递动态内容
- Windows 路径用 `pathlib.Path`、raw string `r"..."`、或正斜杠，避免 `\U` `\n` `\t` 误解析
- 文档中优先用仓库相对路径
