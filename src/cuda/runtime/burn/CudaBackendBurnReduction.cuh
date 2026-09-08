/** Shared CUDA burn summary reduction for dense and sparse execution. */
#pragma once
#include "cuda/runtime/CudaBackendTypes.h"
#include "driver/DriverBurnPolicy.h"
#include "driver/ReductionSpec.h"
#include <cuda_runtime.h>

namespace arch::cuda::burn_detail {
template <class EosTag>
__global__ void reduce_burn_kernel(
    const reduction::ReductionCandidate* candidates, const int* statuses,
    int count, DeviceBurnSummary* result)
{
    if (blockIdx.x != 0 || threadIdx.x != 0) return;
    const auto spec = reduction::minimum_spec(
        DriverBurn::INACTIVE_LIMITER_CANDIDATE);
    auto reduced = reduction::begin_reduction(spec);
    amr::CellLogicalKey seed_key{};
    seed_key.logical_i = -1;
    seed_key.component = DriverBurn::BURN_LIMITER_COMPONENT;
    reduction::combine_candidate(
        spec, reduced,
        {DriverBurn::INACTIVE_LIMITER_CANDIDATE, seed_key, true});
    DeviceBurnSummary summary{};
    for (int cell = 0; cell < count; ++cell) {
        reduction::combine_candidate(spec, reduced, candidates[cell]);
        const auto disposition = static_cast<DriverBurn::BurnCellDisposition>(
            statuses[cell]);
        if (disposition == DriverBurn::BurnCellDisposition::InvalidComposition
            || disposition == DriverBurn::BurnCellDisposition::SolverFailed) {
            ++summary.failed_cells;
            if (summary.status == 0) summary.status = statuses[cell];
        }
    }
    const auto finalized = reduction::finalize_reduction(spec, reduced);
    if (finalized.status != reduction::ReductionStatus::Ok
        && summary.status == 0)
        summary.status = -static_cast<int>(finalized.status) - 1;
    summary.limiter = finalized.value;
    *result = summary;
}
} // namespace arch::cuda::burn_detail
