/**
 * @file CudaBackendBurnTabular3DIso7.cu
 * @brief Instantiate the iso7/Tabular3D common burn route.
 *
 * The binding helper supplies ODE dispatch and shared numerical calls; this
 * translation unit selects types only and owns no runtime storage.
 * Workflow:
 * 1. Receive an EOS and network route selected at dispatch.
 * 2. Instantiate the matching CUDA burn entry point once.
 * 3. Pass all scalar chemistry through the common mathematical library.
 */

#include "physics/eos/tabular/Tabular3DEOS.h"
#include "physics/network/iso7/NetIso7.h"
#include "cuda/runtime/burn/dispatch/CudaBackendBurnNetworkRouteImpl.cuh"

ARCH_DEFINE_TABULAR_BURN_NETWORK_ROUTE(
    launch_tabular3d_iso7_route, ::NetIso7, Tabular3DEOSView)
#undef ARCH_DEFINE_TABULAR_BURN_NETWORK_ROUTE
