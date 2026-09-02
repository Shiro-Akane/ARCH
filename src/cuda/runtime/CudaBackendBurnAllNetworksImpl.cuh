/**
 * @file CudaBackendBurnAllNetworksImpl.cuh
 * @brief Full network visitor retained for memory-safe Ideal/Helm burn TUs.
 *
 * Tabular EOS launchers deliberately do not include this file.  They select a
 * narrow external route so NVCC sees only one network's ODE instantiations per
 * translation unit.
 */

#pragma once

#include "CudaBackendBurnImpl.cuh"
#include "CudaBurnNetworkTypes.h"

namespace arch::cuda::burn_detail {

template <class Eos>
struct NetworkRouteVisitor {
    RouteContext<Eos>& context;

    template <class NetworkRegistration>
    void operator()()
    {
        using NetworkBinding = typename dispatch::PolicyRegistration<
            NetworkRegistration>::CudaBinding;
        if constexpr (!std::is_same_v<NetworkBinding, dispatch::AbsentBinding>
                      && !std::is_same_v<
                          NetworkBinding, dispatch::CudaNoNetworkBinding>) {
            using Network = NetworkTypeFor<NetworkBinding>;
            OdeRouteVisitor<Network, Eos> visitor{context};
            const bool ode_found = dispatch::visit_policy<
                dispatch::OdeSolverPolicies>(
                    context.plan.ode_solver, visitor);
            context.invoked = context.invoked && ode_found;
        }
    }
};

template <class Eos>
cudaError_t visit_route(
    const dispatch::ResolvedExecutionPlan& plan, DeviceStateView state,
    DeviceGridView grid, std::byte* workspace_storage,
    reduction::ReductionCandidate* candidates, int* statuses,
    DeviceBurnSummary* summary, double burn_dt, Eos eos,
    BurnConfigView config, cudaStream_t stream)
{
    if (!valid_route_arguments(
            plan, state, grid, workspace_storage, candidates, statuses,
            summary, burn_dt, eos, config, stream))
        return cudaErrorInvalidValue;
    RouteContext<Eos> context{
        plan, state, grid, workspace_storage, candidates, statuses, summary,
        burn_dt, eos, config, stream};
    NetworkRouteVisitor<Eos> visitor{context};
    const bool network_found = dispatch::visit_policy<
        dispatch::NetworkPolicies>(plan.network, visitor);
    return network_found && context.invoked
        ? context.result : cudaErrorInvalidValue;
}

} // namespace arch::cuda::burn_detail
