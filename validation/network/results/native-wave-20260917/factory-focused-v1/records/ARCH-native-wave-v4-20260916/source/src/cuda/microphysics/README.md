# Microphysics storage and sparse execution

This directory supplies the device storage and solver-library resources needed
by EOS and burning calculations. The index separates long-lived uploaded data
from the work buffers and execution used to solve reaction systems.

| Responsibility | Main entries |
| --- | --- |
| EOS launch contracts | [microphysics_api.h](microphysics_api.h), [common.h](common.h) |
| Immutable EOS/species storage | [device_species_owner.h](device_species_owner.h), [helm_eos_device_owner.h](helm_eos_device_owner.h), [tabular3_eos_device_owner.h](tabular3_eos_device_owner.h), [tabular4_eos_device_owner.h](tabular4_eos_device_owner.h) and their `.cpp` owners |
| Shared owner utilities | [device_eos_owner_utils.h](device_eos_owner_utils.h), [helm_eos_loader.h](helm_eos_loader.h) |
| Generated network storage | [device_network_owner.h](device_network_owner.h) |
| Sparse provider and scaling | [CuDssSparseSolver.h](CuDssSparseSolver.h), [SparseEquilibration.h](SparseEquilibration.h) and their implementations |
| Batched burn execution | [SparseOdeBatch.cuh](SparseOdeBatch.cuh), [SparseBurnCells.cuh](SparseBurnCells.cuh); [SparseBeNrBatch.cuh](SparseBeNrBatch.cuh) contains aliases, not another solver |

Storage owners are responsible for uploading existing host data and subsequently exposing borrowed device views. All underlying EOS, network, ODE, and equilibration mathematics must remain governed by their common authorities in [physics](../../physics/README.md) and [numerics](../../numerics/README.md). The cuDSS adapter handles provider handles, buffers, and error reporting, while the actual registration and dense/sparse routing logic resides in [runtime/burn](../runtime/burn/README.md).
