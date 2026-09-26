# Hydro runtime binding

[CudaBackendHydro.h](CudaBackendHydro.h) is the lightweight launch interface.
[CudaBackendHydroControl.cpp](CudaBackendHydroControl.cpp) handles backend state
and stage completion. [CudaBackendHydroInstantiation.cuh](CudaBackendHydroInstantiation.cuh)
supplies the common typed instantiation, bound by the Ideal, Helm and Tabular
translation units in this directory.

Cell and face kernels live in [cuda/hydro](../../hydro/README.md). Flux,
reconstruction and integration methods remain in shared [numerics](../../../numerics/README.md).
Each binding includes its selected EOS implementation, not the whole catalog.

The runtime retains capacity for CFL batch bindings and per-block mean EOS
scratch. Each call rebuilds bindings from the current storage generation; each
stage rebuilds thermodynamic values from its selected input slot. Stream
completion precedes metadata destruction, status consumption and state
publication. Separate block latches preserve early failures across a batch.
