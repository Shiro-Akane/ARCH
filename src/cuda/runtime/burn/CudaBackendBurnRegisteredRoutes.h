/** Lightweight registered network dispatch, with no numerical instantiations. */
#pragma once

#include "cuda/runtime/burn/CudaBackendBurnDenseRoutes.h"
#include <type_traits>

namespace arch::cuda::burn_detail {

template<class Eos>
struct RegisteredDenseNetworkVisitor {
    using Route = decltype(&launch_dense_burn_network_route<
        dispatch::CudaIso7Binding, Eos>);
    Route route = nullptr;

    template<class Registration> void operator()()
    {
        using Binding = typename dispatch::PolicyRegistration<Registration>::CudaBinding;
        if constexpr (!std::is_same_v<Binding, dispatch::AbsentBinding>
                      && !std::is_same_v<Binding, dispatch::CudaNoNetworkBinding>)
            route = &launch_dense_burn_network_route<Binding, Eos>;
    }
};

template<class Eos>
cudaError_t visit_registered_dense_route(
    const dispatch::ResolvedExecutionPlan& plan, DeviceStateView state,
    DeviceGridView grid, std::byte* workspace_storage,
    reduction::ReductionCandidate* candidates, int* statuses,
    DeviceBurnSummary* summary, double burn_dt, Eos eos,
    CudaBurnArguments config, cudaStream_t stream)
{
    RegisteredDenseNetworkVisitor<Eos> visitor;
    if (!dispatch::visit_policy<dispatch::NetworkPolicies>(plan.network, visitor)
        || visitor.route == nullptr)
        return cudaErrorInvalidValue;
    // Argument and ODE-policy validation remain in the shared single-network
    // entry point. This layer only selects the registered external delegate.
    return visitor.route(plan, state, grid, workspace_storage, candidates,
        statuses, summary, burn_dt, eos, config, stream);
}

} // namespace arch::cuda::burn_detail
