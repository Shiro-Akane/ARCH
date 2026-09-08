/**
 * @file CudaBackendBurnTabular3D.cu
 * @brief Select a registered Tabular3D burn network delegate.
 *
 * Built-in and generated routes forward the same launch arguments. Numerical
 * work is instantiated by the selected network/EOS binding, not this switch.
 */

#include "physics/eos/Tabular3DEOS.h"
#include "cuda/runtime/burn/CudaBackendBurnNetworkRoutes.h"

namespace arch::cuda {
cudaError_t launch_cuda_burn_route(
    const dispatch::ResolvedExecutionPlan& plan, DeviceStateView state,
    DeviceGridView grid, std::byte* workspace_storage,
    reduction::ReductionCandidate* candidates, int* statuses,
    DeviceBurnSummary* summary, double burn_dt, Tabular3DEOSView eos,
    CudaBurnArguments config, cudaStream_t stream)
{
    using Route = decltype(
        &burn_detail::launch_tabular3d_aprox13_route);
    Route route = nullptr;
    switch (plan.network) {
    case dispatch::NetworkId::Aprox13:
        route = burn_detail::launch_tabular3d_aprox13_route;
        break;
    case dispatch::NetworkId::Aprox19:
        route = burn_detail::launch_tabular3d_aprox19_route;
        break;
    case dispatch::NetworkId::Aprox21:
        route = burn_detail::launch_tabular3d_aprox21_route;
        break;
    case dispatch::NetworkId::Iso7:
        route = burn_detail::launch_tabular3d_iso7_route;
        break;
#define ARCH_SELECT_CUSTOM_TABULAR_ROUTE(TAG, VALUE, NAME, TYPE) \
    case dispatch::NetworkId::TAG: \
        route = burn_detail::launch_tabular3d_##TAG##_route; break;
    ARCH_FOR_EACH_CUDA_CUSTOM_NETWORK(ARCH_SELECT_CUSTOM_TABULAR_ROUTE)
#undef ARCH_SELECT_CUSTOM_TABULAR_ROUTE
    default:
        return cudaErrorInvalidValue;
    }
    return route(
        plan, state, grid, workspace_storage, candidates, statuses, summary,
        burn_dt, eos, config, stream);
}
} // namespace arch::cuda
