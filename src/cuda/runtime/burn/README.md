# Burn runtime interfaces and binding

| Responsibility | Main entries |
| --- | --- |
| Arguments and result interfaces | [CudaBackendBurn.h](CudaBackendBurn.h), [CudaBackendBurnSparse.h](sparse/CudaBackendBurnSparse.h), [CudaBurnOdeTypes.h](CudaBurnOdeTypes.h) |
| Dense execution | [CudaBackendBurnImpl.cuh](dense/CudaBackendBurnImpl.cuh), [CudaBackendBurnReduction.cuh](CudaBackendBurnReduction.cuh) |
| Registration and dispatch | [CudaBackendBurnDenseRoutes.h](dense/CudaBackendBurnDenseRoutes.h), [CudaBackendBurnRegisteredRoutes.h](dispatch/CudaBackendBurnRegisteredRoutes.h), [CudaBackendBurnNetworkRoutes.h](dispatch/CudaBackendBurnNetworkRoutes.h), [CudaBackendBurnNetworkRouteImpl.cuh](dispatch/CudaBackendBurnNetworkRouteImpl.cuh) |
| Sparse owners | [CudaBackendBurnSparseFactory.cpp](sparse/CudaBackendBurnSparseFactory.cpp), [CudaBackendBurnSparseRoutes.h](sparse/CudaBackendBurnSparseRoutes.h), [CudaBackendBurnSparseImpl.cuh](sparse/CudaBackendBurnSparseImpl.cuh) |
| Built-in EOS bindings | [routes](routes/README.md) |

The shared [burn solver](../../../numerics/burnsolver/README.md) and
[cell handoff policy](../../../driver/stages/DriverBurnPolicy.h) own ODE, thermodynamic
and accepted-energy calculations. [Microphysics](../../microphysics/README.md)
owns device storage and cuDSS adaptation. Keep declaration headers lightweight
and add networks through registration, without network-name branches in the
runtime. Generated wrappers belong in the build tree.
