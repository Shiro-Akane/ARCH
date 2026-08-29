#include "CudaBackendBurnImpl.cuh"

namespace arch::cuda {
cudaError_t launch_cuda_burn_route(
    const dispatch::ResolvedExecutionPlan& plan, DeviceStateView state,
    DeviceGridView grid, std::byte* workspace_storage,
    reduction::ReductionCandidate* candidates, int* statuses,
    DeviceBurnSummary* summary, double burn_dt, IdealGasView eos,
    BurnConfigView config, cudaStream_t stream)
{
    return burn_detail::visit_route(
        plan, state, grid, workspace_storage, candidates, statuses, summary,
        burn_dt, eos, config, stream);
}
} // namespace arch::cuda
