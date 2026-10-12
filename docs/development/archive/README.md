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
| Compute/Studio integration and release execution history | [Dated checkpoints and original evidence](compute-studio/ExecutionHistory.zh-CN.md); current actions remain in the [compute plan](../ComputeOptimizationPlan.zh-CN.md) |
| CPU/CUDA optimization campaign | [Implementation and acceptance ledger](optimization/ImplementationAndAcceptancePlan.zh-CN.md); subsequent work follows the [current compute plan](../ComputeOptimizationPlan.zh-CN.md) |
| Low-density and configuration migration | [Implementation record](low-density/ImplementationReport.zh-CN.md), [robustness design](low-density/RobustnessPlan.zh-CN.md), [retired parameter audit](low-density/ParameterRetirementAudit.zh-CN.md) |

Moving a note here changes its location, not the source identity of its
measurements. Inputs, machine-readable results and failed-attempt records keep
their established validation paths so that reproducible commands and provenance
remain valid. Do not link an intermediate observation as the current result.

## Modernization checkpoints

The current [modernization plan](../../../ARCH_MODERNIZATION_PLAN.md) owns execution order; these completed records retain their original inspected or accepted scope.

| Record | Purpose |
| --- | --- |
| [Preflight](modernization/Preflight-20261012.zh-CN.md) | Working entry, old progress, Studio integration and branch cleanup |
| [Stage 1 completion](modernization/ModernizationR1-Stage1Completion-20261012.zh-CN.md) | Shared-core map, coverage limits and subsequent work |
| [Original repository map](modernization/ModernizationR1-RepositoryMap-20261012.zh-CN.md) | Earlier R1.1 snapshot: 11,076 paths; later 11,078 includes two documentation files |
| [Cellular burn-rate audit](modernization/CellularBurnRateCpuCudaAudit-20261008.zh-CN.md) | Original report imported byte-for-byte from commit `110e769ddbf4bad6a8aafef744dffe4c7aadf27f`; executed reproduction is `ee091b35c`, not the report branch |

<details>
<summary>Stage 1 machine evidence</summary>

[Path coverage](modernization/ModernizationR1-RepositoryCoverage-20261012.json) and [build inventory](modernization/ModernizationR1-BuildInventory-20261012.json) retain the original baseline counts, production closure and configured tests.

</details>
