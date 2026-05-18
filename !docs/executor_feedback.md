# Executor Workflow Feedback

Codex appends observations here. Do not edit manually unless adding a past observation.

- [2026-05-17] good: file_survey template compressed 2253 lines → 69 lines, all 4 spot-checked facts correct
- [2026-05-17] bad: PowerShell tool in Claude Code VSCode extension returns empty output (exit 1), must use Bash tool + chcp 65001
- [2026-05-17] idea: Treat low-risk PASS executor summaries as authoritative; while waiting, Codex should inspect code or plan instead of rereading raw logs.
- [2026-05-17] idea: executor BLE matrix diagnosis needs to flag stale artifacts when failed cases do not create fresh serial logs.
- [2026-05-17] good: Claude executor now launches from repo root for discoverable history while using prompt Execution root for worktree-isolated commands.
- [2026-05-17] bad: broad P1/P2 BLE survey timed out at 180s; use narrower prompts or native pipeline steps for hardware loops. → **FIXED**: pipeline diagnosis timeout no longer overrides the pipeline's actual failure status; reason now says "pipeline step X FAIL (diagnosis compression timed out; pipeline result is authoritative)".
- [2026-05-17] good: focused workflow-risk survey completed in an isolated worktree and produced actionable file:line risks under 100 lines.
- [2026-05-17] bad: stale COM3/BLE resource locks can survive a completed executor if status finalization fails; `-ClearStaleLocks` or explicit stale-lock cleanup needs to be part of hardware retry playbooks. → **FIXED**: `Acquire-ResourceLocks` now checks PID liveness before blocking; dead-owner locks are auto-cleared regardless of age.
- [2026-05-17] idea: hardware matrix pipelines should use `-AllowWorkspaceChanges` because test commands intentionally update artifacts/logs, even when source edits are not expected. → **FIXED**: resource tasks (tasks with `-Resource`) auto-skip the workspace guard; no need for explicit `-AllowWorkspaceChanges`.
- [2026-05-17] good: workflow risk items 1-8 are fixed and verified by background executor; summary fallback, lock commands, session reset, bash execution root, and background-wait discipline all passed smoke tests.
- [2026-05-17] bad: resource `-Background` pipelines can remain at `state=starting` without locks or child processes; foreground fallback is also refused for long resource tasks, so Codex had to run hardware scripts directly after `-ListLocks`. → **FIXED**: background startup now has a 10s health gate and foreground fallback has an explicit `-ForceForeground -Reason` override.
- [2026-05-17] idea: pipeline PASS should surface `matrix_warning > 0` as a non-PASS diagnosis signal for BLE product matrices; P5 returned exit 0 while `case_result=warning`. → **FIXED**: native pipeline maps `matrix_warning > 0` to `INCONCLUSIVE`; matrix script supports `--fail-on-warning` and writes `matrix_result.json`.
- [2026-05-17] bad: non-resource read-only final-summary executor refused foreground with `use_background`, then `-Background -NoWorktree` failed `workspace changed during read-only task`; fingerprinting should ignore executor artifact/status writes or explain the exact changed paths.
- [2026-05-17] bad: `-ListLocks` returned exit 0 with empty stdout during Step 5 hardware validation, so Codex could not rely on lock JSON and fell back to process checks before direct COM3/BLE runs.
