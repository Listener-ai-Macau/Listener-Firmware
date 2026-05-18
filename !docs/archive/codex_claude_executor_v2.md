# Codex Claude Executor（Legacy）

这个方案已封存为 legacy evidence executor，不再作为默认 AI 协作模型。当前默认模型是：开局拆任务，Codex 和 Claude 分别在独立 worktree 推进，Codex 负责难路径和最终集成。

legacy executor 当前操作文档见：

- `!docs/executor_reference.md`

通用协同原则由用户级 `$ai-collaboration-workflow` skill 提供。

仍然保留的用途：

- native pipeline
- COM3 / BLE 等 resource lock
- 长日志、失败诊断、证据压缩
- background health check
- temp worktree isolation
- `RESULT_JSON` / `.done` artifact 协议

不再推荐的用法：

- Codex 做完一个 prompt 后默认让 Claude 审一遍
- Claude 审完直接在同一工作区改代码
- 用 executor 承接宽泛架构判断或开放式任务

已删除的复杂机制：

- session 复用
- prompt pipeline
- patch-proposal mode
- wrapper 诊断质量门
- workspace fingerprint
- stream-json timeout probe
