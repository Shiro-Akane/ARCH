# CUDA acceptance and publication checklist

Audience: contributors and release reviewers. This is the maintained acceptance
contract, not a user guide or a development diary. User-facing capabilities are
described in the [backend guide](../CudaBackendStatus.md); measured accuracy and
performance are summarized in [Validation](../../validation/README.md).

## Current execution contract

The implementation invariants below apply to current work. The
[release plan](../../ARCH_RELEASE_PLAN.md) alone owns execution order, current
qualification, costs and remaining limitations. Historical evidence retains its
original source, inputs and platform; it cannot qualify a newer executable by
matching a test count or an earlier speedup.

ARCH build, test, CI and release acceptance use self-contained scientific checks.
FLASH and other simulation programs are optional developer comparisons, with no
required installation, build, run or reference-output dependency. Consolidate
new coverage into existing test owners and review measured CI runtime before
stage delivery, as defined in the release plan. External comparisons and long
performance campaigns stay outside the required workflow.

Trust source-identified collaborator measurements. Repeat a check when an edit
affects it or a concrete inconsistency makes it unreliable, not merely because
it was run on another machine. Keep scientific budgets, ordinary AMR decisions,
shared numerical ownership and topology transactions explicit. A stage report
must distinguish correctness, performance and any unmet release gate.

The historical HPC integration used
`3dab4e25f9877b6eb2112bbf59a8264618e8862a` and the
`codex/hpc-cuda-integration` worktree. Its recorder-removal scope and original
branch handoff are retained in the [integration record](../histroy/integration/HpcCudaIntegration.md).
That completed handoff does not select a branch or authorize a release for
subsequent work.

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

## Historical integration evidence

The [integration record](../histroy/integration/HpcCudaIntegration.md) and
[performance summary](../../validation/backend/results/hpc-cuda-optimization/README.md)
retain the original recorder removal, coupled source-energy, generated-network
and device-instrumentation scope. Their original failures, measured speedups and
source identities remain there. Current scientific and device qualification is
summarized only in the release plan; publication still needs its final exits.

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

The [archived checklist](../histroy/CudaReleaseStandard.md) preserves earlier
decisions, attempted checks and source-specific outcomes. It is not an additional
active plan. The [archive index](archive/README.md) groups related investigations.

</details>
