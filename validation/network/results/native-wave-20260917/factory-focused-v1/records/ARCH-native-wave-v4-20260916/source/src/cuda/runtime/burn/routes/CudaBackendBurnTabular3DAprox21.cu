/**
 * @file CudaBackendBurnTabular3DAprox21.cu
 * @brief Instantiate the aprox21/Tabular3D common burn route.
 *
 * The binding helper supplies ODE dispatch and shared numerical calls; this
 * translation unit selects types only and owns no runtime storage.
 */

#include "physics/eos/Tabular3DEOS.h"
#include "physics/network/aprox21/NetAprox21.h"
#include "cuda/runtime/burn/CudaBackendBurnNetworkRouteImpl.cuh"

ARCH_DEFINE_TABULAR_BURN_NETWORK_ROUTE(
    launch_tabular3d_aprox21_route, ::NetAprox21, Tabular3DEOSView)
#undef ARCH_DEFINE_TABULAR_BURN_NETWORK_ROUTE
