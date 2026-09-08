# Burn runtime interfaces and binding

| Responsibility | Main entries |
| --- | --- |
| Arguments and result interfaces | [CudaBackendBurn.h](CudaBackendBurn.h), [CudaBackendBurnSparse.h](CudaBackendBurnSparse.h), [CudaBurnOdeTypes.h](CudaBurnOdeTypes.h) |
| Dense execution | [CudaBackendBurnImpl.cuh](CudaBackendBurnImpl.cuh), [CudaBackendBurnReduction.cuh](CudaBackendBurnReduction.cuh) |
| Registration and dispatch | [CudaBackendBurnDenseRoutes.h](CudaBackendBurnDenseRoutes.h), [CudaBackendBurnRegisteredRoutes.h](CudaBackendBurnRegisteredRoutes.h), [CudaBackendBurnNetworkRoutes.h](CudaBackendBurnNetworkRoutes.h), [CudaBackendBurnNetworkRouteImpl.cuh](CudaBackendBurnNetworkRouteImpl.cuh) |
| Sparse owners | [CudaBackendBurnSparseFactory.cpp](CudaBackendBurnSparseFactory.cpp), [CudaBackendBurnSparseRoutes.h](CudaBackendBurnSparseRoutes.h), [CudaBackendBurnSparseImpl.cuh](CudaBackendBurnSparseImpl.cuh) |
| Built-in EOS bindings | [routes](routes/README.md) |

All core ODE, thermodynamic, and accepted-energy mathematics are securely sourced from the shared [burn solver](../../../numerics/burnsolver/README.md) and the [cell handoff policy](../../../driver/DriverBurnPolicy.h). In contrast, device storage and any cuDSS adaptations are maintained within [microphysics](../../microphysics/README.md). Always keep your declaration headers lightweight, preferring to seamlessly extend the registration system rather than injecting rigid network-name branches. Finally, remember that all generated wrappers remain strictly confined to the build tree.
