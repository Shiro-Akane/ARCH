/**
 * @file CudaBackendBurnNetworkRouteImpl.cuh
 * @brief Definition helper for one network x one EOS x all CUDA ODE routes.
 */

#pragma once

#include "CudaBackendBurnImpl.cuh"
#include "CudaBackendBurnNetworkRoutes.h"

#define ARCH_DEFINE_TABULAR_BURN_NETWORK_ROUTE(FUNCTION, NETWORK, EOS) \
    namespace arch::cuda::burn_detail { \
    cudaError_t FUNCTION( \
        const dispatch::ResolvedExecutionPlan& plan, DeviceStateView state, \
        DeviceGridView grid, std::byte* workspace_storage, \
        reduction::ReductionCandidate* candidates, int* statuses, \
        DeviceBurnSummary* summary, double burn_dt, EOS eos, \
        BurnConfigView config, cudaStream_t stream) \
    { \
        return visit_ode_route<NETWORK>( \
            plan, state, grid, workspace_storage, candidates, statuses, \
            summary, burn_dt, eos, config, stream); \
    } \
    }
