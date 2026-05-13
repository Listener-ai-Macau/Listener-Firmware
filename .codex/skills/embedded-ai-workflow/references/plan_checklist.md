# Plan Checklist

Before execution starts, confirm the plan includes:

- clear goal
- current problem
- in-scope items
- out-of-scope items
- assumptions and dependencies
- ordered steps
- acceptance per step
- human checkpoint per step
- blocker list
- Chinese wording if the plan is meant for human review and there is no explicit language override

Before marking a step done, confirm:

- the intended work was completed
- the declared acceptance method was run
- the result was reported clearly
- the next step has not started yet
- if the step added or changed cross-language scripts on Windows, dynamic content is passed safely rather than spliced directly into inline source

Before closing important work, confirm:

- completion summary document exists
- summary explains code location and verification
- known risks are called out
- human-facing document text is in Chinese unless explicitly overridden
- the durable result has been moved to `docs/features/` or another stable `docs/` location
- the completed active plan has been removed from `docs/plans/` unless the repository explicitly wants to keep it
- if explicit PowerShell invocation is required on Windows, the chosen script interop approach is deliberate rather than an accidental default
