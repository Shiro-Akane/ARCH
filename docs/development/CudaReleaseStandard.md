# CUDA implementation and validation

[Backend guide](../CudaBackendStatus.md) · [Implementation ownership](ImplementationOwnership.md) · [Validation](../../validation/README.md)

CPU and CUDA share physical models, mathematical algorithms, policy registration, stage order, AMR criteria and numerical acceptance budgets. Backend-specific code manages kernels, storage, streams, completion and native solver resources.

## Implementation contracts

- AMR topology and Morton ordering are Host-owned. Conservative field migration uses shared mathematics on device storage; topology publication, rollback and field-version checks remain transactional.
- DenseLU and sparse-provider selection follow the registered execution plan and total equation count. The original-system residual determines numerical acceptance; provider failures have explicit errors.
- Burning commits accepted source energy through the common thermal coupling. BD, ENUC and NSE changes require their mathematical tests and applicable full-program comparisons.
- Checkpoints contain scientific identity, native composition, ENUC and controller state. Test same-backend continuation and cross-backend comparisons at equal physical time using their respective budgets.
- EOS data and generated networks follow the [Reference](../Reference.md) and their module contracts. Units, energy reference, physical components and equilibrium assumptions must be declared by the input format.

Use the existing owner when adding an adapter or helper. Device traversal and storage can differ while numerical formulas remain shared.

## Verification

Run focused checks for the affected implementation and its failure paths. Independent analytic or manufactured references test scientific correctness; CPU/CUDA comparison tests backend agreement. Device instrumentation checks memory and synchronization behavior. Full-program tests exercise model, method, AMR and restart combinations.

ARCH's required tests are self-contained. Comparisons with FLASH or other simulation codes are optional research activities. Extend existing test owners and keep the accepted numerical thresholds. A source change requires the relevant checks for that revision; a result record identifies its source, binary, parameters, data and hardware.

## Performance and resources

Measure runtime and peak memory on representative inputs before changing an execution path. Compare equal physical endpoints and numerical accuracy. Keep strict floating-point behavior and the shared numerical contracts while tuning compilation or concurrency.

Use the existing memory/pressure guard for large builds. Record WSL-visible memory separately from installed system RAM. Compiler/network choices affect both compilation cost and runtime capacity. If a resource guard stops a command, preserve the diagnostics and adjust the workload or concurrency before another attempt.

## Reports and publication

A useful validation summary states the problem, reference, configuration, error, performance measurements and applicable limits. Preserve failure records needed to interpret a result. Store scientific inputs and compact processed results under the corresponding validation module; keep raw HDF5, checkpoints and full local logs outside source packages.

Before publication, verify the reviewed source revision, affected tests, dependency notices and package contents. The [release notes](../releases/README.md) describe public versions; [third-party notices](../../THIRD_PARTY_NOTICES.md) retain code and data attribution.
