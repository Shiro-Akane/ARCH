/**
 * @file CudaBackendBurnHelm.cu
 * @brief Select the registered compact burn delegate for Helmholtz.
 *
 * Network selection forwards borrowed launch arguments and the caller's stream.
 * The per-network binding instantiates the common ODE and burn policies.
 */

#include "physics/eos/HelmEos.h"
#include "cuda/runtime/burn/CudaBackendBurnRegisteredRoutes.h"

namespace arch::cuda {
cudaError_t launch_cuda_burn_route(
    const dispatch::ResolvedExecutionPlan& plan, DeviceStateView state,
    DeviceGridView grid, std::byte* workspace_storage,
    reduction::ReductionCandidate* candidates, int* statuses,
    DeviceBurnSummary* summary, double burn_dt, HelmEosView eos,
    CudaBurnArguments config, cudaStream_t stream)
{
    return burn_detail::visit_registered_dense_route(
        plan, state, grid, workspace_storage, candidates, statuses, summary,
        burn_dt, eos, config, stream);
}
} // namespace arch::cuda
