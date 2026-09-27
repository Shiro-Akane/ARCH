# Phase 2H Completion Report

Date: 2026-09-27. **PASS WITH NON-BLOCKING ISSUES.**
Final local checkpoint: studio-phase2h-v0.19.0.
Branch: studio/phase2h-mainline-capability-sync.
No automatic push. STOP before Phase 3.

## Completed stages

| Stage | Checkpoint | Outcome |
| --- | --- | --- |
| A — Core tree sync / CPU revalidation | studio-phase2h-a-v0.17.0 / 01860f291d5348531eb84cbb6a505ca7a21a9d1b | PASS |
| B — runtime schema / retired / capability migration | studio-phase2h-b-v0.18.0 / af3c94aee31d748e4dbc9ecc8deb85728dafc3a6 | PASS |
| C — packaged Windows + WSL compatibility | studio-phase2h-v0.19.0 (resolve peeled tag) | PASS WITH NON-BLOCKING ISSUES |

A/B tag objects and commits are unchanged. C's commit contains reports/status
only, preserving the tested B implementation and production assets.

## Integration and authority

Disposable worktree audit selected exact authoritative non-Studio tree
synchronization, with a separate Core sync commit
86f999bafccf5868cef4e4258ff0e166048a35ae. No merge of diverged main and no local
scientific rewrite. Core authority:
502eadcb33a9e2d3c8bd079a10dbdc8af208ef10
(main parent 48f6d357d6b8085501d2012d70e923080bae3402).

All 18,928 tracked non-Studio entries match that authoritative tree. Studio's
previous Desktop, Session and AMR work was preserved.

## Binary, capabilities and migration

New A CPU Debug binary: build-phase2h-cpu/bin/ARCH.
CUDA OFF / KLU OFF / OpenMP ON / BUILD_TESTING ON.
SHA-256: 53fdf7939cf4d68c806b206f1e7cf2f4d0e6d3724682a203af6e01250375fcf7.
Build ID: 819e0395-cf95-4138-856d-0fa489386973.
77 explicit tracked inputs; dependenciesComplete=false remains explicit.

Actual binary: configuration extension v2, 92 standard parameters, 14 registered
models. Counts are acceptance evidence, not production constants.

Five retired keys are preserved on load, excluded from Custom/alias editing,
reported as migration issues and removed only through explicit undoable edits
and Save. timeintegrator is not equivalent to time_integrator.
Four new Gravity keys and their values/constraints are read from current schema.
Self gravity is no longer globally unavailable, while gravity field Preview
remains unsupported. JENS remains unavailable, with no silent fallback.

Schema Default, Inspection Parsed Value and matching Preview Model-read Value
remain distinct. Source discovery and tracked-input paths use the current tree.
Only Sod/CellularDet support full field/initial AMR; ordinary Init samples are
not represented as AMR cell arrays.

## Validation

B full regression, retained without repetition during unchanged C:
Studio171/171, Host70/70, Core13/13; lint/typecheck/build PASS.

C fresh Windows package was compared against B dist/Host/src: zero mismatches.
Actual native desktop UAT covered Sod inspection/warm Preview/x_pos/AMR,
Cellular 2D/warm/field/AMR, GravityBox source association/inspection-only,
92-key catalog/14-model registry, Gravity controls, retired remove/Undo/Save/
Reopen and JENS rejection.

Native Save and Save As were verified on disk; no original scientific config
was edited. Idle warm, active Preview and active AMR window shutdowns left no
owned Node/ARCH/launcher process. Fast cancel and active-state timing were
instrumented through the actual packaged Host API; native window closure was
still exercised. See the detailed report for precise evidence boundaries.

Final Core identity, preserved A/B tags, reports-only diff and git diff --check
were checked before checkpoint creation.

## Reports and remaining limits

- PHASE2H_MAINLINE_SYNC_AUDIT.md
- PHASE2H_A_MAINLINE_SYNC_REPORT.md
- PHASE2H_B_CAPABILITY_MIGRATION_REPORT.md
- PHASE2H_MAINLINE_COMPATIBILITY_REPORT.md

Non-blocking: inherited package metadata is still 0.16.0 and sample CLI docs
mention an old project path; exact tested Git/file/binary identities are recorded.
Some action labels have low contrast. No regression-only scope expansion was
made to address these. Package remains portable/unsigned; complete dependency
freshness remains unknown, separate from validated tracked inputs.

No new field/AMR model, CUDA/GPU qualification, simulation, SSH or Phase 3.
The final checkpoint is local only. **STOP.**
