---
name: arch-engineering
description: Apply ARCH's existing numerical and engineering contracts during implementation, review, validation, or bounded performance work. Coordinate with the global multimodel-engineering skill without changing physics, architecture or accepted tolerances merely to improve speed.
---

# ARCH engineering

Use this project skill with the global `multimodel-engineering` skill when external workers have useful bounded work. This file is a workflow boundary, not a new physical or software architecture. Read only the relevant existing project contract, code paths and acceptance evidence.

Keep ARCH-specific skill refinements, numerical rules, workload measurements and validation evidence in this repository. Reuse the global MCP and provider registry; do not create a second ARCH gateway or copy account configuration here. A project-only improvement does not change global routing/budgets, and workflow self-adjustment does not authorize changing the project contract.

## Preserve the numerical contract

- Identify the actual equations/definitions, units, storage/indexing, boundary/synchronization behavior, precision and error budgets involved. Use established project requirements. Do not infer a new tolerance from a plot, a benchmark, a passing build or another model's assertion.
- CPU/GPU paths should preserve the project's accepted physical definitions and equivalence requirements. Inspect source, build/dispatch and executable behavior when claiming integration; documentation/frontend presence alone does not establish runtime validation. This skill does not prescribe a new shared-library architecture.
- Freeze interfaces, invariants, baseline and scientific acceptance before coupled implementation. A concrete conflict goes back to the user with evidence and options; a worker must not change the contract to simplify its task.
- Select validation that can detect the relevant failure: limiting/analytic cases, conservation or identity checks, convergence, representative regressions and CPU/GPU comparison when applicable. Do not add a universal test checklist, rerun every benchmark after a text edit, or pretend local checks prove all physical correctness.
- Preserve any established memory/resource ceiling for the selected workload. Measure peak memory and runtime when relevant; this skill does not invent a new universal machine limit.

## Optimize only with a reason to stop

Start from a profile and a representative accepted baseline. Record build/configuration, input scale, hardware, runtime variance and the numerical metric needed to assess the proposed change. Compare the same workload and include validation, review and maintenance cost. A performance hypothesis without measurements is a hypothesis, not a required refactor.

Make one bounded change at a time. Stop when the agreed objective is met or when two nonblocking attempts yield no material improvement. Prefer the simpler accepted implementation; defer speculative extreme cases and unmeasured abstractions. A reproduced numerical/correctness failure remains blocking and must be fixed or reported unresolved, regardless of optimization attempts.

## Delegate without duplicate ownership

DeepSeek can verify a frozen derivation, inspect a focused path, implement an isolated change, or test an explicit hypothesis. Gemini can improve selected frozen documentation or existing comment bodies; it cannot rename identifiers, change equations, alter algorithms or format whole source files under an editing task.

Assign one writer per file/interface and use isolated worktrees for implementation. Parallelize independent checks and stable text; serialize shared layout, ownership and synchronization decisions. The manager independently validates critical findings and integrates only accepted scope. Raw H5/checkpoint or unrelated private data is excluded from task packets; selected source and processed evidence may be sent under the global authorization.

Keep a short task/evidence index instead of copying full logs into every README. Reuse existing project documentation locations. Separate inspected source facts, proposed changes, executed runtime tests and numerical acceptance in reports. No source or architecture optimization is authorized merely by installing this skill.
