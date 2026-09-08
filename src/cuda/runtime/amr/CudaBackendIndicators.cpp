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
    workspace.cell_errors = reserve_indicator_scratch(scratch.errors, largest_cells);
    double* summary = reserve_indicator_scratch(scratch.summary, accesses.size());
    auto* device_selection = reserve_indicator_scratch(scratch.selection,
        selection.size() * sizeof(amr::indicator::Selection));
    const bool thermo = workspace.pressure || workspace.temperature || workspace.gamma1;
    if (thermo) {
        workspace.thermodynamics = reserve_indicator_scratch(scratch.thermodynamics, 3 * largest_cells);
        if (impl_->species_count > 0)
            workspace.composition = reserve_indicator_scratch(scratch.composition,
                largest_cells * static_cast<std::size_t>(impl_->species_count));
    }
    workspace.selection = reinterpret_cast<const amr::indicator::Selection*>(device_selection);
    std::vector<double> result(accesses.size());
    try {
        if (!selection.empty()) {
            const auto bytes = selection.size() * sizeof(selection.front());
            enqueue_cuda_metadata_upload(device_selection, selection.data(), bytes,
                impl_->stream.get(), impl_->runtime_counters, "upload AMR indicator selection");
        }
        // All blocks share a stream-ordered scratch arena. Only one scalar per
        // block crosses to the Host, after the complete batch has been queued.
        for (std::size_t index = 0; index < accesses.size(); ++index) {
            auto& block = impl_->require_block(accesses[index]);
            workspace.block_error = summary + index;
            workspace.eos_status = block.cfl_status.get();
            visit_eos(impl_->eos, [&](const auto& eos) {
                check_cuda(launch_cuda_refinement_indicators(block.require_access(accesses[index]),
                    block.grid, eos, workspace, impl_->stream.get()), "evaluate CUDA AMR indicators");
            });
            impl_->runtime_counters.kernel_count += thermo ? 3 : 2;
        }
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
