# Legacy Executor Reference

这是 `tools/claude_execute.ps1` 的 legacy 操作参考。默认协作模型已经转为“开局拆任务、两个 agent 在独立 worktree 并行推进、Codex 集成”；本文件只保留 evidence executor、native pipeline、resource lock 和 artifact 协议的参考。

## 核心模型

- Wrapper 只做硬护栏：超时、后台、resource lock、worktree 隔离、native pipeline、artifact 协议。
- Codex 保留软判断：是否信摘要、是否重跑诊断、是否抽查证据、如何修代码。
- 每次 executor 调用都是新 Claude session；不复用 session。
- 只保留普通 execute 和 native pipeline；不保留 prompt pipeline / patch proposal。
- 小事实、小文档、小 grep 不走 executor；Codex 直接做。
- 确定性命令优先 native pipeline；executor 只用于窄范围多文件证据压缩、长日志摘要、测试/构建失败诊断。
- 不把宽泛评估题直接交给 executor；先拆成可验证的小问题。
- Claude 不再作为“Codex 做完后的默认 reviewer”。如果要并行协作，优先使用独立 worktree 的 upfront 任务拆分。
- Legacy executor 可以作为中高风险改动后的只读 reviewer，但这是风险触发的例外，不是每个 prompt 后的默认步骤，也不直接改代码。
- 当 Claude token 不计成本时，优化目标仍然是节省 Codex 主上下文和人类等待时间：让 Claude 吃大日志/大 diff/多文件证据，Codex 只消费结构化摘要和少量 `file:line` 证据。

## Legacy 只读 Review

Codex 先实现和验证；只有当前 diff 涉及多文件行为、BLE/audio/protocol、跨模块边界、重要 bug、测试缺口或不确定失败，并且无法用更清晰的 dual-agent 拆分处理时，才触发 legacy Claude 只读审查。Claude 输出问题和证据，Codex 决定是否采纳、亲自修改并重跑相关验证。

示例：

```powershell
pwsh -File .\tools\claude_execute.ps1 `
  -Name post-step-review `
  -TimeoutSeconds 120 `
  -Background `
  -Task "Read the current git diff and review for behavioral regressions only. Focus on BLE/audio/protocol invariants, component/port boundaries, missing tests, and source-firmware consistency. Do not edit files. Return findings with severity and file:line evidence."
```

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
  @{ name = "flash"; command = "pwsh"; args = @("-File", "./tools/flash.ps1", "-Port", "COM3"); timeout_seconds = 180 }
) | ConvertTo-Json -Compress

pwsh -File .\tools\claude_execute.ps1 -Name build-flash -TimeoutSeconds 600 -Background -Resource COM3,BLE -PipelineJson $steps
```

Step 字段：

| 字段 | 必填 | 说明 |
|---|---|---|
| `name` | 否 | 步骤名 |
| `command` | 是 | PowerShell 命令字符串，或配合 `args` 作为可执行文件路径 |
| `args` | 否 | 参数数组；存在时直接执行 `command args...`，不存在时把 `command` 当 PowerShell 命令字符串执行 |
| `timeout_seconds` | 否 | 单步超时 |
| `fail_on_stdout_regex` / `fail_on_stderr_regex` / `fail_on_output_regex` | 否 | 匹配则 FAIL |
| `inconclusive_on_stdout_regex` / `inconclusive_on_stderr_regex` / `inconclusive_on_output_regex` | 否 | 匹配则 INCONCLUSIVE |

优先使用 `command` + `args` 数组，避免在 JSON、PowerShell、子 shell 之间多层转义。只有短的 PowerShell 片段才直接写成 `command` 字符串。

BLE matrix gate 示例：

```powershell
$steps = @(
  @{
    name = "matrix"
    command = "python"
    args = @("./tools/verify_audio_ble_product_matrix.py", "--port", "COM3", "--capture-seconds", "5", "--fail-on-warning")
    timeout_seconds = 600
    structured_results = @(
      @{
        name = "ble_matrix"
        stdout_regex = "^\s*matrix_result_json=(.+?)\s*$"
        summary_fields = @("status", "matrix_total", "matrix_failed", "matrix_warning", "failed_cases", "warning_cases", "summary_log")
        copy_path_fields = @("summary_log")
        rules = @(
          @{ field = "status"; equals = "FAIL"; status = "FAIL"; reason = "BLE matrix status=FAIL" },
          @{ field = "matrix_warning"; greater_than = 0; status = "INCONCLUSIVE"; reason = "BLE matrix has warnings" }
        )
      }
    )
  }
) | ConvertTo-Json -Compress

pwsh -File .\tools\claude_execute.ps1 -Name ble-matrix -TimeoutSeconds 600 -Background -Resource COM3,BLE -PipelineJson $steps
```

## Artifact 消费

- 前台：读 stdout 最后一行 `RESULT_JSON: {...}`。
- 后台：读 artifact 下 `.done`。
- PASS：低风险任务可直接信摘要。
- Pipeline PASS：先读 `pipeline_policy.json`，只有 `continue_ok=true` 才继续下一档 gate。
- 非 PASS：读 `diagnosis.json` / `evidence.md` / `pipeline_steps.json`。
- Executor timeout：先看 `diagnosis.json.metrics`、进程启动时间、首个 stdout/stderr、首个 artifact 写入和最终状态。没有首输出信号时，只能归类为启动/认证/API 排队/prefill/健康状态未知；不要直接假设是任务太慢。
- 高风险 BLE/audio/hardware/protocol/cross-module 结论必须抽查结构化证据。

常见 artifact：

- `status.json`
- `.done`
- `summary.md`
- `diagnosis.json`
- `evidence.md`
- `workspace_delta.json`
- `pipeline_steps.json`
- `pipeline_policy.json`
- `pipeline_step_*/stdout.log`
- `pipeline_step_*/stderr.log`
- `pipeline_step_*/consistency.json`
- `pipeline_step_*/structured_result_*.json`

## Worktree

- 非资源、非 `-NoWorktree`、非 `-AllowWorkspaceChanges` 的任务在 `%TEMP%\codex_claude_executor_worktrees\<repo-hash>\<job>` 运行。
- Wrapper 会确认该目录是真实 git top-level，避免误把主仓库当 worktree reset/checkout。
- 资源任务默认在主仓库运行，因为硬件脚本通常依赖本地设备和 ignored artifact。
- 只读 executor 不再因为 `.cache` / `tests/artifacts` 这类自身 artifact 变化失败；如源码状态发生变化，`workspace_delta.json` 会列出 changed paths。
- Native pipeline 每步记录 source `head`/`diff_hash`、声明的 `artifact_files` size/sha、声明的 `structured_results` JSON 摘要和 artifact 内副本。
- Native pipeline 只按 step metadata 读取结构化结果；JSON 缺失或规则命中会按 metadata 映射为 FAIL / INCONCLUSIVE。
- `pipeline_policy.json` 汇总是否可继续信任当前 gate：跨 step `head` / `diff_hash` 漂移、structured result 缺失或规则命中都会让 `continue_ok=false`。

## Feedback

异常、重试、总结错误或新任务类型经验，追加到 `!docs/executor_feedback.md`。
