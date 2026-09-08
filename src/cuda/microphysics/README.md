# Microphysics storage and sparse execution

This functional index groups the closely related owners without moving their
established interfaces during the maintenance freeze.

| Responsibility | Main entries |
| --- | --- |
| EOS launch contracts | [microphysics_api.h](microphysics_api.h), [common.h](common.h) |
| Immutable EOS/species storage | [device_species_owner.h](device_species_owner.h), [helm_eos_device_owner.h](helm_eos_device_owner.h), [tabular3_eos_device_owner.h](tabular3_eos_device_owner.h), [tabular4_eos_device_owner.h](tabular4_eos_device_owner.h) and their `.cpp` owners |
| Shared owner utilities | [device_eos_owner_utils.h](device_eos_owner_utils.h), [helm_eos_loader.h](helm_eos_loader.h) |
| Generated network storage | [device_network_owner.h](device_network_owner.h) |
| Sparse provider and scaling | [CuDssSparseSolver.h](CuDssSparseSolver.h), [SparseEquilibration.h](SparseEquilibration.h) and their implementations |
| Batched burn execution | [SparseOdeBatch.cuh](SparseOdeBatch.cuh), [SparseBurnCells.cuh](SparseBurnCells.cuh); [SparseBeNrBatch.cuh](SparseBeNrBatch.cuh) contains aliases, not another solver |

Storage owners upload existing data and expose borrowed views. EOS, network,
ODE and equilibration mathematics keep their common authorities in
[physics](../../physics/README.md) and [numerics](../../numerics/README.md).
The cuDSS adapter owns provider handles, buffers and error reporting; registration
and dense/sparse routing live in [runtime/burn](../runtime/burn/README.md).
