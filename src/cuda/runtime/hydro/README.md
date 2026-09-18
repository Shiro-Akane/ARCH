# Hydro runtime binding

[CudaBackendHydro.h](CudaBackendHydro.h) is the lightweight launch interface.
[CudaBackendHydroControl.cpp](CudaBackendHydroControl.cpp) handles backend state
and stage completion. [CudaBackendHydroInstantiation.cuh](CudaBackendHydroInstantiation.cuh)
supplies the common typed instantiation, bound by the Ideal, Helm and Tabular
translation units in this directory.

Cell and face kernels live in [cuda/hydro](../../hydro/README.md). Flux,
reconstruction and integration methods remain in shared [numerics](../../../numerics/README.md).
Each binding includes its selected EOS implementation, not the whole catalog.
