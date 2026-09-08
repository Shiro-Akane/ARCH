/**
 * @file CudaBackendBurnTabular4DIso7.cu
 * @brief Instantiate the iso7/Tabular4D common burn route.
 *
 * The binding helper supplies ODE dispatch and shared numerical calls; this
 * translation unit selects types only and owns no runtime storage.
 */

#include "physics/eos/Tabular4DEOS.h"
#include "physics/network/iso7/NetIso7.h"
#include "cuda/runtime/burn/CudaBackendBurnNetworkRouteImpl.cuh"

ARCH_DEFINE_TABULAR_BURN_NETWORK_ROUTE(
    launch_tabular4d_iso7_route, ::NetIso7, Tabular4DEOSView)
#undef ARCH_DEFINE_TABULAR_BURN_NETWORK_ROUTE
