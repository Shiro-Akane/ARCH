/**
 * @file CudaBackendBurnNetworkRoutes.h
 * @brief Internal link seam for the split Tabular CUDA burn matrix.
 *
 * This is not a second burn API.  The public launch_cuda_burn_route overloads
 * remain the only backend entry points; these declarations merely keep their
 * runtime network switch separate from the network-specific NVCC templates.
 */

#pragma once

#include "cuda/runtime/burn/CudaBackendBurn.h"

namespace arch::cuda::burn_detail {

#define ARCH_DECLARE_TABULAR_BURN_NETWORK_ROUTE(FUNCTION, EOS) \
    cudaError_t FUNCTION( \
        const dispatch::ResolvedExecutionPlan& plan, DeviceStateView state, \
        DeviceGridView grid, std::byte* workspace_storage, \
        reduction::ReductionCandidate* candidates, int* statuses, \
        DeviceBurnSummary* summary, double burn_dt, EOS eos, \
        CudaBurnArguments config, cudaStream_t stream)

ARCH_DECLARE_TABULAR_BURN_NETWORK_ROUTE(
    launch_tabular3d_aprox13_route, Tabular3DEOSView);
ARCH_DECLARE_TABULAR_BURN_NETWORK_ROUTE(
    launch_tabular3d_aprox19_route, Tabular3DEOSView);
ARCH_DECLARE_TABULAR_BURN_NETWORK_ROUTE(
    launch_tabular3d_aprox21_route, Tabular3DEOSView);
ARCH_DECLARE_TABULAR_BURN_NETWORK_ROUTE(
    launch_tabular3d_iso7_route, Tabular3DEOSView);

ARCH_DECLARE_TABULAR_BURN_NETWORK_ROUTE(
    launch_tabular4d_aprox13_route, Tabular4DEOSView);
ARCH_DECLARE_TABULAR_BURN_NETWORK_ROUTE(
    launch_tabular4d_aprox19_route, Tabular4DEOSView);
ARCH_DECLARE_TABULAR_BURN_NETWORK_ROUTE(
    launch_tabular4d_aprox21_route, Tabular4DEOSView);
ARCH_DECLARE_TABULAR_BURN_NETWORK_ROUTE(
    launch_tabular4d_iso7_route, Tabular4DEOSView);

// Generated packages use the same registered ID and ODE visitor as built-ins;
// CMake emits narrow delegate TUs, never another copy of the numerical kernels.
#define ARCH_DECLARE_CUSTOM_TABULAR_ROUTES(TAG, VALUE, NAME, TYPE) \
    ARCH_DECLARE_TABULAR_BURN_NETWORK_ROUTE(launch_tabular3d_##TAG##_route, Tabular3DEOSView); \
    ARCH_DECLARE_TABULAR_BURN_NETWORK_ROUTE(launch_tabular4d_##TAG##_route, Tabular4DEOSView);
ARCH_FOR_EACH_CUDA_CUSTOM_NETWORK(ARCH_DECLARE_CUSTOM_TABULAR_ROUTES)
#undef ARCH_DECLARE_CUSTOM_TABULAR_ROUTES

#undef ARCH_DECLARE_TABULAR_BURN_NETWORK_ROUTE

} // namespace arch::cuda::burn_detail
