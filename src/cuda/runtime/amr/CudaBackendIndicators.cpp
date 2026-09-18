/**
 * @file CudaBackendIndicators.cpp
 * @brief Gather device refinement summaries for host mesh decisions.
 *
 * Reuse backend-owned scratch across stream-ordered blocks, evaluate shared
 * indicators with the selected EOS and download one scalar per block. Complete
 * the batch before host consumption or scratch retirement, then reject invalid
 * summaries before topology work.
 */

#include "cuda/runtime/control/CudaBackendInternal.h"
#include "cuda/amr/RefinementIndicators.h"
#include <cmath>

namespace arch::cuda {
namespace {
template <class T>
T* reserve_indicator_scratch(std::unique_ptr<DeviceAllocation<T>>& owner, std::size_t count)
{
    if (count == 0) return nullptr;
    if (!owner || owner->size() < count) {
        auto replacement = std::make_unique<DeviceAllocation<T>>();
        replacement->allocate(count);
        owner.swap(replacement);
    }
    return owner->get();
}
} // namespace

std::vector<double> CudaBackend::evaluate_refinement_indicators(
    std::span<const backend::BackendStateAccess> accesses,
    const AmrConfig& config, double density_floor, std::span<const int> species)
{
    if (accesses.empty()) throw std::invalid_argument("empty CUDA refinement topology");
    // The sequential evaluator tolerated repeated accesses; concurrent blocks
    // must not share the same mutable EOS latch. Reuse the backend's complete
    // Current-slot / generation / unique-handle preflight before any upload.
    validate_hydro_batch_accesses(accesses);
    impl_->select_device();
    const auto& first = impl_->require_block(accesses.front());
    const auto selection = amr::indicator::make_selection(config, first.grid.dim, species);
    for (int index : species)
        if (index < 0 || index >= impl_->species_count)
            throw std::invalid_argument("invalid CUDA refinement species");
    std::size_t largest_cells = 0;
    for (const auto& access : accesses) {
        const auto& block = impl_->require_block(access);
        if (access.slot != state::StateSlot::Current || block.grid.dim != first.grid.dim)
            throw std::invalid_argument("invalid CUDA refinement source");
        largest_cells = std::max(largest_cells, static_cast<std::size_t>(block.grid.total_size));
    }
    auto& scratch = impl_->refinement_scratch;
    DeviceIndicatorWorkspace workspace;
    workspace.selection_count = static_cast<int>(selection.size());
    workspace.density_floor = density_floor;
    workspace.pressure = config.refine_on_p || config.refine_on_entropy;
    workspace.temperature = config.refine_on_temp;
    workspace.gamma1 = config.refine_on_entropy;
    double* summary = reserve_indicator_scratch(scratch.summary, accesses.size());
    auto* device_selection = reserve_indicator_scratch(scratch.selection,
        selection.size() * sizeof(amr::indicator::Selection));
    const bool thermo = workspace.pressure || workspace.temperature || workspace.gamma1;
    const auto wave_capacity = indicator_wave_capacity(largest_cells, impl_->species_count,
        thermo, accesses.size());
    const auto scratch_cells = largest_cells * wave_capacity;
    const auto scratch_fields = 1 + (thermo ? 3 + static_cast<std::size_t>(impl_->species_count) : 0);
    // A single high-water arena prevents separately retained thermo/error
    // allocations from exceeding the optional scratch budget after a route
    // change. Compact metadata, output summaries and a required oversized
    // single-block allocation are not claims of a total-VRAM cap.
    auto* arena = reserve_indicator_scratch(scratch.arena, scratch_cells * scratch_fields);
    workspace.cell_errors = arena;
    if (thermo) {
        workspace.thermodynamics = arena + scratch_cells;
        if (impl_->species_count > 0)
            workspace.composition = arena + 4 * scratch_cells;
    }
    workspace.selection = reinterpret_cast<const amr::indicator::Selection*>(device_selection);
    std::vector<DeviceIndicatorBatchBlock> bindings;
    bindings.reserve(accesses.size());
    for (std::size_t index = 0; index < accesses.size(); ++index) {
        auto& block = impl_->require_block(accesses[index]);
        auto block_workspace = workspace;
        const auto offset = (index % wave_capacity) * largest_cells;
        block_workspace.cell_errors += offset;
        if (thermo) {
            block_workspace.thermodynamics += 3 * offset;
            if (impl_->species_count > 0)
                block_workspace.composition += offset * static_cast<std::size_t>(impl_->species_count);
        }
        block_workspace.block_error = summary + index;
        block_workspace.eos_status = block.cfl_status.get();
        bindings.push_back({block.require_access(accesses[index]), block.grid, block_workspace});
    }
    const auto binding_bytes = bindings.size() * sizeof(DeviceIndicatorBatchBlock);
    auto* device_bindings = reinterpret_cast<DeviceIndicatorBatchBlock*>(
        reserve_indicator_scratch(scratch.bindings, binding_bytes));
    std::vector<double> result(accesses.size());
    try {
        if (!selection.empty()) {
            const auto bytes = selection.size() * sizeof(selection.front());
            enqueue_cuda_metadata_upload(device_selection, selection.data(), bytes,
                impl_->stream.get(), impl_->runtime_counters, "upload AMR indicator selection");
        }
        // Each block in a wave has private scratch and an EOS latch. Later
        // waves reuse that arena only after the earlier kernels on this stream.
        // Borrowed Host bindings survive until the final completion boundary.
        enqueue_cuda_metadata_upload(device_bindings, bindings.data(), binding_bytes,
            impl_->stream.get(), impl_->runtime_counters, "upload AMR indicator bindings");
        visit_eos(impl_->eos, [&](const auto& eos) {
            const auto launched = launch_cuda_refinement_indicators_batch(bindings, device_bindings,
                wave_capacity, eos, impl_->stream.get());
            check_cuda(launched.error, "evaluate CUDA AMR indicators");
            impl_->runtime_counters.kernel_count += launched.kernels_launched;
        });
        const auto bytes = result.size() * sizeof(double);
        check_cuda(cudaMemcpyAsync(result.data(), summary, bytes,
            cudaMemcpyDeviceToHost, impl_->stream.get()), "download AMR block indicators");
        impl_->runtime_counters.bytes_d2h += bytes;
        quiesce();
    } catch (...) {
        impl_->quiesce_or_terminate();
        throw;
    }
    for (double value : result)
        if (!std::isfinite(value))
            throw std::runtime_error("Non-finite AMR indicator value encountered.");
    return result;
}

} // namespace arch::cuda
