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

Moving a note here changes its location, not the source identity of its
measurements. Inputs, machine-readable results and failed-attempt records keep
their established validation paths so that reproducible commands and provenance
remain valid. Do not link an intermediate observation as the current result.
