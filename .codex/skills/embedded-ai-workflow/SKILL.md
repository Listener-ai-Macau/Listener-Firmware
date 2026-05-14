---
name: embedded-ai-workflow
description: "Plan and execute embedded software work with explicit human approval gates. Use when working on embedded firmware features, bring-up, refactors, hardware integration, or important bug fixes in this repository."
---

# Embedded AI Workflow

## Overview

Use this skill as the default workflow for embedded software development in this repository. Treat it as the process layer, while repository-specific skills provide local structure, tools, and platform facts.

## Workflow Levels

Choose the lightest level that still matches the risk.

- `full`: new features, architecture changes, board bring-up, hardware integration, safety-critical changes, or important bugs
- `standard`: normal feature work or moderate-risk bug fixes
- `fast`: very small, low-risk, local fixes when the user explicitly wants a quick change

Default to `full` if hardware behavior, architecture, or cross-module integration is involved.

## Required Plan-First Flow

Before meaningful implementation, create a plan document in the repository's active-doc area:

- feature / bring-up / implementation plans go in `!docs/plans/`
- current bug fix / bug investigation plans go in `!docs/fixes/`

Before writing that plan, first do a short precedent review.

The default question is:

- `Has this already been solved by an official example, a mature built-in platform capability, a common industry pattern, or a well-established open-source tool?`

Default rule:

- Do not jump straight to a custom architecture or custom implementation if a reusable precedent likely already exists
- Check official SDK examples, official platform guidance, repository-local references, and mature widely used tools before proposing a new mechanism

The goal is not to avoid all custom work. The goal is to avoid reinventing a wheel that already exists and is already known to work.

## Official-First Execution Rule

Treat `official / built-in / already-proven in-repo / mature widely used` options as the default path.

Default rule:

- Prefer direct reuse of official examples, official APIs, built-in platform capabilities, existing repository mechanisms, and mature community-standard tools before inventing a wrapper, workaround, or new abstraction
- Only introduce custom logic when the existing option does not cover the needed product semantics, constraints, or acceptance criteria
- When using a custom layer, keep it as thin and local as practical around the reused official path

## Plan Surface Area Rule

The precedent review is required, but it does not need to be fully written into the human-facing plan by default.

Human-facing plan rule:

- Do not expand official, built-in, or already-adopted existing paths into long plan sections just to prove they were checked
- If the intended path is simply `use the official / existing mechanism as-is`, that can stay implicit or be mentioned only briefly
- Write detailed plan content only for the parts that are actually custom, adapted, risky, or decision-heavy

Explicitly write the precedent review into the plan only when at least one of these is true:

- the official or existing path was rejected
- the official or existing path needs a non-trivial adaptation
- custom glue changes behavior in a meaningful way
- the user needs to review a tradeoff, risk, or deviation from the precedent
- the reason a custom path is necessary would otherwise be non-obvious

Preferred interpretation:

- `official / existing path`: do it, usually without turning it into plan-heavy prose
- `custom path or custom deviation`: document it clearly in the plan so the human can review it

The plan must include:

- task goal
- current problem or missing capability
- scope
- out-of-scope items
- assumptions and dependencies
- ordered execution steps
- acceptance method for each step
- human checkpoints
- known blockers

When the work mixes official reuse with custom changes:

- Keep the plan focused on custom code, custom behavior, custom acceptance risk, and human decision points
- Mention official reused pieces only as much as needed for context and step boundaries

For implementation-oriented plans, each execution step must also explicitly record:

- which code files, scripts, docs, or modules are expected to change
- whether the step is documentation-only, script-only, firmware-only, host-only, or mixed
- the concrete command-level or artifact-level acceptance for that step

Default rule:

- Do not leave steps as high-level direction only when the real intent is to implement code
- If a step will later require code or script changes, name the likely files or at least the target module / directory in the plan
- If a step will later require verification, name the expected command, output fields, artifact path, or log markers in the plan

Add a short `existing options / precedent review` section only when the `Plan Surface Area Rule` says the precedent review needs to be human-visible.

If the repository already has a plan template or workflow document, follow it. If not, create a concise plan using this structure.

## Step Gating Rules

Execute only the current approved step.

For each step:

1. implement or investigate only the current step
2. run the acceptance checks for that step
3. report pass or fail against the defined acceptance
4. wait for approval before moving to the next step

Do not silently continue into later steps just because the current one passed.

If reality changes, update the plan and ask for approval on the revised plan before continuing.

## Acceptance Design

Make each step objectively testable.

Preferred acceptance methods:

- build succeeds
- flash succeeds
- logs contain specific markers
- unit or smoke tests pass
- host-device interaction shows a specific result
- an expected artifact or document exists

Avoid vague acceptance text such as `works`, `looks good`, or `mostly done`.

## Bug Workflow

Important bugs follow the same process.

Create or update a bug plan under `!docs/fixes/` with:

- observed behavior
- expected behavior
- reproduction steps
- suspected scope
- root-cause investigation steps
- fix steps
- acceptance checks

Only skip a dedicated plan for clearly trivial, low-risk fixes when the user explicitly wants a fast path.

## Execution Discipline

When a plan exists, do not improvise beyond it.

Allowed:

- implementing the current step
- tightening details required for the current step to pass
- recording newly discovered constraints
- proposing a plan update

Not allowed without approval:

- expanding scope
- changing architecture direction
- starting later steps early
- mixing unrelated cleanup into the active step

## Script Interop Rule

When embedded repositories use mixed scripting on Windows, especially `PowerShell` calling inline `Python`, treat dynamic content passing as a reliability rule, not an ad-hoc implementation detail.

Default rules:

- Do not splice dynamic user text, JSON, logs, prompts, or binary-derived content directly into inline source code strings
- Pass dynamic content through a safe transport such as:
- base64-encoded UTF-8 text
- JSON serialization
- command-line arguments
- environment variables
- temporary files

Preferred interpretation:

- The primary fix is safe cross-language parameter passing
- Encoding differences may still matter, but they are not the main protection against inline script corruption

For `PowerShell -> Python` handoff in embedded repositories, default to:

- base64 or JSON for dynamic payloads

Only embed text directly into inline Python source when the content is static, ASCII-safe, and not influenced by runtime data.

## Windows Path Escaping Rule

On Windows, treat path serialization as part of script interop hygiene.

Default rules:

- Do not casually paste backslash paths such as `C:\Users\name\...` into inline `Python`, JSON literals, regex patterns, or Markdown examples that may later be copied into code
- Assume sequences such as `\U`, `\n`, and `\t` can be misinterpreted unless the transport is explicit
- Prefer repo-relative paths in repository documents when that is practical
- When an absolute Windows path is unavoidable, prefer one of:
- `pathlib.Path(...)`
- raw Python strings such as `r"C:\path\to\file"`
- forward-slash paths when the target tool accepts them
- command-line arguments, environment variables, JSON, or temporary files for dynamic paths

Before finalizing Windows-oriented scripts or docs, do a quick pass for accidental escape sequences caused by copied backslash paths.

## Completion Documentation

When an important feature or important bug fix is completed, add a short handoff document under the repository's `!docs/` tree.

Prefer a `!docs/features/` directory when the repository uses one.

The summary should include:

- what was built or fixed
- where the important code lives
- how to verify it
- important constraints
- known risks and likely next steps

After an implementation plan has been fully completed and its final behavior has been summarized, prefer moving the durable knowledge into `!docs/features/` and removing the no-longer-active plan document.

Treat `!docs/plans/` as the place for:

- reusable plan templates
- active feature / implementation plans
- currently relevant in-progress work

Treat `!docs/fixes/` as the place for:

- current bug fix plans
- current bug investigation notes
- active bug-focused recovery work

Do not let `!docs/plans/` or `!docs/fixes/` accumulate old completed documents unless the repository explicitly wants to preserve them.

## Human-Facing Language Rule

Write human-facing repository documents in Chinese by default.

This applies to content such as:

- plan documents under `!docs/plans/`
- bug-fix documents under `!docs/fixes/`
- feature summaries under `!docs/features/`
- workflow and handoff documents under `!docs/`
- approval notes, acceptance notes, and human review context written into repository documents

This rule is about the document body and review-facing wording. File and directory names may still use repository-friendly naming such as `snake_case` when that is more practical.

If a repository or user explicitly requires another language, follow that explicit override.

## Human vs AI Responsibilities

AI owns:

- writing the plan
- executing the approved step
- running command-line verification
- collecting logs and evidence
- analyzing results
- writing completion summaries

Default execution rule:

- When the environment allows it, AI should default to doing the hands-on execution itself
- Prefer running setup, build, flash, log capture, host-side test scripts, serial injection, result analysis, and iterative fixes directly
- Prefer command-line verification first
- Do not push executable work back to the human when AI can reasonably perform it on the current machine
- Treat interactive monitor sessions as optional human convenience, not the default verification path for AI
- Ask the human to step in only when the next action truly requires a physical or OS-mediated step that AI cannot replace

Human owns:

- approving plans
- approving movement to the next step
- making product or architecture decisions
- performing unavoidable physical or OS-mediated actions when AI cannot

## Interaction Rule

When the user asks `what next`, answer from the approved plan:

- current step status
- acceptance result
- next blocked or unblocked checkpoint

Do not switch to an ad-hoc freeform task list when an approved plan already exists.

## Repository Integration

Repository-specific embedded skills should extend this workflow by adding:

- local directory and naming rules
- toolchain and build commands
- board and platform facts
- repo-specific code reading order
- repo-specific verification commands

Do not duplicate workflow text across every repository-specific skill. Keep the workflow here and repository facts there.
