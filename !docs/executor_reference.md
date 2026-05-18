# Executor Reference

这是 `tools/claude_execute.ps1` 的操作参考。通用协同原则由用户级 `$ai-collaboration-workflow` skill 提供。

## 核心模型

- Wrapper 只做硬护栏：超时、后台、resource lock、worktree 隔离、native pipeline、artifact 协议。
- Codex 保留软判断：是否信摘要、是否重跑诊断、是否抽查证据、如何修代码。
- 每次 executor 调用都是新 Claude session；不复用 session。
- 只保留普通 execute 和 native pipeline；不保留 prompt pipeline / patch proposal。

## 常用调用

```powershell
# 普通执行 / 调查
pwsh -File .\tools\claude_execute.ps1 -Name survey -TimeoutSeconds 30 -Task "<task>"

# 后台长任务
pwsh -File .\tools\claude_execute.ps1 -Name build-survey -TimeoutSeconds 300 -Background -Task "<task>"

# 强制前台长任务
pwsh -File .\tools\claude_execute.ps1 -Name debug -TimeoutSeconds 120 -ForceForeground -Reason "interactive debugging" -Task "<task>"

# 资源锁
pwsh -File .\tools\claude_execute.ps1 -ListLocks
pwsh -File .\tools\claude_execute.ps1 -ClearStaleLocks
```

`TimeoutSeconds > 30` 的前台任务默认拒绝。要么用 `-Background`，要么显式 `-ForceForeground -Reason`.

## Native Pipeline

```powershell
$steps = @(
  @{ name = "build"; command = "idf.py build"; timeout_seconds = 420 },
  @{ name = "flash"; command = "pwsh -File ./tools/flash.ps1 -Port COM3"; timeout_seconds = 180 }
) | ConvertTo-Json -Compress

pwsh -File .\tools\claude_execute.ps1 -Name build-flash -TimeoutSeconds 600 -Background -Resource COM3,BLE -PipelineJson $steps
```

Step 字段：

| 字段 | 必填 | 说明 |
|---|---|---|
| `name` | 否 | 步骤名 |
| `command` | 是 | PowerShell 命令 |
| `timeout_seconds` | 否 | 单步超时 |
| `fail_on_stdout_regex` / `fail_on_stderr_regex` / `fail_on_output_regex` | 否 | 匹配则 FAIL |
| `inconclusive_on_stdout_regex` / `inconclusive_on_stderr_regex` / `inconclusive_on_output_regex` | 否 | 匹配则 INCONCLUSIVE |

BLE matrix gate 示例：

```powershell
$steps = @(
  @{
    name = "matrix"
    command = "python ./tools/verify_audio_ble_product_matrix.py --port COM3 --capture-seconds 5 --fail-on-warning"
    timeout_seconds = 600
    inconclusive_on_stdout_regex = "^\s*matrix_warning=([1-9]\d*)\s*$"
  }
) | ConvertTo-Json -Compress

pwsh -File .\tools\claude_execute.ps1 -Name ble-matrix -TimeoutSeconds 600 -Background -Resource COM3,BLE -PipelineJson $steps
```

## Artifact 消费

- 前台：读 stdout 最后一行 `RESULT_JSON: {...}`。
- 后台：读 artifact 下 `.done`。
- PASS：低风险任务可直接信摘要。
- 非 PASS：读 `diagnosis.json` / `evidence.md` / `pipeline_steps.json`。
- 高风险 BLE/audio/hardware/protocol/cross-module 结论必须抽查结构化证据。

常见 artifact：

- `status.json`
- `.done`
- `summary.md`
- `diagnosis.json`
- `evidence.md`
- `pipeline_steps.json`
- `pipeline_step_*/stdout.log`
- `pipeline_step_*/stderr.log`

## Worktree

- 非资源、非 `-NoWorktree`、非 `-AllowWorkspaceChanges` 的任务在 `%TEMP%\codex_claude_executor_worktrees\<repo-hash>\<job>` 运行。
- Wrapper 会确认该目录是真实 git top-level，避免误把主仓库当 worktree reset/checkout。
- 资源任务默认在主仓库运行，因为硬件脚本通常依赖本地设备和 ignored artifact。

## Feedback

异常、重试、总结错误或新任务类型经验，追加到 `!docs/executor_feedback.md`。
