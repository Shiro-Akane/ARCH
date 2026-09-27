/**
 * @file CudaBackendBurnTabular4DAprox13.cu
 * @brief Instantiate the aprox13/Tabular4D common burn route.
 *
 * The binding helper supplies ODE dispatch and shared numerical calls; this
 * translation unit selects types only and owns no runtime storage.
 * Workflow:
 * 1. Receive an EOS and network route selected at dispatch.
 * 2. Instantiate the matching CUDA burn entry point once.
 * 3. Pass all scalar chemistry through the common mathematical library.
 */

#include "physics/eos/tabular/Tabular4DEOS.h"
#include "physics/network/aprox13/NetAprox13.h"
#include "cuda/runtime/burn/dispatch/CudaBackendBurnNetworkRouteImpl.cuh"

ARCH_DEFINE_TABULAR_BURN_NETWORK_ROUTE(
    launch_tabular4d_aprox13_route, ::NetAprox13, Tabular4DEOSView)
#undef ARCH_DEFINE_TABULAR_BURN_NETWORK_ROUTE
