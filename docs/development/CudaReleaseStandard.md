# CUDA acceptance and publication checklist

Audience: contributors and release reviewers. This is the maintained acceptance
contract, not a user guide or a development diary. User-facing capabilities are
described in the [backend guide](../CudaBackendStatus.md); measured accuracy and
performance are summarized in [Validation](../../validation/README.md).

## Active execution contract

The accepted optimization delivery is based on
`3dab4e25f9877b6eb2112bbf59a8264618e8862a`. The independent
`codex/hpc-cuda-integration` worktree contains the bounded cleanup and local
integration fixes described in the [integration record](HpcCudaIntegration.md).
The owner has authorized committing this work and pushing it to the original
`origin/codex/hpc-cuda-optimization` branch. Do not merge main, update the frozen
branch, publish a tag or create a hosted release as part of this handoff.

Trust source-identified collaborator measurements. Repeat a check when an edit
affects it or a concrete inconsistency makes it unreliable, not merely because
it was run on another machine. The final documentation pass changes no numerical
implementation, compiler settings or runtime parameters.

## Implementation contract

- CPU and CUDA share mathematics, physical models, policy registration, stage
  order, AMR criteria and numerical acceptance budgets. Backend-specific code
  owns kernels, storage, streams, completion and external solver adapters.
- AMR topology and Morton ordering remain host-owned; conservative field
  migration uses shared mathematics on device storage. Preserve transactional
  publication, rollback and field-version checks.
- DenseLU and sparse-provider selection use the registered execution plan and
  total equation count. Keep original-system residual checks and explicit error
  reporting; do not hide provider failures as successful ODE retries.
- Burning commits accepted source energy through the shared thermal handoff.
  BD, ENUC and NSE changes require their focused mathematical tests as well as
  appropriate application checks. CPU/CUDA agreement alone is not an independent
  scientific reference.
- Maintain one complete checkpoint contract, including scientific identity,
  native composition, ENUC and controller state. Same-backend continuation and
  equal-physical-time cross-backend comparison have different purposes; keep
  their existing budgets and distinguish them in reports.
- EOS source formats and generated-network capabilities follow the
  [Reference](../Reference.md) and their owning modules. Do not infer units,
  energy zero, missing components or an NSE model from an arbitrary table.

Consult [implementation ownership](ImplementationOwnership.md) before introducing
a helper, wrapper or new file. Split code by responsibility and include only the
types needed at that boundary; avoid parallel CPU/CUDA formula implementations.

## Verification required for this handoff

| Responsibility | Recorded result |
| --- | --- |
| Shared physics and optimization campaign | 1,188 microphysics runs and 1,155 comparisons passed; Hydro/AMR and large-network records retain their separate scope |
| Local build and focused regression | Core and selected targets built; 352 tooling tests and 12 CPU/CUDA tests passed |
| Optional AMR recorder | 24 runs and 12 exact comparisons passed across one, two and three dimensions |
| Active ENUC with coupled AMR/restart | BD/RKL2 and all transport passed 12 routes and 9 comparisons, with an active limiter witnessed on every route |
| Device instrumentation | Ten focused memory/race checks passed with complete clean reports |
| Documentation and publication | Verify numbers, translations, local links, archive boundaries and staged file types before pushing |

Exact identities, test inputs and resource observations belong to the
[integration record](HpcCudaIntegration.md). The
[performance summary](../../validation/backend/results/hpc-cuda-optimization/README.md)
retains the original campaign measurements. Production 150/200-isotope
correctness is accepted for the tested configurations; further acceleration,
experimental sparse-provider promotion and distributed execution remain separate
work. None is implied by the approximately 5× aprox13 coupled-AMR result.

## Performance and memory policy

Preserve optimized runtime behavior while improving compilation. Do not reduce
physical accuracy or blanket-disable runtime optimization to make a build look
cheaper. Use the existing memory/pressure guard and configurable concurrency.
Modest productive swap is acceptable; sustained memory or IO thrashing is not.
Stop only the owned command tree if its resource guard fails.

Record WSL-visible memory separately from installed host RAM. The i7-10700-class,
16 GB host and RTX 3060 Ti-class 8 GB GPU are a measured mid-range reference,
not a universal hardware minimum. Build concurrency and application capacity
depend on the selected network, backend, compiler and workload.

## Progress/evidence rules

Update the existing summary and record after a successful rerun. Preserve the
actual source, binary, data and parameter identities; a later commit does not
change which tree an earlier test ran on. Record failures and unavailable checks
as such, and distinguish numerical agreement, independent physical references,
performance measurements and instrumented checks.

Keep user guides, contributor contracts and detailed evidence separate. Use
functional names rather than internal milestone codes. Intermediate reviews and
superseded checklists belong in the contributor archive; ordinary documentation
indexes should not lead directly to raw logs or JSON. Generated HDF5, build
products and recovery archives stay outside the publishing commit.

Before publishing, reread this contract and the integration record, check the
remote branch for new work, inspect the staged changes and verify the resulting
remote commit. A later main integration must review independent main changes.
Third-party permissions remain documented in the canonical
[notices](../../THIRD_PARTY_NOTICES.md); tests do not establish licensing rights.

<details>
<summary>Historical acceptance decisions — contributor reference</summary>

The [archived checklist](archive/CudaReleaseStandard.md) preserves earlier
decisions, attempted checks and source-specific outcomes. It is not an additional
active plan. The [archive index](archive/README.md) groups related investigations.

</details>
