# 双 Agent Worktree 协作流程

## 目标

默认协作模型不是“Codex 做完后 Claude 审”，而是“开局拆任务、两个 agent 分别在独立 worktree 推进”。Codex 负责难路径、架构判断、风险控制和最终集成；Claude 负责简单、低耦合、可独立验收的子任务。

旧 `claude_execute.ps1` executor 保留为 legacy evidence tool，用于 native pipeline、resource lock、长日志压缩和失败诊断，不再作为默认 reviewer。

## 适用场景

- 一个任务能按文件或职责清楚拆开。
- Codex 的下一步不依赖 Claude 的结果，可以继续推进核心路径。
- Claude 的交付物能独立检查，例如 docs、测试、fixture、host-side 小工具、grep survey、日志摘要。
- 两边的改动能通过 Codex 做一次最终集成和验证。

不适用场景：

- 两个 agent 需要同时改同一个文件。
- 架构决策、BLE/audio/protocol 不变量或硬件 gate 结论还没定。
- Claude 的结果会阻塞 Codex 的立即下一步。
- 任务太小，Codex 直接读/改/测更快。

## 角色分工

Codex：

- 负责核心固件、协议、端口层、硬件 gate、风险判断。
- 维护任务计划、拆分边界和最终验收标准。
- 集成 Claude worktree 的 diff，处理冲突，重跑验证。
- 对最终行为负责。

Claude：

- 负责明确分配的低耦合文件或产物。
- 优先做中文文档、测试、fixture、host-side helper、机械性重复编辑、窄范围 grep survey。
- 只在自己的 worktree 内改动，不碰未分配文件。
- 输出变更摘要、验证结果和需要 Codex 注意的风险。

## Worktree 规则

- 每个 agent 使用独立 worktree 或等价隔离工作区。
- 开始前明确文件所有权：谁能改哪些文件，谁只能读哪些文件。
- 避免共享文件并发编辑；如果必须碰同一文件，Claude 只交 notes 或 patch proposal，Codex 手动合入。
- Claude 不直接修改主 worktree；Codex 从 Claude worktree 拉取 diff 后集成。
- 集成前先看 Claude diff，确认没有越界改动、格式 churn 或不相关重构。

## 分配模板

```text
Route: dual-agent

Codex owns:
- <hard path files/responsibilities>
- <integration and final validation>

Claude owns:
- <low-coupling files/responsibilities>
- Must not edit: <shared/high-risk files>

Claude deliverables:
- Diff in its worktree
- Short summary
- Validation command/output
- Risks or assumptions

Integration:
- Codex reviews Claude diff
- Codex merges/adapts changes
- Codex runs final targeted checks
```

## 本仓库推荐拆法

适合 Codex：

- `components/` 产品逻辑
- `protocols/` 协议和错误契约
- `ports/esp32/` ESP-IDF glue
- BLE/audio 传输语义
- COM3/BLE flash/monitor/matrix gate
- `pipeline_policy.json` 相关结论

适合 Claude：

- `!docs/` 中的说明、计划、验收记录
- 小型 Python/PowerShell host-side helper
- 单元测试、fixture、示例输入输出
- 明确关键词的 grep survey
- 长日志或历史 artifact 摘要

禁止默认交给 Claude：

- `audio_data` 语义重定义
- host subscription 顺序
- `subscribe` before `connect` 兼容性
- BLE matrix warning policy
- source-firmware consistency gate

## 时间和 Token 评估

这个模式主要省 Codex token 和关键路径时间，不一定省总 token。

- 省 Codex token：Claude 消化低耦合文件、日志、survey 后只返回摘要和 diff，Codex 不需要把全部上下文读进主会话。
- 省时间：只有 Codex 在 Claude 运行时继续做核心路径，才会缩短墙钟时间。
- 不省时间：Codex 等 Claude 审查完再改，等价串行，通常更慢。
- 不省 token：同一文件两边都读、都改、再解决冲突，会浪费上下文和人工注意力。

实践预期：

- 小任务：不要拆，Codex 直接做。
- 中等任务且边界清楚：通常能省 20% 到 40% Codex 主上下文，墙钟时间取决于并行度。
- 大任务含日志/测试/文档：可能明显降低 Codex 上下文压力，但需要严格文件所有权。

## 改善建议

- 给每个 dual-agent 任务生成一个短的 ownership block，先写清楚再开工。
- Claude 默认拿“能独立验收”的任务，不拿“需要持续设计判断”的任务。
- 集成时优先用 diff，而不是让 Claude 口头总结改了什么。
- 保留 legacy executor 给 native pipeline 和 evidence compression，不继续扩展成通用指挥系统。
- 后续可以做一个轻量脚本：创建 Claude worktree、写入任务说明、执行验证命令、导出 diff 和 summary，避免手工复制上下文。
