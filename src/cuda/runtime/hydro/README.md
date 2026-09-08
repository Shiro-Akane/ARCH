# Hydro runtime binding

[CudaBackendHydro.h](CudaBackendHydro.h) is the lightweight launch interface.
[CudaBackendHydroControl.cpp](CudaBackendHydroControl.cpp) handles backend state
and stage completion. [CudaBackendHydroInstantiation.cuh](CudaBackendHydroInstantiation.cuh)
supplies the common typed instantiation, bound by the Ideal, Helm and Tabular
translation units in this directory.

Cell and face kernels live in [cuda/hydro](../../hydro/README.md); fluxes,
reconstruction and time-integration mathematics remain in the common
[numerics](../../../numerics/README.md) owners. Preserve this include boundary
when adding a policy rather than importing every EOS into each binding.
