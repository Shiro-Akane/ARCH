#include "physics/network/iso7/NetIso7.h"
#include "CudaBackendBurnNetworkRouteImpl.cuh"

ARCH_DEFINE_TABULAR_BURN_NETWORK_ROUTE(
    launch_tabular4d_iso7_route, ::NetIso7, Tabular4DEOSView)
#undef ARCH_DEFINE_TABULAR_BURN_NETWORK_ROUTE
