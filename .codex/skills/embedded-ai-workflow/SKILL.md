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

Before meaningful implementation, create a plan document in the repository's `docs/plans/` directory.

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

Create or update a bug plan with:

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

## Completion Documentation

When an important feature or important bug fix is completed, add a short handoff document under the repository's `docs/` tree.

Prefer a `docs/features/` directory when the repository uses one.

The summary should include:

- what was built or fixed
- where the important code lives
- how to verify it
- important constraints
- known risks and likely next steps

After an implementation plan has been fully completed and its final behavior has been summarized, prefer moving the durable knowledge into `docs/features/` and removing the no-longer-active plan document.

Treat `docs/plans/` as the place for:

- reusable plan templates
- active plans
- currently relevant in-progress work

Do not let `docs/plans/` accumulate old completed plans unless the repository explicitly wants to preserve them.

## Human-Facing Language Rule

Write human-facing repository documents in Chinese by default.

This applies to content such as:

- plan documents under `docs/plans/`
- feature summaries under `docs/features/`
- workflow and handoff documents under `docs/`
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
