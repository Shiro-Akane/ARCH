/**
 * @file CudaBackendBurnNetworkRoutes.h
 * @brief Internal link seam for the split Tabular CUDA burn matrix.
 *
 * This is not a second burn API.  The public launch_cuda_burn_route overloads
 * remain the only backend entry points; these declarations merely keep their
 * runtime network switch separate from the network-specific NVCC templates.
 */

#pragma once

#include "CudaBackendBurn.h"

namespace arch::cuda::burn_detail {

static_assert(
    ARCH_CUSTOM_CUDA_NETWORK_COUNT == 0,
    "CUDA custom networks require an explicit split burn route registration");

#define ARCH_DECLARE_TABULAR_BURN_NETWORK_ROUTE(FUNCTION, EOS) \
    cudaError_t FUNCTION( \
        const dispatch::ResolvedExecutionPlan& plan, DeviceStateView state, \
        DeviceGridView grid, std::byte* workspace_storage, \
        reduction::ReductionCandidate* candidates, int* statuses, \
        DeviceBurnSummary* summary, double burn_dt, EOS eos, \
        BurnConfigView config, cudaStream_t stream)

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

#undef ARCH_DECLARE_TABULAR_BURN_NETWORK_ROUTE

} // namespace arch::cuda::burn_detail
