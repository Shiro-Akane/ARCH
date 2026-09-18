/**
 * @file CudaBackendHydroControl.cpp
 * @brief Coordinate CUDA hydro, boundary exchange, reflux and slot rotation.
 *
 * Validate store generations and shared stage/plan descriptors before binding
 * device views. Runtime-owned scratch and transfer metadata remain alive through
 * stream completion; ghost and slot visibility follow the common driver contract.
 * Flux, CFL, interpolation and geometry formulas stay in their shared owners.
 */

#include "cuda/runtime/control/CudaBackendInternal.h"

#include "amr/CoarseFineCellPlan.h"
#include "amr/LimitedLinearProlongation.h"
#include "cuda/hydro/GridGeometryAdapter.cuh"

#include <cmath>
#include <string>

namespace arch::cuda {
namespace {

bool same_hydro_descriptor(
    const scheduler::StageDescriptor& left,
    const scheduler::StageDescriptor& right) noexcept
{
    return left.stage == right.stage
        && left.old_slot == right.old_slot
        && left.input_slot == right.input_slot
        && left.output_slot == right.output_slot
        && left.old_weight == right.old_weight
        && left.update_weight == right.update_weight
        && left.flux_register_weight == right.flux_register_weight
        && left.input_requires_ghost == right.input_requires_ghost
        && left.refresh_ghost_after == right.refresh_ghost_after;
}

scheduler::HydroMethod hydro_method(
    dispatch::TimeIntegratorId integrator)
{
    using dispatch::TimeIntegratorId;
    switch (integrator) {
    case TimeIntegratorId::Euler: return scheduler::HydroMethod::Euler;
    case TimeIntegratorId::Rk2: return scheduler::HydroMethod::RK2;
    case TimeIntegratorId::Rk3: return scheduler::HydroMethod::RK3;
    }
    throw std::invalid_argument("invalid resolved Hydro integrator");
}

} // namespace

double CudaBackend::compute_hydro_dt(
    backend::BackendStateAccess current, double cfl)
{
    return compute_hydro_dt_batch({&current, 1}, cfl).front();
}

std::vector<double> CudaBackend::compute_hydro_dt_batch(
    std::span<const backend::BackendStateAccess> currents, double cfl)
{
    validate_hydro_batch_accesses(currents);
    std::vector<double> result(currents.size());
    if (currents.empty()) return result;
    std::vector<CudaBlockRuntime*> blocks;
    blocks.reserve(currents.size());
    for (const auto current : currents) {
        auto& block = impl_->require_block(current);
        static_cast<void>(block.require_access(current));
        blocks.push_back(&block);
    }
    impl_->select_device();
    auto& scratch = impl_->hydro_batch;
    scratch.ensure_capacity(currents.size());
    // Result and owner staging outlive the guard, including a failed second
    // download. No Host pointer escapes this synchronous batch operation.
    CudaQuiescenceGuard work_guard{*impl_};
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        auto& block = *blocks[index];
        const CudaHydroWorkspaceView workspace{
            block.face_flux.view(), block.hydro_delta.view(),
            block.cfl_candidates.get(), scratch.dt->get() + index,
            scratch.status->get() + index, impl_->species_workspace};
        cudaError_t launch_error = cudaSuccess;
        visit_eos(impl_->eos, [&](const auto& eos) {
            launch_error = launch_cuda_backend_hydro_dt(
                block.require_access(currents[index]), block.grid, eos,
                cfl, workspace, impl_->stream.get());
        });
        check_cuda(launch_error, "launch Hydro dt batch");
    }
    // Pageable staging is sufficient: transfers occur AFTER every launch,
    // and this API intentionally returns synchronously. Two bulk D2H calls,
    // not two tiny transfers per block; no pinned buffers/events are needed.
    check_cuda(cudaMemcpyAsync(result.data(), scratch.dt->get(),
                   result.size() * sizeof(double), cudaMemcpyDeviceToHost,
                   impl_->stream.get()), "download Hydro dt batch");
    check_cuda(cudaMemcpyAsync(scratch.host_status.data(), scratch.status->get(),
                   currents.size() * sizeof(int), cudaMemcpyDeviceToHost,
                   impl_->stream.get()), "download Hydro CFL batch status");
    quiesce();
    work_guard.completed = true;
    impl_->runtime_counters.kernel_count += 2 * currents.size();
    impl_->runtime_counters.bytes_d2h += currents.size() * (sizeof(double) + sizeof(int));
    for (std::size_t index = 0; index < currents.size(); ++index) {
        if (scratch.host_status[index] != static_cast<int>(reduction::ReductionStatus::Ok))
            throw std::runtime_error("Invalid CUDA hydro EOS query or CFL reduction: block="
                + std::to_string(currents[index].block.uid.value) + " epoch="
                + std::to_string(currents[index].block.epoch.value) + " status="
                + std::to_string(scratch.host_status[index]));
    }
    return result;
}

state::CompletionToken CudaBackend::execute_hydro_stage(
    backend::BackendStateAccess current,
    const scheduler::StageDescriptor& descriptor,
    double dt, state::CompletionToken expected)
{
    return execute_hydro_stage_batch({&current, 1}, descriptor, dt, expected);
}

state::CompletionToken CudaBackend::execute_hydro_stage_batch(
    std::span<const backend::BackendStateAccess> currents,
    const scheduler::StageDescriptor& descriptor,
    double dt, state::CompletionToken expected)
{
    validate_hydro_batch_accesses(currents);
    const auto plan = scheduler::make_hydro_plan(
        hydro_method(impl_->launch.plan.time_integrator));
    if (!complete_token(expected) || descriptor.stage <= 0
        || descriptor.stage > static_cast<int>(plan.stages.size())
        || !same_hydro_descriptor(descriptor, plan.stages[descriptor.stage - 1]))
        throw std::invalid_argument("invalid Hydro stage contract");
    if (currents.empty()) return expected;
    impl_->select_device();
    auto& scratch = impl_->hydro_batch;
    scratch.ensure_capacity(currents.size());
    std::vector<DeviceHydroBatchBlock> blocks;
    blocks.reserve(currents.size());
    for (std::size_t index = 0; index < currents.size(); ++index) {
        const auto current = currents[index];
        auto& block = impl_->require_block(current);
        static_cast<void>(block.require_access(current));
        blocks.push_back({block.slots[slot_index(descriptor.old_slot)],
            block.slots[slot_index(descriptor.input_slot)],
            block.slots[slot_index(descriptor.output_slot)],
            block.hydro_delta.view(), block.face_flux.view(), block.grid,
            scratch.status->get() + index,
            make_cuda_amr_route_views(impl_->active_amr_flux.get(), current.block)});
    }
    auto& device_blocks = impl_->hydro_bindings;
    device_blocks.reserve(blocks.size());
    CudaBackendLaunchResult launch{};
    CudaQuiescenceGuard work_guard{*impl_};
    enqueue_cuda_metadata_upload(device_blocks.get(), blocks.data(),
        blocks.size() * sizeof(DeviceHydroBatchBlock), impl_->stream.get(),
        impl_->runtime_counters, "upload Hydro batch bindings");
    visit_eos(impl_->eos, [&](const auto& eos) {
        launch = launch_cuda_backend_hydro_stage_batch(impl_->launch.plan,
            blocks, device_blocks.get(), eos,
            impl_->launch.entropy_fix_coefficient, impl_->launch.density_floor,
            impl_->launch.minimum_internal_energy, impl_->launch.maximum_internal_energy,
            descriptor, dt, impl_->stream.get(), impl_->species_workspace, impl_->launch.gravity);
    });
    if (!launch.route_found) throw std::logic_error("CUDA Hydro batch route is unavailable");
    check_cuda(launch.error, "launch Hydro stage batch");
    const auto kernels = static_cast<std::uint64_t>(launch.kernels_launched);
    // Distinct latches prevent a valid later block from clearing earlier EOS
    // failures. The ordered stream still protects shared species scratch and
    // registers face flux before its block scratch is overwritten.
    check_cuda(cudaMemcpyAsync(scratch.host_status.data(), scratch.status->get(),
                   currents.size() * sizeof(int), cudaMemcpyDeviceToHost,
                   impl_->stream.get()), "download Hydro stage batch EOS status");
    quiesce();
    work_guard.completed = true;
    impl_->runtime_counters.kernel_count += kernels;
    impl_->runtime_counters.bytes_d2h += currents.size() * sizeof(int);
    for (std::size_t index = 0; index < currents.size(); ++index) {
        if (scratch.host_status[index] != 0)
            throw std::runtime_error("Invalid CUDA hydro stage EOS query: stage="
                + std::to_string(descriptor.stage) + " block="
                + std::to_string(currents[index].block.uid.value) + " epoch="
                + std::to_string(currents[index].block.epoch.value) + " status="
                + std::to_string(scratch.host_status[index]));
    }
    return expected;
}

state::CompletionToken CudaBackend::clear_amr_flux_register(
    state::CompletionToken expected)
{
    if (!complete_token(expected))
        throw std::invalid_argument(
            "invalid CUDA AMR flux-clear completion contract");
    CudaAmrFluxPlanRuntime* const plan = impl_->active_amr_flux.get();
    if (plan == nullptr)
        throw std::logic_error("CUDA AMR flux plan is unavailable");

    int kernels = 0;
    for (const auto& requirement : plan->surface_requirements) {
        const auto register_role = static_cast<std::uint8_t>(
            amr::AmrFluxSurfaceRole::Register);
        if ((requirement.roles & register_role) == 0) continue;
        if (requirement.block < 0
            || requirement.block >= static_cast<int>(plan->host_blocks.size())
            || requirement.face < 0 || requirement.face >= 6)
            throw std::logic_error(
                "CUDA AMR flux-clear surface is outside the active plan");
        const auto surface = plan->host_blocks[
            static_cast<std::size_t>(requirement.block)]
                                 .registers[requirement.face];
        check_cuda(launch_cuda_amr_flux_surface_clear(
                       surface, impl_->stream.get()),
                   "clear CUDA AMR flux surface");
        ++kernels;
    }
    if (kernels > 0) quiesce();
    impl_->runtime_counters.kernel_count +=
        static_cast<std::uint64_t>(kernels);
    return expected;
}

state::CompletionToken CudaBackend::execute_amr_reflux(
    state::StateSlot slot, double dt, state::CompletionToken expected)
{
    if (!complete_token(expected) || !std::isfinite(dt) || dt < 0.0)
        throw std::invalid_argument("invalid CUDA AMR reflux contract");
    CudaAmrFluxPlanRuntime* const plan = impl_->active_amr_flux.get();
    if (plan == nullptr)
        throw std::logic_error("CUDA AMR flux plan is unavailable");
    if (plan->runtime_blocks.size() != plan->host_blocks.size()
        || plan->device_blocks.size() != plan->host_blocks.size())
        throw std::logic_error("CUDA AMR flux block bindings drifted");

    // Slot rotation changes the DeviceStateView pointers.  Rebind immediately
    // before reflux so Hydro targets the rotated Current buffer and RKL targets
    // the descriptor's not-yet-published output slot.
    for (std::size_t index = 0; index < plan->host_blocks.size(); ++index) {
        CudaBlockRuntime* const block = plan->runtime_blocks[index];
        if (block == nullptr)
            throw std::logic_error("CUDA AMR flux block was retired early");
        plan->host_blocks[index].state = block->slots[slot_index(slot)];
    }

    const auto& reflux = plan->compiled_reflux;
    if (reflux.targets.empty()) return expected;
    if (reflux.contributions.empty())
        throw std::logic_error("CUDA AMR reflux plan has no contributions");
    const std::size_t descriptor_bytes = plan->host_blocks.size()
        * sizeof(DeviceAmrFluxBlockView);
    enqueue_cuda_metadata_upload(
                   plan->device_blocks.get(), plan->host_blocks.data(),
                   descriptor_bytes, impl_->stream.get(), impl_->runtime_counters,
               "rebind CUDA AMR reflux state slots");
    check_cuda(launch_cuda_amr_reflux(
                   plan->device_blocks.get(),
                   static_cast<int>(plan->host_blocks.size()),
                   plan->reflux_targets.get(),
                   static_cast<int>(reflux.targets.size()),
                   plan->reflux_contributions.get(),
                   static_cast<int>(reflux.contributions.size()),
                   dt, impl_->stream.get()),
               "launch CUDA AMR reflux");
    quiesce();
    if (dt > 0.0) ++impl_->runtime_counters.kernel_count;
    return expected;
}

state::CompletionToken CudaBackend::execute_physical_boundary(
    backend::BackendStateAccess access, state::StateVersion version,
    state::CompletionToken expected)
{
    auto& block = impl_->require_block(access);
    const DeviceStateView selected = block.require_access(access);
    if (!state::is_valid(version) || !complete_token(expected))
        throw std::invalid_argument("invalid boundary completion contract");
    check_cuda(launch_cuda_backend_boundary_plan(
                   selected, block.boundary_transfers.get(),
                   block.boundary, impl_->stream.get()),
               "launch boundary plan");
    quiesce();
    for (const auto& phase : block.boundary.phases)
        if (phase.count > 0) ++impl_->runtime_counters.kernel_count;
    return expected;
}

state::CompletionToken CudaBackend::execute_physical_boundary_batch(
    std::span<const backend::BackendStateAccess> accesses,
    state::StateVersion version, state::CompletionToken expected)
{
    validate_hydro_batch_accesses(accesses, accesses.empty()
        ? state::StateSlot::Current : accesses.front().slot);
    if (!state::is_valid(version) || !complete_token(expected))
        throw std::invalid_argument("invalid boundary batch completion contract");
    if (accesses.empty()) return expected;
    if (accesses.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::overflow_error("boundary batch exceeds launch index extent");
    std::vector<DeviceBoundaryBatchBlock> blocks;
    blocks.reserve(accesses.size());
    std::array<int, 3> phase_counts{};
    for (const auto access : accesses) {
        const auto& block = impl_->require_block(access);
        const auto selected = block.require_access(access);
        check_cuda(validate_boundary_launch(selected, block.boundary_transfers.get(),
            block.boundary), "validate boundary batch block");
        blocks.push_back({selected, block.boundary_transfers.get(), block.boundary.phases});
        for (std::size_t phase = 0; phase < phase_counts.size(); ++phase)
            phase_counts[phase] = std::max(phase_counts[phase],
                static_cast<int>(block.boundary.phases[phase].count));
    }
    impl_->select_device();
    auto& device_blocks = impl_->boundary_bindings;
    device_blocks.reserve(blocks.size());
    int kernels = 0;
    CudaQuiescenceGuard work_guard{*impl_};
    enqueue_cuda_metadata_upload(device_blocks.get(), blocks.data(),
        blocks.size() * sizeof(DeviceBoundaryBatchBlock), impl_->stream.get(),
        impl_->runtime_counters, "upload boundary batch bindings");
    check_cuda(launch_cuda_backend_boundary_batch(device_blocks.get(),
        static_cast<int>(blocks.size()), phase_counts, impl_->stream.get(), kernels),
        "launch boundary batch");
    quiesce();
    work_guard.completed = true;
    impl_->runtime_counters.kernel_count += static_cast<std::uint64_t>(kernels);
    return expected;
}

state::CompletionToken CudaBackend::execute_same_level_exchange(
    std::span<const backend::BackendStateAccess> accesses,
    const amr::SameLevelExchangePlan& plan, state::StateSlot slot,
    state::StateVersion source_version,
    state::CompletionToken expected)
{
    if (!state::is_valid(source_version) || !complete_token(expected))
        throw std::invalid_argument("invalid CUDA same_level exchange completion");
    impl_->execute_same_level_exchange(accesses, plan, slot,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            return impl_->require_block(access);
        });
    return expected;
}

void CudaBackend::Impl::execute_same_level_exchange(
    std::span<const backend::BackendStateAccess> accesses,
    const amr::SameLevelExchangePlan& plan, state::StateSlot slot,
    const BlockResolver& resolve)
{
    if (!amr::is_valid(plan.epoch) || plan.fingerprint == 0
        || amr::compute_same_level_exchange_fingerprint(plan)
            != plan.fingerprint
        || accesses.empty() || accesses.size() != plan.blocks.size())
        throw std::invalid_argument("invalid CUDA same-level exchange contract");

    std::map<amr::BlockHandle, int> indices;
    std::vector<DeviceExchangeBlock> host_blocks;
    host_blocks.reserve(accesses.size());
    int species_count = -1;
    for (const auto& access : accesses) {
        if (access.slot != slot
            || access.block.epoch.value != plan.epoch.value)
            throw std::invalid_argument("stale CUDA exchange access");
        auto& block = resolve(access);
        if (!indices.emplace(access.block, static_cast<int>(host_blocks.size())).second)
            throw std::invalid_argument("duplicate CUDA exchange access");
        const DeviceStateView selected = block.require_access(access);
        if (species_count >= 0 && selected.n_species != species_count)
            throw std::invalid_argument("CUDA exchange species counts differ");
        species_count = selected.n_species;
        host_blocks.push_back({selected, block.grid});
    }
    for (const auto& endpoint : plan.blocks) {
        if (!amr::is_valid(endpoint.handle)
            || endpoint.handle.epoch.value != plan.epoch.value
            || indices.find(endpoint.handle) == indices.end())
            throw std::invalid_argument("CUDA exchange endpoint is missing");
    }

    struct PreparedExchangePhase {
        std::vector<DeviceExchangeOperation> operations;
        std::uint64_t cells = 0;
    };
    std::array<PreparedExchangePhase, 3> prepared_phases{};
    const std::uint64_t field_count =
        static_cast<std::uint64_t>(6 + species_count);
    if (field_count < 6
        || field_count > static_cast<std::uint64_t>(
            std::numeric_limits<int>::max()))
        throw std::overflow_error("CUDA exchange field count overflow");
    std::size_t expected_first = 0;
    for (std::size_t phase_index = 0; phase_index < plan.phases.size(); ++phase_index) {
        const auto phase = plan.phases[phase_index];
        if (phase.id != static_cast<amr::ExchangePhaseId>(phase_index)
            || phase.first != expected_first
            || phase.first > plan.operations.size()
            || phase.count > plan.operations.size() - phase.first
            || phase.count > static_cast<std::size_t>(
                std::numeric_limits<int>::max()))
            throw std::invalid_argument("invalid CUDA exchange phase metadata");
        expected_first += phase.count;
        if (phase.count == 0) continue;

        auto& prepared = prepared_phases[phase_index];
        prepared.operations.reserve(phase.count);
        for (std::size_t offset = 0; offset < phase.count; ++offset) {
            const auto& operation = plan.operations[phase.first + offset];
            if (operation.ordinal != phase.first + offset
                || operation.phase != phase.id
                || operation.source_box.extent
                    != operation.destination_box.extent)
                throw std::invalid_argument("invalid CUDA exchange operation");
            const auto source = indices.find(operation.source.handle);
            const auto destination = indices.find(operation.destination.handle);
            if (source == indices.end() || destination == indices.end())
                throw std::invalid_argument("stale CUDA exchange operation");
            const auto validate_box = [&](const amr::LogicalExchangeBox& box,
                                          const DeviceGridView& grid) {
                for (int axis = 0; axis < 3; ++axis) {
                    const std::int64_t origin = axis == 0 ? grid.is
                        : (axis == 1 ? grid.js : grid.ks);
                    const std::int64_t total = axis == 0 ? grid.total_x
                        : (axis == 1 ? grid.total_y : grid.total_z);
                    const std::int64_t first = origin + box.first[axis];
                    const std::int64_t end = first + box.extent[axis];
                    if (box.extent[axis] == 0 || first < 0 || end > total)
                        throw std::out_of_range(
                            "CUDA exchange box is outside block layout");
                }
            };
            validate_box(operation.source_box, host_blocks[source->second].grid);
            validate_box(
                operation.destination_box,
                host_blocks[destination->second].grid);
            const std::uint64_t cells =
                amr::exchange_detail::checked_product(
                    operation.source_box.extent);
            if (prepared.cells
                > std::numeric_limits<std::uint64_t>::max() - cells)
                throw std::overflow_error("CUDA exchange phase size overflow");
            DeviceExchangeOperation compiled{};
            compiled.source_block = source->second;
            compiled.destination_block = destination->second;
            compiled.scratch_first = prepared.cells;
            for (int axis = 0; axis < 3; ++axis) {
                compiled.source_first[axis] = operation.source_box.first[axis];
                compiled.destination_first[axis] =
                    operation.destination_box.first[axis];
                compiled.extent[axis] = operation.source_box.extent[axis];
            }
            prepared.operations.push_back(compiled);
            prepared.cells += cells;
        }
        if (prepared.cells
            > std::numeric_limits<std::size_t>::max() / field_count)
            throw std::overflow_error("CUDA exchange scratch size overflow");
    }
    if (expected_first != plan.operations.size())
        throw std::invalid_argument("CUDA exchange phases do not cover plan");
    if (plan.operations.empty()) return;

    select_device();
    auto& device_blocks = exchange_scratch.blocks;
    device_blocks.reserve(host_blocks.size());
    CudaQuiescenceGuard work_guard{*this};
    enqueue_cuda_metadata_upload(
                   device_blocks.get(), host_blocks.data(),
                   host_blocks.size() * sizeof(DeviceExchangeBlock),
                   stream.get(), runtime_counters,
               "upload CUDA exchange blocks");
    for (std::size_t phase = 0; phase < prepared_phases.size(); ++phase) {
        auto& prepared = prepared_phases[phase];
        auto& buffers = exchange_scratch.phases[phase];
        if (prepared.operations.empty()) continue;
        buffers.operations.reserve(prepared.operations.size());
        buffers.values.reserve(static_cast<std::size_t>(
            prepared.cells * field_count));
        enqueue_cuda_metadata_upload(
                       buffers.operations.get(),
                       prepared.operations.data(),
                       prepared.operations.size()
                           * sizeof(DeviceExchangeOperation),
                       stream.get(), runtime_counters,
                   "upload CUDA exchange operations");
    }

    std::uint64_t kernels = 0;
    for (std::size_t phase = 0; phase < prepared_phases.size(); ++phase) {
        const auto& prepared = prepared_phases[phase];
        auto& buffers = exchange_scratch.phases[phase];
        if (prepared.operations.empty()) continue;
        check_cuda(launch_cuda_backend_exchange_phase(
                       device_blocks.get(), buffers.operations.get(),
                       static_cast<int>(prepared.operations.size()),
                       static_cast<int>(field_count), prepared.cells,
                       buffers.values.get(), stream.get()),
                   "launch CUDA same-level exchange phase");
        kernels += 2;
    }
    // Gather/scatter and subsequent phases are ordered on the same stream.
    checked_quiesce("synchronize CUDA same_level exchange");
    runtime_counters.kernel_count += kernels;
    work_guard.completed = true;
    return;
}

state::CompletionToken CudaBackend::execute_coarse_fine_exchange(
    std::span<const backend::BackendStateAccess> accesses,
    const amr::CoarseFineTransferPlan& plan, state::StateSlot slot,
    state::StateVersion source_version,
    state::CompletionToken expected)
{
    if (!state::is_valid(source_version) || !complete_token(expected))
        throw std::invalid_argument("invalid CUDA coarse_fine exchange completion");
    impl_->execute_coarse_fine_exchange(accesses, plan, slot,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            return impl_->require_block(access);
        });
    return expected;
}

void CudaBackend::Impl::execute_coarse_fine_exchange(
    std::span<const backend::BackendStateAccess> accesses,
    const amr::CoarseFineTransferPlan& plan, state::StateSlot slot,
    const BlockResolver& resolve)
{
    if (accesses.empty())
        throw std::invalid_argument(
            "invalid CUDA coarse-fine exchange contract");
    amr::validate_amr_plan(plan);

    std::map<amr::BlockHandle, int> indices;
    std::vector<DeviceExchangeBlock> host_blocks;
    host_blocks.reserve(accesses.size());
    int species_count = -1;
    for (const backend::BackendStateAccess& access : accesses) {
        if (access.slot != slot
            || access.block.epoch != plan.scope.from_epoch)
            throw std::invalid_argument(
                "stale CUDA coarse-fine exchange access");
        CudaBlockRuntime& block = resolve(access);
        if (!indices.emplace(
                access.block, static_cast<int>(host_blocks.size())).second)
            throw std::invalid_argument(
                "duplicate CUDA coarse-fine exchange access");
        const DeviceStateView selected = block.require_access(access);
        if (block.grid.dim != plan.dimension)
            throw std::invalid_argument(
                "CUDA coarse-fine exchange dimension mismatch");
        if (species_count >= 0 && selected.n_species != species_count)
            throw std::invalid_argument(
                "CUDA coarse-fine exchange species counts differ");
        species_count = selected.n_species;
        host_blocks.push_back({selected, block.grid});
    }
    if (species_count < 0)
        throw std::invalid_argument(
            "CUDA coarse-fine exchange requires blocks");

    const amr::CoarseFineCellPlan cell_plan =
        amr::compile_coarse_fine_cell_plan(plan, species_count);
    if (cell_plan.transfers.size()
        > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::overflow_error(
            "CUDA coarse-fine transfer count overflow");
    const std::uint64_t field_count = static_cast<std::uint64_t>(
        6 + species_count);
    if (field_count < 6
        || field_count > static_cast<std::uint64_t>(
            std::numeric_limits<int>::max()))
        throw std::overflow_error(
            "CUDA coarse-fine field count overflow");

    const auto cell_index = [](const DeviceGridView& grid,
                               const amr::LogicalAmrCell& cell) {
        const std::array<std::int64_t, 3> absolute{
            static_cast<std::int64_t>(grid.is) + cell[0],
            static_cast<std::int64_t>(grid.js) + cell[1],
            static_cast<std::int64_t>(grid.ks) + cell[2]};
        if (absolute[0] < 0 || absolute[0] >= grid.total_x
            || absolute[1] < 0 || absolute[1] >= grid.total_y
            || absolute[2] < 0 || absolute[2] >= grid.total_z)
            throw std::out_of_range(
                "coarse-fine cell is outside CUDA block layout");
        return grid.index(
            static_cast<int>(absolute[0]),
            static_cast<int>(absolute[1]),
            static_cast<int>(absolute[2]));
    };

    std::vector<DeviceCoarseFineTransfer> host_transfers;
    host_transfers.reserve(cell_plan.transfers.size());
    for (const amr::CoarseFineCellTransfer& transfer
         : cell_plan.transfers) {
        const auto source = indices.find(transfer.source.handle);
        const auto destination = indices.find(transfer.destination.handle);
        if (source == indices.end() || destination == indices.end())
            throw std::invalid_argument(
                "CUDA coarse-fine endpoint is missing");
        DeviceCoarseFineTransfer lowered{};
        lowered.source_block = source->second;
        lowered.destination_block = destination->second;
        lowered.destination_cell = cell_index(
            host_blocks[destination->second].grid,
            transfer.destination_cell);
        lowered.source_count = transfer.source_count;
        if (transfer.rule == amr::RefinementRule::CoarseGhostInjection) {
            lowered.prolongation_dimension = static_cast<std::uint8_t>(
                cell_plan.dimension);
            for (int axis = 0; axis < 3; ++axis)
                lowered.fine_position[axis] = transfer.fine_position[axis];
        }
        if (lowered.source_count == 0
            || lowered.source_count > std::size(lowered.source_cells))
            throw std::invalid_argument(
                "CUDA coarse-fine source count is invalid");
        const auto& source_grid = host_blocks[source->second].grid;
        const auto source_geometry = make_grid_geometry_view(source_grid);
        for (std::size_t cell = 0; cell < lowered.source_count; ++cell) {
            lowered.source_cells[cell] = cell_index(
                source_grid, transfer.source_cells[cell]);
            // Match Host lowering: equal Cartesian volumes cancel; curved
            // cells use the same shared physical-volume authority. These are
            // immutable geometry inputs, not device state downloaded to Host.
            const auto& logical = transfer.source_cells[cell];
            const double measure = source_geometry.geometry == GridMetrics::Geometry::Cartesian
                ? 1.0 : GridMetrics::CellVolume(source_geometry,
                    source_grid.is + logical[0], source_grid.js + logical[1],
                    source_grid.ks + logical[2]);
            if (!std::isfinite(measure) || measure <= 0.0)
                throw std::invalid_argument("CUDA coarse-fine source measure is invalid");
            lowered.source_measures[cell] = measure;
            lowered.source_measure_sum += measure;
        }
        if (!std::isfinite(lowered.source_measure_sum) || lowered.source_measure_sum <= 0.0)
            throw std::invalid_argument("CUDA coarse-fine source measure sum is invalid");
        if (lowered.prolongation_dimension != 0) {
            for (std::size_t cell = 0; cell < std::size(lowered.slope_cells);
                 ++cell)
                lowered.slope_cells[cell] = cell_index(
                    host_blocks[source->second].grid,
                    transfer.slope_cells[cell]);
        }
        host_transfers.push_back(lowered);
    }
    if (host_transfers.empty()) return;

    select_device();
    auto& device_blocks = exchange_scratch.blocks;
    auto& device_transfers = exchange_scratch.transfers;
    auto& scratch = exchange_scratch.coarse_values;
    auto& exchange_status = exchange_scratch.status;
    device_blocks.reserve(host_blocks.size());
    device_transfers.reserve(host_transfers.size());
    exchange_status.reserve(1);
    if (host_transfers.size()
        > std::numeric_limits<std::size_t>::max() / field_count)
        throw std::overflow_error(
            "CUDA coarse-fine scratch size overflow");
    scratch.reserve(static_cast<std::size_t>(
        host_transfers.size() * field_count));
    int status = 0;
    CudaQuiescenceGuard work_guard{*this};
    enqueue_cuda_metadata_upload(
                   device_blocks.get(), host_blocks.data(),
                   host_blocks.size() * sizeof(DeviceExchangeBlock),
                   stream.get(), runtime_counters,
               "upload CUDA coarse-fine blocks");
    enqueue_cuda_metadata_upload(
                   device_transfers.get(), host_transfers.data(),
                   host_transfers.size() * sizeof(DeviceCoarseFineTransfer),
                   stream.get(), runtime_counters,
               "upload CUDA coarse-fine transfers");
    check_cuda(launch_cuda_backend_coarse_fine_exchange(
                   device_blocks.get(), device_transfers.get(),
                   static_cast<int>(host_transfers.size()),
                   static_cast<int>(field_count), scratch.get(),
                   exchange_status.get(), stream.get()),
               "launch CUDA coarse-fine exchange");
    check_cuda(cudaMemcpyAsync(
                   &status, exchange_status.get(), sizeof(int),
                   cudaMemcpyDeviceToHost, stream.get()),
               "download CUDA coarse-fine exchange status");
    checked_quiesce("synchronize CUDA coarse_fine exchange");
    work_guard.completed = true;
    runtime_counters.kernel_count += 2;
    runtime_counters.bytes_d2h += sizeof(int);
    if (status != 0)
        throw std::runtime_error(
            amr::prolongation_math::invalid_prolongation_density_message());
    return;
}

void CudaBackend::rotate_slots(
    backend::BackendStateAccess current, state::SlotRotation rotation)
{
    auto& block = impl_->require_block(current);
    static_cast<void>(block.require_access(current));
    if (current.slot != state::StateSlot::Current)
        throw std::invalid_argument("rotation requires Current access");
    const std::array<std::size_t, 3> source{
        slot_index(rotation.current_from), slot_index(rotation.next_from),
        slot_index(rotation.scratch_from)};
    if (source[0] == source[1] || source[0] == source[2]
        || source[1] == source[2])
        throw std::invalid_argument("slot rotation is not a permutation");
    const auto old = block.slots;
    for (std::size_t destination = 0; destination < 3; ++destination)
        block.slots[destination] = old[source[destination]];
}


} // namespace arch::cuda
