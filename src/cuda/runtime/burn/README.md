# Burn runtime interfaces and binding

| Responsibility | Main entries |
| --- | --- |
| Arguments and result interfaces | [CudaBackendBurn.h](CudaBackendBurn.h), [CudaBackendBurnSparse.h](CudaBackendBurnSparse.h), [CudaBurnOdeTypes.h](CudaBurnOdeTypes.h) |
| Dense execution | [CudaBackendBurnImpl.cuh](CudaBackendBurnImpl.cuh), [CudaBackendBurnReduction.cuh](CudaBackendBurnReduction.cuh) |
| Registration and dispatch | [CudaBackendBurnDenseRoutes.h](CudaBackendBurnDenseRoutes.h), [CudaBackendBurnRegisteredRoutes.h](CudaBackendBurnRegisteredRoutes.h), [CudaBackendBurnNetworkRoutes.h](CudaBackendBurnNetworkRoutes.h), [CudaBackendBurnNetworkRouteImpl.cuh](CudaBackendBurnNetworkRouteImpl.cuh) |
| Sparse owners | [CudaBackendBurnSparseFactory.cpp](CudaBackendBurnSparseFactory.cpp), [CudaBackendBurnSparseRoutes.h](CudaBackendBurnSparseRoutes.h), [CudaBackendBurnSparseImpl.cuh](CudaBackendBurnSparseImpl.cuh) |
| Built-in EOS bindings | [routes](routes/README.md) |

The shared [burn solver](../../../numerics/burnsolver/README.md) and
[cell handoff policy](../../../driver/DriverBurnPolicy.h) own ODE, thermodynamic
and accepted-energy calculations. [Microphysics](../../microphysics/README.md)
owns device storage and cuDSS adaptation. Keep declaration headers lightweight
and add networks through registration, without network-name branches in the
runtime. Generated wrappers belong in the build tree.
