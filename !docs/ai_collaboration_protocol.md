# 三体 AI 协作协议

状态：活跃
日期：2026-05-18

## 三个 AI

| 工具 | 模型 | 启动命令 |
|---|---|---|
| Codex CLI | GPT-5.5（OpenAI 直连） | `codex` |
| Codex CLI + `tai` profile | GPT-5.x（TTAPI 代理） | `tai` |
| Claude Code CLI | GLM-5.1 | `claude` |

**所有 AI 角色平等**：拆步骤、干活、审查、测试谁都能做。谁闲谁就做。

## 硬约束（必须遵守）

1. **开工前必须读状态**：`cat C:\Users\Billy\Desktop\listener\docs\plans\*_status.json`，确认没有别人在做同一步骤
2. **认领必须写 JSON**：用 `tools\update_plan_status.ps1` 更新 assignee + status，**必须在开始编码前执行**
3. **真实硬件操作必须按资源加锁**：只有占用真实硬件的步骤需要锁；纯文档、静态检查、编译不需要。用到 `COM3` 锁 `COM3`，用到 `BLE` 锁 `BLE`，用完分别释放
4. **完成必须更新 JSON**：验收通过后标记 completed，清空 assignee
5. **阻塞必须写明原因**：`blocked_reason` 字段

违反以上约束导致冲突的，由冲突方负责修复。

## 工作流

```
1. 任务到达 → 任一 AI 或人类拆成可并行的独立步骤，写入 docs/plans/
2. 人类可以分配步骤，或者谁空闲谁认领
3. 【硬约束】认领后立即 update_plan_status.ps1 标记 assignee + in_progress
4. 在当前仓库开任务 branch 做；不创建 worktree
5. 验收通过才算完成；验收不通过就继续修
6. 需要人工的步骤标记 blocked + blocked_reason
7. 不需要人工确认的步骤直接做，不等待审批
8. 推送 origin 不再默认要求人工确认；执行推送前审核 gate，通过后直接推送
```

## 身份识别

启动时先看启动器注入的身份，再用工作目录路径校验：

- `tai` 入口（`C:\Users\Billy\.codex\!ttapi\tai.ps1`）会给普通会话注入 Tai 身份 prompt，并设置进程环境变量：
  - `AI_AGENT_ID=Tai`
  - `AI_AGENT_ROLE=implementation-worker`
  - `TAI_AGENT_ID=Tai`
  - `TAI_PROFILE=tai`
  - `TAI_LAUNCH_COMMAND=tai`
- `tai --no-identity-prompt` 只用于调试，不作为日常协作入口
- `~/.codex/config.toml` 的 `[profiles.tai]` 只负责模型 provider，不负责身份 prompt

如果启动器身份和工作目录冲突，先停止改代码，切到正确 branch 后再继续。

## 任务文档

- **共享任务状态**：`C:\Users\Billy\Desktop\listener\docs\plans\*_status.json`
  - JSON 是唯一状态源，没有第二个
  - 所有 AI 无论在哪个仓库工作，都读这个路径
- **参考文档**：`C:\Users\Billy\Desktop\listener\docs\plans\*.md`
  - 只放步骤定义、验收标准、架构说明，不放状态
- **各仓库 `!docs/`**：features、product_solutions 等仓库级文档继续放各自仓库

## 计划格式（硬约束）

任何 AI 写计划时必须遵守以下格式，确保步骤可并行分工：

### JSON 状态格式

```json
{
  "plan": "<plan_name>",
  "updated_at": "...",
  "phases": [
    {
      "id": 1,
      "title": "阶段名",
      "status": "completed | in_progress | pending",
      "steps": [
        {
          "id": "1.1",
          "title": "步骤名",
          "repo": "firmware | Listener-Type | firmware+Listener-Type",
          "status": "pending | in_progress | completed | blocked",
          "assignee": null,
          "parallel_group": "A"
        }
      ]
    }
  ]
}
```

### 关键规则

1. **分阶段（phases）**：不同阶段串行，同阶段内步骤尽量并行
2. **parallel_group**：同组可同时做，不同组互不依赖，同组内不能改同一文件
3. **只追踪未完成**：已完成阶段折叠为一行 summary，不展开步骤
4. **步骤粒度**：单次会话可完成并验收的大小
5. **repo 标注**：每个步骤必须标注涉及哪个仓库

## Branch 协作

直接在当前仓库开 branch，不建 worktree：

- 开工：从状态 JSON 里的 `base_branch` 或当前目标分支创建 `ai/<task>` / `task/<description>`
- 分支命名：`ai/<task>` 或 `task/<description>`
- 只在任务 branch 上改动；不要创建 worktree
- 做完后按计划要求 merge/cherry-pick/PR 回目标分支，再删 branch
- **冲突靠 JSON 状态、`write_paths` 和文件边界避免，不靠物理隔离**

## 硬件资源锁

共享硬件（`COM3`、`BLE`）通过 `tools/lock_resource.ps1` / `tools/unlock_resource.ps1` 互斥。

- 只有会占用真实硬件的命令才需要锁：`flash`、`monitor`、串口输入/抓取、BLE capture、BLE matrix、真实设备验证
- 纯文档、静态检查、代码搜索、`python -m compileall`、不接触设备的构建不需要锁
- `lock_resource.ps1` 当前一次只锁一个 `-Resource`；同时用到 `COM3` 和 `BLE` 时分别调用两次
- 用前 `lock_resource`，用完 `unlock_resource`
- 示例：
  - `powershell -File .\tools\lock_resource.ps1 -Resource COM3 -Owner <you>`
  - `powershell -File .\tools\lock_resource.ps1 -Resource BLE -Owner <you>`
- 进程退出后 60 分钟自动过期
- **同一时间只有一个 AI 操作硬件**

## 阻塞规则

- 只有必须人工执行的操作才算阻塞：物理操作、人工决策
- 推送 origin 是自动执行步骤：必须先完成推送前审核 gate（状态、分支、待推提交、未提交改动、基础验证、secret scan），审核通过后直接推送
- 其他所有步骤直接执行，不等待审批
- 验收不通过不算完成，继续修直到通过
- 阻塞时在 status JSON 标记 `blocked` + `blocked_reason`

## 汇报格式

任何 AI 汇报时必含：

```
1. 当前状态：每个步骤做到哪了（in_progress / completed / pending / blocked）
2. 阻塞点：是否有、是什么
3. 下一步：还有什么步骤没做
```

## 任务分配

任务由人工分配：
- 直接告诉某个 AI 做哪个步骤
- 或者在 status JSON 中设置 `assignee`，空闲 AI 看到 unassigned 的步骤可以认领

## Tai 的额外能力

`tai` 命令（`~/.codex\!ttapi\tai.ps1`）自带身份认证、会话日志、brief note、模型切换、非交互模式。

## 旧模式归档

旧的 Codex executor 模式已归档到 `!docs/archive/`。
`claude_execute.ps1` 保留为工具脚本，不再作为指挥模式入口。
