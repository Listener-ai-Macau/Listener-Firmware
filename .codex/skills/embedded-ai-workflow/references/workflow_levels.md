# Workflow Levels

Use these levels to balance rigor and speed.

## Full

Use for:

- new feature work
- board bring-up
- hardware integration
- architecture changes
- multi-module refactors
- important bugs

Expect:

- dedicated plan file
- explicit per-step acceptance
- human approval between meaningful steps
- completion summary document

## Standard

Use for:

- normal feature work
- moderate-risk bug fixes
- contained subsystem improvements

Expect:

- plan file
- explicit acceptance per step
- human approval at step boundaries

## Fast

Use only when all of the following are true:

- change is small and local
- risk is low
- no meaningful hardware or architecture impact
- user explicitly prefers a fast path

Expect:

- a concise plan can be embedded in the conversation or a very small plan file
- still define acceptance
- do not silently expand scope
