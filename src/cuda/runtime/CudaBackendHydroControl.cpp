#include "CudaBackendInternal.h"

#include "amr/CoarseFineCellPlan.h"

#include <cmath>

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
    auto& block = impl_->require_block(current);
    const DeviceStateView state = block.require_access(current);
    if (current.slot != state::StateSlot::Current)
        throw std::invalid_argument("Hydro dt requires Current");
    const CudaHydroWorkspaceView workspace{
        block.face_flux.view(), block.hydro_delta.view(),
        block.cfl_candidates.get(), block.cfl_result.get(),
        block.cfl_status.get()};
    cudaError_t launch_error = cudaSuccess;
    visit_eos(impl_->eos, [&](const auto& eos) {
        launch_error = launch_cuda_backend_hydro_dt(
            state, block.grid, eos, cfl, workspace, impl_->stream.get());
    });
    check_cuda(launch_error, "launch Hydro dt");
    double result = 0.0;
    int status = static_cast<int>(reduction::ReductionStatus::Empty);
    check_cuda(cudaMemcpyAsync(
                   &result, block.cfl_result.get(), sizeof(double),
                   cudaMemcpyDeviceToHost, impl_->stream.get()),
               "download Hydro dt");
    check_cuda(cudaMemcpyAsync(
                   &status, block.cfl_status.get(), sizeof(int),
                   cudaMemcpyDeviceToHost, impl_->stream.get()),
               "download Hydro CFL reduction status");
    quiesce();
    impl_->runtime_counters.kernel_count += 2;
    impl_->runtime_counters.bytes_d2h += sizeof(double) + sizeof(int);
    if (status != static_cast<int>(reduction::ReductionStatus::Ok))
        throw std::runtime_error("Invalid CUDA hydro CFL reduction");
    return result;
}

state::CompletionToken CudaBackend::execute_hydro_stage(
    backend::BackendStateAccess current,
    const scheduler::StageDescriptor& descriptor,
    double dt, state::CompletionToken expected)
{
    auto& block = impl_->require_block(current);
    static_cast<void>(block.require_access(current));
    const scheduler::HydroPlan plan = scheduler::make_hydro_plan(
        hydro_method(impl_->launch.plan.time_integrator));
    if (current.slot != state::StateSlot::Current || !complete_token(expected)
        || descriptor.stage <= 0
        || descriptor.stage > static_cast<int>(plan.stages.size())
        || !same_hydro_descriptor(
            descriptor, plan.stages[descriptor.stage - 1]))
        throw std::invalid_argument("invalid Hydro stage contract");
    const DeviceStateView old_state = block.slots[slot_index(descriptor.old_slot)];
    const DeviceStateView input = block.slots[slot_index(descriptor.input_slot)];
    const DeviceStateView output = block.slots[slot_index(descriptor.output_slot)];
    const auto amr_routes = make_cuda_amr_route_views(
        impl_->active_amr_flux.get(), current.block);
    CudaBackendLaunchResult launch{};
    visit_eos(impl_->eos, [&](const auto& eos) {
        launch = launch_cuda_backend_hydro_stage(
            impl_->launch.plan, old_state, input, output,
            block.hydro_delta.view(), block.face_flux.view(), block.grid, eos,
            impl_->launch.entropy_fix_coefficient,
            impl_->launch.density_floor,
            impl_->launch.minimum_internal_energy,
            impl_->launch.maximum_internal_energy,
            amr_routes.data(),
            descriptor, dt, impl_->stream.get());
        });
    if (!launch.route_found)
        throw std::logic_error("CUDA Hydro route is unavailable");
    check_cuda(launch.error, "launch Hydro stage");
    quiesce();
    impl_->runtime_counters.kernel_count +=
        static_cast<std::uint64_t>(launch.kernels_launched);
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
    check_cuda(cudaMemcpyAsync(
                   plan->device_blocks.get(), plan->host_blocks.data(),
                   descriptor_bytes, cudaMemcpyHostToDevice,
                   impl_->stream.get()),
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
    impl_->runtime_counters.bytes_h2d += descriptor_bytes;
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

state::CompletionToken CudaBackend::execute_same_level_exchange(
    std::span<const backend::BackendStateAccess> accesses,
    const amr::SameLevelExchangePlan& plan, state::StateSlot slot,
    state::StateVersion source_version,
    state::CompletionToken expected)
{
    if (!state::is_valid(source_version) || !complete_token(expected)
        || !amr::is_valid(plan.epoch) || plan.fingerprint == 0
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
        auto& block = impl_->require_block(access);
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
        DeviceAllocation<DeviceExchangeOperation> device_operations;
        DeviceAllocation<double> scratch;
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

    DeviceAllocation<DeviceExchangeBlock> device_blocks;
    device_blocks.allocate(host_blocks.size());
    check_cuda(cudaMemcpyAsync(
                   device_blocks.get(), host_blocks.data(),
                   host_blocks.size() * sizeof(DeviceExchangeBlock),
                   cudaMemcpyHostToDevice, impl_->stream.get()),
               "upload CUDA exchange blocks");
    for (auto& prepared : prepared_phases) {
        if (prepared.operations.empty()) continue;
        prepared.device_operations.allocate(prepared.operations.size());
        prepared.scratch.allocate(static_cast<std::size_t>(
            prepared.cells * field_count));
        check_cuda(cudaMemcpyAsync(
                       prepared.device_operations.get(),
                       prepared.operations.data(),
                       prepared.operations.size()
                           * sizeof(DeviceExchangeOperation),
                       cudaMemcpyHostToDevice, impl_->stream.get()),
                   "upload CUDA exchange operations");
    }

    for (auto& prepared : prepared_phases) {
        if (prepared.operations.empty()) continue;
        check_cuda(launch_cuda_backend_exchange_phase(
                       device_blocks.get(), prepared.device_operations.get(),
                       static_cast<int>(prepared.operations.size()),
                       static_cast<int>(field_count), prepared.cells,
                       prepared.scratch.get(), impl_->stream.get()),
                   "launch CUDA same-level exchange phase");
        impl_->runtime_counters.kernel_count += 2;
        quiesce();
    }
    return expected;
}

state::CompletionToken CudaBackend::execute_coarse_fine_exchange(
    std::span<const backend::BackendStateAccess> accesses,
    const amr::CoarseFineTransferPlan& plan, state::StateSlot slot,
    state::StateVersion source_version,
    state::CompletionToken expected)
{
    if (!state::is_valid(source_version) || !complete_token(expected)
        || accesses.empty())
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
        CudaBlockRuntime& block = impl_->require_block(access);
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
        if (lowered.source_count == 0
            || lowered.source_count > std::size(lowered.source_cells))
            throw std::invalid_argument(
                "CUDA coarse-fine source count is invalid");
        for (std::size_t cell = 0; cell < lowered.source_count; ++cell) {
            lowered.source_cells[cell] = cell_index(
                host_blocks[source->second].grid,
                transfer.source_cells[cell]);
        }
        host_transfers.push_back(lowered);
    }
    if (host_transfers.empty()) return expected;

    DeviceAllocation<DeviceExchangeBlock> device_blocks;
    DeviceAllocation<DeviceCoarseFineTransfer> device_transfers;
    DeviceAllocation<double> scratch;
    device_blocks.allocate(host_blocks.size());
    device_transfers.allocate(host_transfers.size());
    if (host_transfers.size()
        > std::numeric_limits<std::size_t>::max() / field_count)
        throw std::overflow_error(
            "CUDA coarse-fine scratch size overflow");
    scratch.allocate(static_cast<std::size_t>(
        host_transfers.size() * field_count));
    check_cuda(cudaMemcpyAsync(
                   device_blocks.get(), host_blocks.data(),
                   host_blocks.size() * sizeof(DeviceExchangeBlock),
                   cudaMemcpyHostToDevice, impl_->stream.get()),
               "upload CUDA coarse-fine blocks");
    check_cuda(cudaMemcpyAsync(
                   device_transfers.get(), host_transfers.data(),
                   host_transfers.size() * sizeof(DeviceCoarseFineTransfer),
                   cudaMemcpyHostToDevice, impl_->stream.get()),
               "upload CUDA coarse-fine transfers");
    check_cuda(launch_cuda_backend_coarse_fine_exchange(
                   device_blocks.get(), device_transfers.get(),
                   static_cast<int>(host_transfers.size()),
                   static_cast<int>(field_count), scratch.get(),
                   impl_->stream.get()),
               "launch CUDA coarse-fine exchange");
    impl_->runtime_counters.kernel_count += 2;
    quiesce();
    return expected;
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
