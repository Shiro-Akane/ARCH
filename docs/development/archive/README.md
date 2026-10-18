# Historical development records

This directory is for contributors tracing earlier design decisions. Its notes
retain intermediate findings, unresolved questions at the time, machine-specific
observations and superseded plans. They are not setup instructions, current
feature descriptions or additional acceptance requirements.

For normal use, read the [backend guide](../../CudaBackendStatus.md). For measured
results, start with [Validation](../../../validation/README.md). The maintained
[acceptance checklist](../CudaReleaseStandard.md) governs current review work.

| Subject | Preserved records |
| --- | --- |
| Backend review and early runtime checks | [Backend evidence](CudaBackendEvidence.md) ([中文](CudaBackendEvidence.zh-CN.md)); [shared AMR, sparse burn and geometry refactor](CudaRefactorSmoke.md) |
| Acceptance decisions and intermediate checks | [Historical acceptance checklist](CudaReleaseStandard.md) |
| Geometry derivation and correction review | [Curvilinear metric review](CurvilinearMetricReview.md) |
| Refinement-indicator roundoff analysis | [AMR indicator review](AmrIndicatorNoiseReview.md) |
| Physical transport coefficient comparison | [Diffusion coefficient review](DiffusionCoefficientAlignment.md) ([中文](DiffusionCoefficientAlignment.zh-CN.md)) |
| Compute/Studio integration and release execution history | [Dated checkpoints and original evidence](compute-studio/ExecutionHistory.zh-CN.md); current actions are maintained only in the [release plan](../../../ARCH_RELEASE_PLAN.md); the compute table preserves original scientific requirements |
| CPU/CUDA optimization campaign | [Implementation and acceptance ledger](optimization/ImplementationAndAcceptancePlan.zh-CN.md); subsequent release work follows the [release plan](../../../ARCH_RELEASE_PLAN.md) |
| Low-density and configuration migration | [Implementation record](low-density/ImplementationReport.zh-CN.md), [robustness design](low-density/RobustnessPlan.zh-CN.md), [retired parameter audit](low-density/ParameterRetirementAudit.zh-CN.md) |

Moving a note here changes its location, not the source identity of its
measurements. Inputs, machine-readable results and failed-attempt records keep
their established validation paths so that reproducible commands and provenance
remain valid. Do not link an intermediate observation as the current result.

## Modernization checkpoints

The [release plan](../../../ARCH_RELEASE_PLAN.md) owns current execution order. The [closed modernization plan](../../../ARCH_MODERNIZATION_PLAN.md) and these completed records retain their original inspected or accepted scope.

| Record | Purpose |
| --- | --- |
| [Preflight](modernization/Preflight-20261012.zh-CN.md) | Working entry, old progress, Studio integration and branch cleanup |
| [Stage 1 completion](modernization/ModernizationR1-Stage1Completion-20261012.zh-CN.md) | Shared-core map, coverage limits and subsequent work |
| [Original repository map](modernization/ModernizationR1-RepositoryMap-20261012.zh-CN.md) | Earlier R1.1 snapshot: 11,076 paths; later 11,078 includes two documentation files |
| [Cellular burn-rate audit](modernization/CellularBurnRateCpuCudaAudit-20261008.zh-CN.md) | Original report imported byte-for-byte from commit `110e769ddbf4bad6a8aafef744dffe4c7aadf27f`; executed reproduction is `ee091b35c`, not the report branch |

The current [R1–R4 foundation report](../modernization/R1R4Foundation-20261013.zh-CN.md) consolidates the completed foundation; the following dated notes preserve the status and failures at each checkpoint, rather than current instructions.

| Dated checkpoint | Preserved scope |
| --- | --- |
| [R2.1 identity](modernization/BaselineIdentity-20261012.zh-CN.md) | Original source, executable, representative and input baseline |
| [R2.2 numerical batch](modernization/R22NumericalChecks-20261012.zh-CN.md) | Original accepted cases, derived inputs, failures and stopped builds |
| [R2 closeout](modernization/R23PerformanceAndConfiguration-20261012.zh-CN.md) | Runtime/build/RAM measurements and bounded O7 review |
| [R3.1 audit](modernization/R31AuditTriage-20261013.zh-CN.md) | Candidate triage and retained scientific gaps |
| [R3.2 attribution](modernization/R32RklCompilationAndInactiveIdentity-20261013.zh-CN.md) | RKL diagnostic profiles and strict inactive identity decision |
| [R3 handoff](modernization/R33CloseoutAndR4Handoff-20261013.zh-CN.md) | Frozen R4 work packages and input provenance |
| [R4.1/R4.2 acceptance](modernization/R41R42Implementation-20261013.zh-CN.md) | Tool/input changes, static checks and separate provider/actual-workspace reviews |

The original machine JSON receipts stay under `development/modernization/` unchanged. The foundation machine index maps old human paths to these archives; no compatibility stubs or rewritten raw receipts are used.

<details>
<summary>Stage 1 machine evidence</summary>

[Path coverage](modernization/ModernizationR1-RepositoryCoverage-20261012.json) and [build inventory](modernization/ModernizationR1-BuildInventory-20261012.json) retain the original baseline counts, production closure and configured tests.

</details>

## RZ implementation history

The 107 dated RZ implementation notes are retained under `histroy/rz/`. They
record intermediate decisions, mathematics, failures and source identities;
the [release plan](../../../ARCH_RELEASE_PLAN.md) owns current work. The
[relocation map](../../../validation/backend/results/release-readiness/REL35DocumentationRelocation-20261016.json)
resolves original paths in immutable machine records. Original Summary JSON
files remain in `docs/development/`; no redirect notes are maintained.

For historical derivations, start with the [physical source decision](histroy/rz/RZExternalSourceDecisionPackage-20261006.zh-CN.md),
[viscous law decision](histroy/rz/RZViscosityDecisionPackage-20261006.zh-CN.md),
[axis reference](histroy/rz/RZAxisNeighborDecisionPackage-20261006.zh-CN.md) or
[continuous force reference](histroy/rz/RZContinuousPotentialForceReferenceContract-20261005.zh-CN.md).
These records are historical references, not a new pending-task list.
