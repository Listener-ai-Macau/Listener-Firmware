# Codex Claude Executor

历史实现已收敛为精简版，当前操作文档见：

- `!docs/executor_reference.md`

通用协同原则由用户级 `$ai-collaboration-workflow` skill 提供。

保留的核心能力：

- 新 session 执行器
- native pipeline
- resource lock
- background health check
- temp worktree isolation
- `RESULT_JSON` / `.done` artifact 协议

已删除的复杂机制：

- session 复用
- prompt pipeline
- patch-proposal mode
- wrapper 诊断质量门
- workspace fingerprint
