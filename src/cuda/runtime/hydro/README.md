# Hydro runtime binding

[CudaBackendHydro.h](CudaBackendHydro.h) is the lightweight launch interface.
[CudaBackendHydroControl.cpp](CudaBackendHydroControl.cpp) handles backend state
and stage completion. [CudaBackendHydroInstantiation.cuh](CudaBackendHydroInstantiation.cuh)
supplies the common typed instantiation, bound by the Ideal, Helm and Tabular
translation units in this directory.

While cell and face kernels reside in [cuda/hydro](../../hydro/README.md), the mathematical definitions for fluxes, reconstruction, and time-integration remain firmly in the common [numerics](../../../numerics/README.md) modules. You must rigorously preserve this clear include boundary whenever you add a new policy, explicitly avoiding the temptation to blindly import every EOS into each individual binding.
