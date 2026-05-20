# Voice Keyboard Firmware — Codex / Tai 指令

## 身份

你的身份由启动器注入（环境变量 `AI_AGENT_ID`）。检测到就用作 `assignee`；否则用 `codex` 或 `tai`。

## AI 协作

完整协议见 `C:\Users\Billy\Desktop\listener\docs\ai_collaboration_protocol.md`。

工具脚本：
- 查看可认领步骤：`pwsh -NoProfile -File tools\list_available_steps.ps1 -Plan <plan>`
- 认领步骤并创建/切换分支：`pwsh -NoProfile -File tools\claim_step.ps1 -Plan <plan> -StepId <id> -Assignee <you>`
- 刷新心跳/更新步骤：`powershell -File tools\update_plan_status.ps1 -Plan <plan> -StepId <id> -Status in_progress -Assignee <you> -Touch`
- 完成步骤：`powershell -File tools\update_plan_status.ps1 -Plan <plan> -StepId <id> -Status completed -ValidationResult "PASS: ..."`
- 阻塞步骤：`powershell -File tools\update_plan_status.ps1 -Plan <plan> -StepId <id> -Status blocked -Assignee <you> -BlockedReason "..."`
- 校验状态：`pwsh -NoProfile -File tools\validate_plan_status.ps1`
- 硬件锁：`powershell -File tools\lock_resource.ps1 -Resource COM3 -Owner <you>`
- 释放锁：`powershell -File tools\unlock_resource.ps1 -Resource COM3 -Owner <you>`

## 构建命令

```powershell
idf.py build                    # 构建
idf.py -p COM3 flash            # 烧录
pwsh -NoProfile -File ./tools/esp_idf_ci.ps1 build   # Git Bash CI
```

## 文档位置

- 共享任务状态：`C:\Users\Billy\Desktop\listener\docs\plans\*_status.json`
- 计划参考文档：`C:\Users\Billy\Desktop\listener\docs\plans\*.md`
- 协作协议：`C:\Users\Billy\Desktop\listener\docs\ai_collaboration_protocol.md`
- 仓库级文档：`!docs/features/`
- 人类文档默认中文
