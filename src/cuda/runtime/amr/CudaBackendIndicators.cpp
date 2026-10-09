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
#include "cuda/amr/RegridMigration.h"
#include "cuda/common/GridMetricsCache.h"
#include "amr/storage/Block.h"
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

/** Read current device state into compact, ordered JENS minima without Host EOS.
 * Validate the entire access/layout batch before enqueueing; the Driver owns
 * StateVersion/publication readiness. Sequential kernels reuse one arena only
 * on the same stream, and one final copy/fence precedes scratch reuse or return.
 */
std::vector<double> CudaBackend::evaluate_jeans_resolution(
    std::span<const backend::BackendStateAccess> accesses)
{
    if (accesses.empty()) return {};
    validate_hydro_batch_accesses(accesses);
    std::vector<CudaBlockRuntime*> blocks;
    std::vector<DeviceStateView> views;
    std::size_t largest_cells = 0;
    for (const auto& access : accesses) {
        auto& block = impl_->require_block(access);
        const auto view = block.require_access(access);
        if (!valid_hydro_view(view) || !valid_hydro_grid(block.grid)
            || view.total_size != block.grid.total_size
            || block.grid.active_cell_count() <= 0
            || block.grid.geometry != static_cast<int>(DeviceGeometry::Cartesian)
            || view.n_species != impl_->species_count)
            throw std::invalid_argument("invalid CUDA JENS accepted-state layout");
        largest_cells = std::max(largest_cells, static_cast<std::size_t>(block.grid.total_size));
        blocks.push_back(&block);
        views.push_back(view);
    }
    visit_eos(impl_->eos, [&](const auto& eos) {
        if constexpr (requires { eos.species.size(); }) {
            if (eos.species.size() > 0 && eos.species.size() != impl_->species_count)
                throw std::invalid_argument("CUDA JENS EOS/species extent mismatch");
        }
    });
    const auto fields = 1 + static_cast<std::size_t>(impl_->species_count);
    if (largest_cells > std::numeric_limits<std::size_t>::max() / fields / sizeof(double))
        throw std::overflow_error("CUDA JENS scratch extent overflow");
    impl_->select_device();
    auto& scratch = impl_->refinement_scratch;
    double* arena = reserve_indicator_scratch(scratch.arena, largest_cells * fields);
    double* summaries = reserve_indicator_scratch(scratch.summary, accesses.size());
    std::vector<double> result(accesses.size());
    try {
        for (std::size_t index = 0; index < blocks.size(); ++index) {
            auto& block = *blocks[index];
            DeviceJeansWorkspace workspace{arena, summaries + index,
                impl_->species_count ? arena + largest_cells : nullptr, block.cfl_status.get()};
            visit_eos(impl_->eos, [&](const auto& eos) {
                check_cuda(launch_cuda_jeans_resolution(views[index], block.grid, eos,
                    workspace, impl_->stream.get()), "evaluate CUDA JENS accepted-state minimum");
                impl_->runtime_counters.kernel_count += 2;
            });
        }
        const auto bytes = result.size() * sizeof(double);
        check_cuda(cudaMemcpyAsync(result.data(), summaries, bytes, cudaMemcpyDeviceToHost,
            impl_->stream.get()), "download CUDA JENS block minima");
        impl_->runtime_counters.bytes_d2h += bytes;
        quiesce();
    } catch (...) {
        impl_->quiesce_or_terminate();
        throw;
    }
    for (double value : result)
        if (!std::isfinite(value) || value <= 0.0)
            throw std::runtime_error("CUDA JENS rejected an invalid accepted cell or EOS result");
    return result;
}

/** Restrict accepted children privately; never publish or modify active storage.
 * The restriction status is completed before authoritative parent EOS is called.
 * CoarseFluid is a coarsening veto; all other transfer/EOS failures propagate.
 */
std::optional<double> CudaBackend::evaluate_jeans_parent(
    std::span<const backend::BackendStateAccess> accesses, const amr::Block& geometry)
{
    auto grid=make_device_grid_view(geometry.grid);
    if (!valid_hydro_grid(grid) || grid.geometry!=static_cast<int>(DeviceGeometry::Cartesian)
        || accesses.size()!=static_cast<std::size_t>(1<<grid.dim))
        throw std::invalid_argument("invalid CUDA JENS candidate-parent geometry");
    validate_hydro_batch_accesses(accesses);
    DeviceRegridChildren children{};
    for (std::size_t child=0;child<accesses.size();++child) {
        auto& block=impl_->require_block(accesses[child]);
        children.blocks[child]={block.require_access(accesses[child]),block.grid};
        if(block.grid.geometry!=static_cast<int>(DeviceGeometry::Cartesian)
            || block.grid.dim!=grid.dim || block.grid.dx1*2!=grid.dx1
            || (grid.dim>=2 && block.grid.dx2*2!=grid.dx2)
            || (grid.dim==3 && block.grid.dx3*2!=grid.dx3))
            throw std::invalid_argument("CUDA JENS candidate-parent child spacing mismatch");
    }
    visit_eos(impl_->eos,[&](const auto& eos) {
        if constexpr(requires {eos.species.size();})
            if(eos.species.size()>0 && eos.species.size()!=impl_->species_count)
                throw std::invalid_argument("CUDA JENS candidate-parent EOS/species mismatch");
    });
    const auto total=static_cast<std::size_t>(grid.total_size);
    const auto count=static_cast<std::size_t>(impl_->species_count);
    if(total>std::numeric_limits<std::size_t>::max()/7/sizeof(double)
        || (count && total>std::numeric_limits<std::size_t>::max()/(count+1)/sizeof(double)))
        throw std::overflow_error("CUDA JENS candidate-parent workspace overflow");
    impl_->select_device();
    DeviceStateStorage parent;
    parent.allocate(grid.total_size,impl_->species_count);
    DeviceAllocation<double> metrics,restriction_workspace;
    DeviceAllocation<int> transfer_status,eos_status;
    metrics.allocate(total*7);
    if(count) restriction_workspace.allocate(static_cast<std::size_t>(grid.active_cell_count())*count);
    transfer_status.allocate(1);eos_status.allocate(1);
    GridMetricsCacheView cache{};
    cache.capacity=total;cache.cell_volume=metrics.get();grid.cell_volume=metrics.get();
    for(int axis=0;axis<3;++axis) {
        cache.face_area_lower[axis]=metrics.get()+total*(1+2*axis);
        cache.face_area_upper[axis]=metrics.get()+total*(2+2*axis);
        grid.face_area_lower[axis]=cache.face_area_lower[axis];
        grid.face_area_upper[axis]=cache.face_area_upper[axis];
    }
    const DeviceRegridBlock destination{parent.view(),grid};
    check_cuda(validate_cuda_regrid_restriction(children,destination,
        {impl_->launch.density_floor,impl_->launch.minimum_internal_energy,
         impl_->launch.maximum_internal_energy},
        restriction_workspace.get(),restriction_workspace.size(),transfer_status.get()),
        "validate CUDA JENS parent restriction");
    auto& scratch=impl_->refinement_scratch;
    double* arena=reserve_indicator_scratch(scratch.arena,total*(1+count));
    double* minimum=reserve_indicator_scratch(scratch.summary,1);
    int status=0;double result=0.;
    try {
        check_cuda(launch_cuda_grid_metrics_cache(grid,cache,impl_->stream.get()),
            "initialize CUDA JENS parent metrics");
        ++impl_->runtime_counters.kernel_count;
        check_cuda(cudaMemsetAsync(transfer_status.get(),0,sizeof(int),impl_->stream.get()),
            "initialize CUDA JENS parent restriction status");
        check_cuda(launch_cuda_regrid_restriction(children,destination,
            {impl_->launch.density_floor,impl_->launch.minimum_internal_energy,
             impl_->launch.maximum_internal_energy},
            restriction_workspace.get(),restriction_workspace.size(),transfer_status.get(),impl_->stream.get()),
            "restrict CUDA JENS candidate parent");
        ++impl_->runtime_counters.kernel_count;
        check_cuda(cudaMemcpyAsync(&status,transfer_status.get(),sizeof(int),cudaMemcpyDeviceToHost,
            impl_->stream.get()),"download CUDA JENS parent restriction status");
        impl_->runtime_counters.bytes_d2h+=sizeof(int);
        quiesce();
        if(status==static_cast<int>(amr::regrid_math::Status::CoarseFluid)) return std::nullopt;
        if(status!=0) throw std::runtime_error(amr::regrid_math::status_message(
            static_cast<amr::regrid_math::Status>(status)));
        const DeviceJeansWorkspace workspace{arena,minimum,count?arena+total:nullptr,eos_status.get()};
        visit_eos(impl_->eos,[&](const auto& eos) {
            check_cuda(launch_cuda_jeans_resolution(destination.state,grid,eos,workspace,impl_->stream.get()),
                "evaluate authoritative CUDA JENS candidate-parent EOS");
            impl_->runtime_counters.kernel_count+=2;
        });
        check_cuda(cudaMemcpyAsync(&result,minimum,sizeof(double),cudaMemcpyDeviceToHost,
            impl_->stream.get()),"download CUDA JENS parent minimum");
        impl_->runtime_counters.bytes_d2h+=sizeof(double);
        quiesce();
    } catch(...) {impl_->quiesce_or_terminate();throw;}
    if(!std::isfinite(result) || result<=0.)
        throw std::runtime_error("CUDA JENS candidate-parent EOS/result is invalid");
    return result;
}

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
