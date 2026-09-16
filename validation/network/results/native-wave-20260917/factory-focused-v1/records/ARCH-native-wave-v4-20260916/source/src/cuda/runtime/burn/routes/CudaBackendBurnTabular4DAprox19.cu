/**
 * @file CudaBackendBurnTabular4DAprox19.cu
 * @brief Instantiate the aprox19/Tabular4D common burn route.
 *
 * The binding helper supplies ODE dispatch and shared numerical calls; this
 * translation unit selects types only and owns no runtime storage.
 */

#include "physics/eos/Tabular4DEOS.h"
#include "physics/network/aprox19/NetAprox19.h"
#include "cuda/runtime/burn/CudaBackendBurnNetworkRouteImpl.cuh"

ARCH_DEFINE_TABULAR_BURN_NETWORK_ROUTE(
    launch_tabular4d_aprox19_route, ::NetAprox19, Tabular4DEOSView)
#undef ARCH_DEFINE_TABULAR_BURN_NETWORK_ROUTE
