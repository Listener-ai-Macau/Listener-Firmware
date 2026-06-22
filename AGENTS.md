# Voice Keyboard Firmware — Codex / Tai 指令

## 身份

你的身份由启动器注入（环境变量 `AI_AGENT_ID`）。检测不到时，显式传 `-Assignee` / `-Owner`，不要退回 Windows 用户名。

## AI 协作

公共 workflow 仓库：

`C:\Users\Billy\Desktop\Denzic\ai-collaboration-workflow`

完整协议见：

`C:\Users\Billy\Desktop\Denzic\ai-collaboration-workflow\docs\ai_collaboration_protocol.md`

共享计划状态：

`C:\Users\Billy\Desktop\Denzic\ai-collaboration-workflow\docs\plans\*_status.json`

本仓库不维护协作脚本；协作命令一律通过公共 workflow 仓库的 `scripts\aiw.ps1` 调用。

常用命令：

```powershell
$aiw = "..\..\ai-collaboration-workflow\scripts\aiw.ps1"
pwsh -NoProfile -File $aiw status
pwsh -NoProfile -File $aiw list -Plan <plan> -IncludeStale
pwsh -NoProfile -File $aiw claim -Plan <plan> -StepId <id> -Assignee <you> -RepoRoot .
pwsh -NoProfile -File $aiw touch -Plan <plan> -StepId <id> -Assignee <you>
pwsh -NoProfile -File $aiw submit -Plan <plan> -StepId <id> -ValidationResult "PASS: ..." -RepoRoot .
pwsh -NoProfile -File $aiw review -Plan <plan> -StepId <id> -Reviewer <reviewer> -Result approved -Notes "<what was checked>"
pwsh -NoProfile -File $aiw block -Plan <plan> -StepId <id> -Assignee <you> -BlockedReason "..."
pwsh -NoProfile -File $aiw lock -Resource COM3 -Owner <you>
pwsh -NoProfile -File $aiw unlock -Resource COM3 -Owner <you>
```

`submit` 只表示提交复核，状态为 `review`；只有 `review -Result approved` 才会写入 `accepted` 并解锁依赖。若需要返工，用 `review -Result changes_requested -Notes "..."`。

## 构建命令

```powershell
idf.py build
idf.py -p COM3 flash
pwsh -NoProfile -File ./tools/esp_idf_ci.ps1 build
```

## 文档位置

- 协作状态/计划：`C:\Users\Billy\Desktop\Denzic\ai-collaboration-workflow\docs\plans\`
- 协作修复计划：`C:\Users\Billy\Desktop\Denzic\ai-collaboration-workflow\docs\fixes\`
- 仓库级功能文档：`!docs/features/`
- 人类文档默认中文
