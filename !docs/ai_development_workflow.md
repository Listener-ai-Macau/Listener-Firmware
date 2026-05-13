# AI Development Workflow

This document defines the default embedded software workflow for AI work in `voice-keyboard-firmware`.

The same process is now kept inside this repository so it can be uploaded and reused together with the repo, while repository-specific facts stay in the separate repo skill.

## 1. Goal

Use AI as an execution engine, but keep human approval at clear checkpoints.

The default model is:

- AI writes a concrete plan first.
- Human approves the plan or asks for changes.
- AI executes only the approved next step.
- Each step has an explicit acceptance method.
- AI does not skip ahead to later steps before the current step is accepted.

This workflow applies to:

- new feature work
- bring-up work
- refactors with behavior impact
- important bug fixing
- integration tasks that touch hardware, tools, or architecture

## 2. Required Plan-First Flow

Before meaningful implementation starts, AI must create a plan document in `!docs/plans/`.

在写方案之前，AI 应先做一轮“现成方案 / 先例检查”。

默认要先问自己：

- 这个问题是不是已经有官方示例、平台内建能力、成熟开源工具，或者业内常见做法可以直接复用？

默认规则是：

- 如果大概率存在现成轮子，不要直接跳到“自定义架构”或“从零写一套”
- 先查官方示例、官方平台文档、仓库内已有参考资料，以及成熟常用工具
- 在方案中明确写出：
  - 查过哪些现成方案
  - 最终复用 / 改造 / 放弃了哪条路线
  - 为什么当前选择比“直接照搬现成方案”更合适

这样做的目标不是禁止自定义实现，而是避免重复造一个已经存在、已经被验证过的轮子。

The plan must include:

- task title
- problem statement
- scope
- out-of-scope items
- assumptions and dependencies
- a step-by-step execution plan
- acceptance criteria for each step
- blockers or human actions expected at any step

如果任务明显存在可复用先例，方案中还应增加一小节“现成方案 / 先例检查”。

AI must present that plan to the human and wait for approval before implementing step 1.

## 3. Step Gating Rules

AI must execute the plan in order.

For each step:

1. AI performs only the work for the current step.
2. AI runs the listed verification for that step.
3. AI reports the result against that step's acceptance criteria.
4. Human approves moving to the next step.

AI must not silently continue to step N+1 just because step N passed technically.

If the human says "continue", that counts as approval for the next step only.

## 4. Bug Handling Rules

Important bugs should follow the same pattern.

AI must create or update a bug plan in `!docs/plans/` with:

- observed behavior
- expected behavior
- suspected scope
- reproduction method
- root-cause investigation steps
- fix steps
- acceptance checks

For trivial local fixes, a separate plan file can be skipped only if the user explicitly wants a quick fix and the change is clearly low risk.

## 5. Execution Discipline

When a plan exists, AI must not improvise beyond it.

Allowed behavior:

- tightening an implementation detail that is required to satisfy the current step
- recording newly discovered constraints
- proposing a plan amendment back to the human

Not allowed without approval:

- expanding scope
- changing architecture direction
- starting later plan steps early
- bundling unrelated cleanup into the same implementation step

If reality changes, AI should update the plan and ask for approval on the revised plan before continuing.

## 6. Acceptance Design

Each step must define an acceptance method that is concrete and testable.

Preferred acceptance methods:

- build succeeds
- flash succeeds
- boot log contains specific markers
- unit or smoke test passes
- host-device interaction produces a specific observable result
- document exists and reflects the implemented behavior

Avoid vague acceptance text such as:

- "looks good"
- "works"
- "mostly done"

## 7. Completion Documentation

When an important feature or important bug fix is completed, AI must add a handoff document under `!docs/`.

Preferred location:

- feature or subsystem summaries: `!docs/features/`
- cross-cutting workflow or architecture notes: `!docs/`

That document should help a future AI quickly recover context and continue work.

Minimum contents:

- what was built or fixed
- where the key code lives
- how to verify it
- important constraints
- known risks or next steps

完成后，若对应方案已经全部执行完毕，并且稳定知识已经整理完成，应优先把最终结果沉淀到 `!docs/features/` 或其他稳定的 `!docs/` 文档里，而不是长期堆在 `!docs/plans/`。

默认建议是：

- `!docs/plans/` 只保留模板和当前有效方案
- 已完成方案在转成功能总结后，从 `!docs/plans/` 中删除

## 7.1 面向人的文档语言

默认情况下，写给人看的仓库文档应使用中文。

包括但不限于：

- `!docs/plans/` 下的方案文档
- `!docs/features/` 下的功能总结
- `!docs/` 下的流程说明、交接说明、验收记录

这个规则主要针对文档正文和说明性文字。

文件名、目录名仍可根据仓库约定继续使用英文 `snake_case`。

如果用户或仓库另有明确语言要求，则以明确要求为准。

## 7.2 Windows 路径与转义

在 Windows 上，路径写法本身也属于脚本互操作可靠性的一部分。

默认规则是：

- 不要把 `C:\Users\name\...` 这类反斜杠路径随手嵌进内联 `Python`、JSON 字面量、正则表达式或之后很可能被复制进代码的示例里
- 默认认为 `\U`、`\n`、`\t` 这类序列可能被误解释，除非传递方式是显式且安全的
- 仓库文档里能用相对路径时，优先用仓库内相对路径，而不是机器相关的绝对路径
- 动态路径优先通过命令行参数、环境变量、JSON、临时文件等方式传递

如果确实必须写绝对 Windows 路径，优先使用以下安全形式之一：

- `pathlib.Path(...)`
- Python 原始字符串，例如 `r"C:\path\to\file"`
- 目标工具可接受时，使用正斜杠路径

在提交 Windows 相关脚本或文档前，AI 应额外检查一次是否因为复制路径而引入了意外转义。

## 8. Human vs AI Responsibilities

AI owns:

- writing plans
- implementation
- command-line verification
- log capture
- result analysis
- documenting the outcome

Human owns:

- approving plans
- approving movement to the next step
- making requirement or architecture decisions
- performing unavoidable physical or OS-mediated actions when AI cannot

## 9. Directory Rules

- Put active and historical task plans in `!docs/plans/`.
- Put reusable feature summaries in `!docs/features/`.
- Keep filenames descriptive and `snake_case`.

For this repository's preferred cleanup model:

- keep templates and active plans in `!docs/plans/`
- move durable finished knowledge to `!docs/features/`
- remove completed plans after their final summary is written, unless there is an explicit reason to keep them

Examples:

- `!docs/plans/esp32_ble_hid_autotest_plan.md`
- `!docs/plans/ble_hid_input_path_bugfix_plan.md`
- `!docs/features/ble_hid_bringup.md`

## 10. When The User Asks "What Next?"

AI should answer by referencing:

- the approved plan
- the current step status
- the next blocked or unblocked acceptance checkpoint

AI should not answer with an ad-hoc new task list when an approved plan already exists.
