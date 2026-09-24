/**
 * @file CudaBackendCoordinateSeam.cpp
 * @brief Bind the shared singular-coordinate AMR donor plan to device slots.
 *
 * Workflow:
 * 1. Resolve active (or unpublished staged) block IDs to checked device views.
 * 2. Lower only immutable donor stencils and upload them to reusable storage.
 * 3. Run the shared reconstruction kernel on the backend stream, checking its
 *    donor status before the scheduler publishes completed device ghosts.
 *
 * The bulk fluid fields stay resident. Only plan metadata and one status word
 * cross the bus; Host and CUDA call the same CoordinateSeamMath.h arithmetic.
 */

#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

#include "amr/exchange/CoordinateSeamPlan.h"
#include "cuda/runtime/control/CudaBackendInternal.h"

namespace arch::cuda {

/** Finish a normal stage-slot seam after physical, same-level and AMR exchange. */
state::CompletionToken CudaBackend::execute_coordinate_seam_exchange(
    std::span<const backend::BackendStateAccess> accesses,
    std::span<const int> active_ids,
    const amr::CoordinateSeamPlan& plan, state::StateSlot slot,
    state::StateVersion source_version, state::CompletionToken expected)
{
    if (!state::is_valid(source_version) || !complete_token(expected))
        throw std::invalid_argument("invalid CUDA coordinate seam completion");
    impl_->execute_coordinate_seam_exchange(accesses, active_ids, plan, slot,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            return impl_->require_block(access);
        });
    return expected;
}

/** Execute the same plan against either active or private migration storage. */
void CudaBackend::Impl::execute_coordinate_seam_exchange(
    std::span<const backend::BackendStateAccess> accesses,
    std::span<const int> active_ids,
    const amr::CoordinateSeamPlan& plan, state::StateSlot slot,
    const BlockResolver& resolve)
{
    if (plan.transfers.empty()) return;
    if (active_ids.size() != accesses.size() || accesses.empty()
        || accesses.size()
            > static_cast<std::size_t>(std::numeric_limits<int>::max())
        || plan.transfers.size()
            > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("invalid CUDA coordinate seam bindings");

    std::map<int, int> index_by_id;
    std::vector<DeviceExchangeBlock> blocks;
    blocks.reserve(accesses.size());
    int species_count = -1;
    for (std::size_t index = 0; index < accesses.size(); ++index) {
        const auto access = accesses[index];
        if (access.slot != slot || !index_by_id.emplace(active_ids[index],
                static_cast<int>(index)).second)
            throw std::invalid_argument("stale or duplicate CUDA coordinate seam binding");
        CudaBlockRuntime& block = resolve(access);
        const DeviceStateView selected = block.require_access(access);
        if (species_count >= 0 && selected.n_species != species_count)
            throw std::invalid_argument("CUDA coordinate seam species counts differ");
        species_count = selected.n_species;
        blocks.push_back({selected, block.grid});
    }

    std::vector<DeviceCoordinateSeamTransfer> lowered;
    lowered.reserve(plan.transfers.size());
    for (const auto& transfer : plan.transfers) {
        const auto source = index_by_id.find(transfer.source_id);
        const auto destination = index_by_id.find(transfer.destination_id);
        if (source == index_by_id.end() || destination == index_by_id.end())
            throw std::invalid_argument("CUDA coordinate seam donor is not active");
        const auto& source_view = blocks[source->second].state;
        const auto& destination_view = blocks[destination->second].state;
        const auto valid_cell = [](int cell, int extent) {
            return cell >= 0 && cell < extent;
        };
        if (!valid_cell(transfer.source_center, source_view.total_size)
            || !valid_cell(transfer.destination_cell, destination_view.total_size))
            throw std::out_of_range("CUDA coordinate seam cell is outside layout");
        for (const int neighbor : transfer.source_neighbor)
            if (!valid_cell(neighbor, source_view.total_size))
                throw std::out_of_range("CUDA coordinate seam slope is outside layout");
        lowered.push_back({transfer, source->second, destination->second});
    }

    select_device();
    auto& device_blocks = exchange_scratch.blocks;
    auto& device_transfers = exchange_scratch.seam_transfers;
    auto& device_status = exchange_scratch.status;
    device_blocks.reserve(blocks.size());
    device_transfers.reserve(lowered.size());
    device_status.reserve(1);
    CudaQuiescenceGuard guard{*this};
    enqueue_cuda_metadata_upload(device_blocks.get(), blocks.data(),
        blocks.size() * sizeof(DeviceExchangeBlock), stream.get(),
        runtime_counters, "upload CUDA coordinate seam blocks");
    enqueue_cuda_metadata_upload(device_transfers.get(), lowered.data(),
        lowered.size() * sizeof(DeviceCoordinateSeamTransfer), stream.get(),
        runtime_counters, "upload CUDA coordinate seam transfers");
    check_cuda(launch_cuda_backend_coordinate_seam(device_blocks.get(),
        device_transfers.get(), static_cast<int>(lowered.size()),
        device_status.get(), stream.get()), "launch CUDA coordinate seam");
    int status = 0;
    check_cuda(cudaMemcpyAsync(&status, device_status.get(), sizeof(status),
        cudaMemcpyDeviceToHost, stream.get()),
        "download CUDA coordinate seam status");
    checked_quiesce("complete CUDA coordinate seam");
    guard.completed = true;
    ++runtime_counters.kernel_count;
    runtime_counters.bytes_d2h += sizeof(status);
    if (status != 0)
        throw std::invalid_argument("CUDA coordinate seam donor has invalid fluid state");
}

} // namespace arch::cuda
