/**
 * @file CudaBackendBurnReduction.cuh
 * @brief Reduce dense and sparse CUDA burn outcomes into one summary.
 *
 * The kernel reads borrowed cell candidates and dispositions using the common
 * ReductionSpec semantics. The runtime owns the summary buffer and must complete
 * the stream before the host uses its limiter or failure information.
 */
#pragma once
#include "cuda/runtime/CudaBackendTypes.h"
#include "cuda/runtime/burn/CudaBackendBurn.h"
#include "driver/stages/DriverBurnPolicy.h"
#include "driver/schedule/ReductionSpec.h"
#include <cuda_runtime.h>

namespace arch::cuda::burn_detail {
template <class EosTag>
__global__ void reduce_burn_kernel(
    const reduction::ReductionCandidate* candidates, const int* statuses,
    int count, DeviceBurnSummary* result,
    const DeviceBurnBatchBlock* blocks = nullptr)
{
    if (blockIdx.x != 0 || threadIdx.x != 0) return;
    if (blocks) {
        const auto& block = blocks[blockIdx.y];
        candidates = block.candidates;
        statuses = block.statuses;
        count = block.grid.active_cell_count();
        result = block.summary;
    }
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
