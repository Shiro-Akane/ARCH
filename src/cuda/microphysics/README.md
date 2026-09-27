# Microphysics storage and sparse execution

This directory supplies the device storage and solver-library resources needed
by EOS and burning calculations. The index separates long-lived uploaded data
from the work buffers and execution used to solve reaction systems.

| Responsibility | Main entries |
| --- | --- |
| EOS launch contracts | [microphysics_api.h](microphysics_api.h), [common.h](common.h) |
| Immutable EOS/species storage | [device_species_owner.h](network/device_species_owner.h), [helm_eos_device_owner.h](eos/owners/helm_eos_device_owner.h), [tabular3_eos_device_owner.h](eos/owners/tabular3_eos_device_owner.h), [tabular4_eos_device_owner.h](eos/owners/tabular4_eos_device_owner.h) and their `.cpp` owners |
| Shared owner utilities | [device_eos_owner_utils.h](eos/device_eos_owner_utils.h), [helm_eos_loader.h](eos/helm_eos_loader.h) |
| Generated network storage | [device_network_owner.h](network/device_network_owner.h) |
| Sparse provider and scaling | [CuDssSparseSolver.h](linalg/CuDssSparseSolver.h), [SparseEquilibration.h](linalg/SparseEquilibration.h) and their implementations |
| Batched burn execution | [SparseOdeBatch.cuh](burn/SparseOdeBatch.cuh), [SparseBurnCells.cuh](burn/SparseBurnCells.cuh) |

Storage owners are responsible for uploading existing host data and subsequently exposing borrowed device views. All underlying EOS, network, ODE, and equilibration mathematics must remain governed by their common authorities in [physics](../../physics/README.md) and [numerics](../../numerics/README.md). The cuDSS adapter handles provider handles, buffers, and error reporting, while the actual registration and dense/sparse routing logic resides in [runtime/burn](../runtime/burn/README.md).

On Linux/WSL, the cuDSS provider loads its configured cuDSS/cuBLAS libraries on first sparse
solver construction. CUDA-enabled executables therefore keep CPU inspection and
preview within the same 1 GiB address-space limit as CPU builds. CMake records the
selected library paths privately in the provider; moving those dependencies
requires reconfiguration. Missing libraries/symbols or an incompatible runtime
version fail the sparse request explicitly. Validation records both the linked
provider archive and its configured runtime libraries.

Native Windows retains direct import-library linking; this package qualifies Linux/WSL execution.
