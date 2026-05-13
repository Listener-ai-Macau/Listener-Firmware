# GitHub 上传与 BLE HID 恢复方案

## 目标

把当前本地 `voice-keyboard-firmware` 仓库上传到 `https://github.com/Listener-ai-Macau/voice-keyboard-firmware`，在这台 Windows 机器上补齐 `ESP-IDF` 开发环境，并让 `docs/features/ble_hid_keyboard_input.md` 描述的 BLE HID 键盘输入验证链路重新可执行、可验收。

## 问题

当前仓库没有配置 Git 远端；本机默认路径 `%USERPROFILE%\esp\esp-idf` 下不存在 `ESP-IDF`，`idf.py` 也不可用；当前板子串口枚举为 `COM3`，与现有文档中的 `COM5` 不一致，因此需要先恢复环境，再验证并修正文档与实际行为的差异。

## 范围

- 绑定并验证 GitHub 远端，安全推送当前本地提交历史
- 按仓库脚本配置并验证本机 `ESP-IDF` 环境
- 构建、刷写并验证 `ESP32-S3` 的 BLE HID 键盘输入链路
- 只在必要时做最小代码修复和文档修订，使功能重新跑通

## 不在本次范围内

- 新增真实语音输入链路
- 修改产品路线或 BLE 方案方向
- 引入 `STM32` 端实现
- 处理与本任务无关的历史计划文档整理

## 前提与依赖

- 当前机器对目标 GitHub 仓库具有推送权限
- 当前机器可以联网访问 `GitHub`、`winget` 和 `ESP-IDF` 依赖源
- `ESP32-S3` 开发板可通过 USB 正常连接，当前检测到的串口为 `COM3`
- Windows 主机仍可用于 BLE 配对和键盘输入捕获

## 步骤

### 第一步：上传当前仓库到 GitHub

状态：`completed`

工作内容：

- 确认目标 GitHub 仓库可访问，识别远端是否已有提交历史
- 在本地仓库中配置 `origin` 指向目标 URL
- 根据远端实际状态推送当前本地分支历史，并确认后续默认推送路径

验收方式：

- `git remote -v` 显示目标 GitHub URL
- 推送命令成功返回
- `git ls-remote origin` 可以看到刚上传的分支引用

人工检查点：

- 仅当 GitHub 凭据缺失、远端已存在冲突历史，或需要显式决定远端默认分支命名时再请人确认

### 第二步：配置并验证 ESP-IDF 环境

状态：`completed`

工作内容：

- 运行 `powershell -ExecutionPolicy Bypass -File .\tools\setup_windows.ps1`
- 确认 `%USERPROFILE%\esp\esp-idf` 已存在且 `idf.py --version` 可用
- 确认仓库内 `tools/build.ps1`、`tools/flash.ps1` 可正常加载 `ESP-IDF` 环境

验收方式：

- `tools/setup_windows.ps1` 执行完成且没有报错退出
- `powershell -ExecutionPolicy Bypass -File .\tools\build.ps1` 至少进入 `idf.py` 构建流程
- `idf.py --version` 返回有效版本号

人工检查点：

- 仅当系统安装依赖需要人工登录、系统权限弹窗、或工具链源不可访问时再请人介入

### 第三步：构建并刷写当前 BLE HID 固件

状态：`completed`

工作内容：

- 使用 `ESP32-S3` 目标重新构建当前仓库
- 按当前实际串口 `COM3` 刷写固件
- 抓取启动日志，确认 BLE HID 输入任务和串口脚本输入路径已经启动

验收方式：

- `powershell -ExecutionPolicy Bypass -File .\tools\build.ps1` 成功完成
- `powershell -ExecutionPolicy Bypass -File .\tools\flash.ps1 -Port COM3` 成功完成
- `powershell -ExecutionPolicy Bypass -File .\tools\capture_serial.ps1 -Port COM3 -ResetBeforeRead` 的输出包含 `ble_hid: START`

人工检查点：

- 仅当设备掉线、串口变化、或需要人工按键进入下载模式时再请人介入

### 第四步：按文档链路验证 BLE HID 功能并修复差异

状态：`in_progress`

工作内容：

- 运行 `powershell -ExecutionPolicy Bypass -File .\tools\verify_ble_hid.ps1 -Port COM3 -Text "ab"`
- 如主机蓝牙连接状态允许，再运行 `powershell -ExecutionPolicy Bypass -File .\tools\verify_ble_hid_end_to_end.ps1 -Port COM3 -Text "ab"`
- 如果验证失败，定位到最小必要代码修复并重新构建、刷写、复验
- 若端口、命令、输出标记或约束与现有功能文档不一致，更新 `docs/features/ble_hid_keyboard_input.md`

验收方式：

- `verify_ble_hid.ps1` 输出固件链路通过或明确进入主机未连接分支
- 在主机已连接 `Listener Keyboard` 的前提下，`verify_ble_hid_end_to_end.ps1` 返回成功标记并捕获到期望文本
- 必要时更新后的 `docs/features/ble_hid_keyboard_input.md` 与当前机器和固件行为一致

人工检查点：

- 仅当需要人工重新配对蓝牙、确认系统键盘输入焦点、或观察机器外部行为时再请人介入

## 当前阻塞项

- Windows 当前存在 `Listener Keyboard` 的配对记录，但设备重启后未在 60 秒内自动建立 BLE 连接
- 端到端验证还需要主机侧先进入已连接状态，当前 `verify_ble_hid.ps1` 日志表现为 `connected=no`

## 备注

- 当前仓库头提交为 `321f3bc 调通蓝牙输入功能`
- 本次已经成功完成：
  - 推送当前仓库到 `origin/master`
  - `tools/setup_windows.ps1`
  - `tools/build.ps1`
  - `tools/flash.ps1 -Port COM3`
  - `tools/capture_serial.ps1 -Port COM3 -ResetBeforeRead`
  - `tools/verify_ble_hid.ps1 -Port COM3 -Text "ab"`
- `verify_ble_hid_end_to_end.ps1` 当前未通过，直接原因是主机侧未连接，缺少 `hid_keyboard: send_ascii done`
