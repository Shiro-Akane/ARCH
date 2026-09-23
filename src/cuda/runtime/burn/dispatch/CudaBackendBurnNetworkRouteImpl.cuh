/**
 * @file CudaBackendBurnNetworkRouteImpl.cuh
 * @brief Definition helper for one network x one EOS x all CUDA ODE routes.
 * Workflow:
 * 1. Read the resolved EOS, network and ODE route.
 * 2. Choose the matching compiled CUDA burn launcher.
 * 3. Fail explicitly when a route is unsupported.
 */

#pragma once

#include "cuda/runtime/burn/dense/CudaBackendBurnImpl.cuh"
#include "cuda/runtime/burn/dispatch/CudaBackendBurnNetworkRoutes.h"

#define ARCH_DEFINE_TABULAR_BURN_NETWORK_ROUTE(FUNCTION, NETWORK, EOS) \
    namespace arch::cuda::burn_detail { \
    cudaError_t FUNCTION( \
        const dispatch::ResolvedExecutionPlan& plan, DeviceStateView state, \
        DeviceGridView grid, std::byte* workspace_storage, \
        reduction::ReductionCandidate* candidates, int* statuses, \
        DeviceBurnSummary* summary, double burn_dt, EOS eos, \
        CudaBurnArguments config, cudaStream_t stream) \
    { \
        return visit_ode_route<NETWORK>( \
            plan, state, grid, workspace_storage, candidates, statuses, \
            summary, burn_dt, eos, config, stream); \
    } \
    }
