# Contributor guide and working records

Start with [implementation ownership](ImplementationOwnership.md) before changing
shared mathematics, backend storage, or execution paths. The map identifies the
single maintained implementation and its CPU/CUDA consumers.

## Current maintenance

- [Maintenance freeze and release ledger](CudaReleaseStandard.md): the active
  post-acceptance organization scope, followed by the completed release plan and
  its execution history. Preserve the distinction between the accepted source
  and later maintenance changes.
- [Optimized core-build reference](../../validation/backend/results/cold-core-first-law-20260907/release-909/README.md):
  the measured cold, no-op and incremental builds, with the two-heavy/four-total
  job configuration. This is a measured reference, not a universal optimum.
- [Tests](../../tests/README.md) and [tools](../../tools/README.md): choose focused
  checks by responsibility and reuse the existing execution/evidence helpers.
- [Validation](../../validation/README.md): the combined acceptance record and
  links to scientific, runtime, instrumentation and resource measurements.

## Historical investigations

These records retain their original observations and source identities. Their
interim conclusions do not replace the current validation index.

- [Earlier compile concurrency experiment](../../validation/backend/results/local-build-reference-20260907/README.md):
  heavy-pool comparisons and memory-pressure observations preceding the complete
  core-build measurement above.
- [Earlier refactor experiments](CudaRefactorSmoke.md): historical smoke/build
  observations, retained for reproducibility rather than universal promises.
- [Archived backend evidence](CudaBackendEvidence.md)
  ([Chinese](CudaBackendEvidence.zh-CN.md)): the previous detailed backend
  reports, including hardware observations and historical qualification claims.
  These are preserved evidence, not the current acceptance decision.

User documentation lives in the project README, [API reference](../Reference.md)
and [backend capabilities](../CudaBackendStatus.md). Quantitative evidence uses
the existing [validation tree](../../validation/README.md): curated module
summaries link to detailed results, input identities and hardware metadata.
Keep reusable helpers in their owning module and run-specific evidence under
`validation/**/results/`. Link the existing user contract rather than copying a
second feature or acceptance checklist into a working note.
