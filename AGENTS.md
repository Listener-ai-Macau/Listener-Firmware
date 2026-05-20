# Voice Keyboard Firmware — Codex / Tai 指令

## 身份

你的身份由启动器注入（环境变量 `AI_AGENT_ID`）。检测不到时，显式传 `-Assignee` / `-Owner`，不要退回 Windows 用户名。

## AI 协作

公共 workflow 仓库：

`C:\Users\Billy\Desktop\listener\ai-collaboration-workflow`

完整协议见：

`C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\docs\ai_collaboration_protocol.md`

共享计划状态：

`C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\docs\plans\*_status.json`

本仓库不维护协作脚本；协作命令一律从公共 workflow 仓库调用。

常用命令：

```powershell
$wf = "..\ai-collaboration-workflow\scripts"
pwsh -NoProfile -File "$wf\list_available_steps.ps1" -Plan <plan> -IncludeStale
pwsh -NoProfile -File "$wf\claim_step.ps1" -Plan <plan> -StepId <id> -Assignee <you> -RepoRoot . -BaseBranch master
pwsh -NoProfile -File "$wf\update_plan_status.ps1" -Plan <plan> -StepId <id> -Status in_progress -Assignee <you> -Touch
pwsh -NoProfile -File "$wf\complete_step.ps1" -Plan <plan> -StepId <id> -ValidationResult "PASS: ..." -RepoRoot .
pwsh -NoProfile -File "$wf\update_plan_status.ps1" -Plan <plan> -StepId <id> -Status blocked -Assignee <you> -BlockedReason "..."
pwsh -NoProfile -File "$wf\lock_resource.ps1" -Resource COM3 -Owner <you>
pwsh -NoProfile -File "$wf\unlock_resource.ps1" -Resource COM3 -Owner <you>
```

## 构建命令

```powershell
idf.py build
idf.py -p COM3 flash
pwsh -NoProfile -File ./tools/esp_idf_ci.ps1 build
```

## 文档位置

- 协作状态/计划：`C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\docs\plans\`
- 协作修复计划：`C:\Users\Billy\Desktop\listener\ai-collaboration-workflow\docs\fixes\`
- 仓库级功能文档：`!docs/features/`
- 人类文档默认中文
