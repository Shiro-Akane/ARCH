# Burn runtime interfaces and binding

| Responsibility | Main entries |
| --- | --- |
| Arguments and result interfaces | [CudaBackendBurn.h](CudaBackendBurn.h), [CudaBackendBurnSparse.h](CudaBackendBurnSparse.h), [CudaBurnOdeTypes.h](CudaBurnOdeTypes.h) |
| Dense execution | [CudaBackendBurnImpl.cuh](CudaBackendBurnImpl.cuh), [CudaBackendBurnReduction.cuh](CudaBackendBurnReduction.cuh) |
| Registration and dispatch | [CudaBackendBurnDenseRoutes.h](CudaBackendBurnDenseRoutes.h), [CudaBackendBurnRegisteredRoutes.h](CudaBackendBurnRegisteredRoutes.h), [CudaBackendBurnNetworkRoutes.h](CudaBackendBurnNetworkRoutes.h), [CudaBackendBurnNetworkRouteImpl.cuh](CudaBackendBurnNetworkRouteImpl.cuh) |
| Sparse owners | [CudaBackendBurnSparseFactory.cpp](CudaBackendBurnSparseFactory.cpp), [CudaBackendBurnSparseRoutes.h](CudaBackendBurnSparseRoutes.h), [CudaBackendBurnSparseImpl.cuh](CudaBackendBurnSparseImpl.cuh) |
| Built-in EOS bindings | [routes](routes/README.md) |

All ODE, thermodynamic and accepted-energy mathematics come from the shared
[burn solver](../../../numerics/burnsolver/README.md) and
[cell handoff policy](../../../driver/DriverBurnPolicy.h). Device storage and
cuDSS adaptation live in [microphysics](../../microphysics/README.md).
Keep declaration headers lightweight and extend the registration system rather
than adding network-name branches. Generated wrappers stay under the build tree.
