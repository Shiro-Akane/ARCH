/**
 * @file CudaBackendHydroControl.cpp
 * @brief Coordinate CUDA hydro, boundary exchange, reflux and slot rotation.
 *
 * Validate store generations and shared stage/plan descriptors before binding
 * device views. Runtime-owned scratch and transfer metadata remain alive through
 * stream completion; ghost and slot visibility follow the common driver contract.
 * Flux, CFL, interpolation and geometry formulas stay in their shared owners.
 */

#include "amr/flux/FluxRegister.h"
#include "cuda/runtime/control/CudaBackendInternal.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <climits>
#include <string>
#include <utility>

#include "amr/exchange/CoarseFineCellPlan.h"
#include "amr/transfer/LimitedLinearProlongation.h"
#include "amr/transfer/RegridTransferMath.h"
#include "cuda/hydro/GridGeometryAdapter.cuh"
#include "driver/dispatch/PolicyDescriptor.h"
#include "numerics/reconstruction/AMRInterfaceStencil.h"
#include "physics/gravity/IGravityPolicy.h"

namespace arch::cuda {
namespace {

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
    std::vector<DeviceHydroDtBatchBlock> bindings;
    bindings.reserve(blocks.size());
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        auto& block = *blocks[index];
        bindings.push_back({block.require_access(currents[index]), block.grid,
            block.cfl_candidates.get(), scratch.dt->get() + index,
            scratch.status->get() + index});
    }
    auto& device = impl_->hydro_dt_bindings;
    device.reserve(bindings.size());
    CudaQuiescenceGuard work_guard{*impl_};
    enqueue_cuda_metadata_upload(device.get(), bindings.data(),
        bindings.size() * sizeof(DeviceHydroDtBatchBlock), impl_->stream.get(),
        impl_->runtime_counters, "upload Hydro CFL bindings");
    CudaBackendLaunchResult launch{};
    visit_eos(impl_->eos, [&](const auto& eos) {
        launch = launch_cuda_backend_hydro_dt_batch(bindings, device.get(), eos,
            cfl, impl_->species_workspace, impl_->stream.get());
    });
    check_cuda(launch.error, "launch Hydro dt batch");
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
    impl_->runtime_counters.kernel_count += launch.kernels_launched;
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
    auto* flux = impl_->active_amr_flux.get();
    if (flux && flux->distributed) {
        flux->captured_stage.reset();
        flux->register_upload_ready = false;
        if (currents.size() != flux->runtime_blocks.size())
            throw std::invalid_argument("distributed CUDA flux stage requires all local blocks");
    }
    validate_hydro_batch_accesses(currents);
    const auto plan = scheduler::make_hydro_plan(
        hydro_method(impl_->launch.plan.time_integrator));
    if (!complete_token(expected) || descriptor.stage <= 0
        || descriptor.stage > static_cast<int>(plan.stages.size())
        || !scheduler::same_stage_descriptor(descriptor, plan.stages[descriptor.stage - 1]))
        throw std::invalid_argument("invalid Hydro stage contract");
    if (currents.empty()) return expected;
    const int species_count = impl_->species_view.count;
    // Complete the actual request preflight before resetting/uploading any
    // numerical binding, receipt, field, observer or flux-register value.
    for (const auto current : currents) {
        auto& block = impl_->require_block(current);
        static_cast<void>(block.require_access(current));
    }
    impl_->select_device();
    auto& scratch = impl_->hydro_batch;
    scratch.ensure_capacity(currents.size());
    scratch.ensure_repairs(currents.size(), species_count);
    const int repair_stride = state::RepairView::fixed_size + 2 * species_count;
    std::vector<DeviceHydroBatchBlock> blocks;
    blocks.reserve(currents.size());
    for (std::size_t index = 0; index < currents.size(); ++index) {
        const auto current = currents[index];
        auto& block = impl_->require_block(current);
        static_cast<void>(block.require_access(current));
        const bool self=impl_->launch.self_gravity;
        if(self && (!gravity_ready_ || block.gravity_generation!=gravity_generation_ || block.self_gravity.density!=block.slots[slot_index(descriptor.input_slot)].rho))
            throw std::logic_error("CUDA Hydro self-gravity field is stale or missing");
        blocks.push_back({block.slots[slot_index(descriptor.old_slot)],
            block.slots[slot_index(descriptor.input_slot)],
            block.slots[slot_index(descriptor.output_slot)],
            block.hydro_delta.view(), block.face_flux.view(), block.grid,
            scratch.status->get() + index,
            make_cuda_amr_route_views(impl_->active_amr_flux.get(), current.block),
            {scratch.repairs->get() + index * repair_stride, species_count},
            self?block.self_gravity:Physical::Gravity::GravityPatchView{},
            block.mean_pressure.get(), block.mean_sound_speed.get(), impl_->launch.roe_wave_speed});

    }
    stage_repairs.reset(species_count);
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
    const auto kernels = static_cast<std::uint64_t>(launch.kernels_launched);
    // A rejected partial launch still drains before recording its reported
    // work. Neither a counter nor a successful enqueue grants completion.
    if (!launch.route_found || launch.error != cudaSuccess) {
        quiesce();
        work_guard.completed = true;
        impl_->runtime_counters.kernel_count += kernels;
    }
    if (!launch.route_found) throw std::logic_error("CUDA Hydro batch route is unavailable");
    check_cuda(launch.error, "launch Hydro stage batch");
    // Distinct latches prevent a valid later block from clearing earlier EOS
    // failures. The ordered stream still protects shared species scratch and
    // registers face flux before its block scratch is overwritten.
    check_cuda(cudaMemcpyAsync(scratch.host_status.data(), scratch.status->get(),
                   currents.size() * sizeof(int), cudaMemcpyDeviceToHost,
                   impl_->stream.get()), "download Hydro stage batch EOS status");
    impl_->runtime_counters.bytes_d2h += currents.size() * sizeof(int);
    check_cuda(cudaMemcpyAsync(scratch.host_repairs.data(), scratch.repairs->get(),
        scratch.host_repairs.size() * sizeof(double), cudaMemcpyDeviceToHost, impl_->stream.get()),
        "download Hydro stage repair summaries");
    impl_->runtime_counters.bytes_d2h += scratch.host_repairs.size() * sizeof(double);

    quiesce();
    work_guard.completed = true;
    // Record known completed work before checking the actual numerical latch,
    // so a drained EOS failure retains its real attempted stage cost.
    impl_->runtime_counters.kernel_count += kernels;
    for (std::size_t index = 0; index < currents.size(); ++index) {
        if (scratch.host_status[index] != 0)
            throw std::runtime_error("Invalid CUDA hydro stage EOS query: stage="
                + std::to_string(descriptor.stage) + " block="
                + std::to_string(currents[index].block.uid.value) + " epoch="
                + std::to_string(currents[index].block.epoch.value) + " status="
                + std::to_string(scratch.host_status[index]));
    }
    for (std::size_t index = 0; index < currents.size(); ++index) {
        state::RepairBudget report(species_count);
        std::copy_n(scratch.host_repairs.data() + index * repair_stride, repair_stride, report.values.data());
        report.block_uid = currents[index].block.uid.value;
        stage_repairs.combine(report);
    }
    if (flux && flux->distributed) flux->captured_stage = expected;

    return expected;
}

std::vector<std::vector<double>> CudaBackend::download_amr_flux_stage(
    const amr::AmrFluxTopologyPlan& topology, state::CompletionToken completed)
{
    auto* plan = impl_->active_amr_flux.get();
    if (!plan || !plan->distributed || !plan->captured_stage
        || *plan->captured_stage != completed || !complete_token(completed)
        || plan->topology_fingerprint != topology.fingerprint
        || plan->epoch != topology.epoch || plan->routes.size() != topology.routes.size())
        throw std::invalid_argument("CUDA flux capture has no matching completed stage");
    amr::validate_amr_flux_topology_plan(topology);
    // Allocate all Host packet storage before the first enqueue. Each surface
    // is copied once; duplicate operations retain the original recipe ordering.
    std::map<std::pair<int, int>, std::vector<double>> surfaces;
    std::vector<std::vector<double>> values(plan->routes.size());
    for (std::size_t i = 0; i < plan->routes.size(); ++i) {
        const auto& route = *plan->routes[i];
        if (route.compiled.logical_fingerprint != topology.routes[i].plan.fingerprint
            || route.capture_offsets.size() != topology.routes[i].plan.operations.size())
            throw std::invalid_argument("CUDA flux capture recipe drifted");
        values[i].resize(route.capture_offsets.size());
        for (const auto& [face, offset] : route.capture_offsets) {
            const auto surface = plan->host_blocks.at(route.compiled.source_block).initial_flux[face];
            const auto count = amr_flux_surface_scalar_count(surface.cell_count, surface.species_count);
            if (!valid_amr_flux_surface(surface) || offset < 0
                || static_cast<std::size_t>(offset) >= count)
                throw std::invalid_argument("CUDA flux capture surface extent drifted");
            surfaces.try_emplace({route.compiled.source_block, face}, count);
        }
    }
    impl_->select_device();
    CudaQuiescenceGuard guard{*impl_};
    for (auto& [key, packet] : surfaces) {
        const auto surface = plan->host_blocks[key.first].initial_flux[key.second];
        check_cuda(cudaMemcpyAsync(packet.data(), surface.rho, packet.size() * sizeof(double),
            cudaMemcpyDeviceToHost, impl_->stream.get()), "download captured CUDA face flux");
        impl_->runtime_counters.bytes_d2h += packet.size() * sizeof(double);
    }
    impl_->checked_quiesce("complete CUDA face-flux packets");
    guard.completed = true;
    for (std::size_t i = 0; i < plan->routes.size(); ++i) {
        const auto& route = *plan->routes[i];
        for (std::size_t n = 0; n < route.capture_offsets.size(); ++n) {
            const auto [face, offset] = route.capture_offsets[n];
            values[i][n] = surfaces.at({route.compiled.source_block, face})[offset];
        }
    }
    plan->captured_stage.reset();
    return values;
}

void CudaBackend::upload_amr_flux_register(const amr::AmrFluxTopologyPlan& topology,
    const amr::FluxRegister& flux, std::uint64_t completed_stages)
{
    auto* plan = impl_->active_amr_flux.get();
    if (plan) plan->register_upload_ready = false;
    if (!plan || !plan->distributed || plan->topology_fingerprint != topology.fingerprint
        || plan->epoch != topology.epoch || flux.GetNumSpecies() != plan->species_count)
        throw std::invalid_argument("CUDA flux upload topology/storage mismatch");
    amr::validate_amr_flux_topology_plan(topology);
    flux.RequireDistributedStages(topology.fingerprint, completed_stages);
    std::vector<std::pair<DeviceAmrFluxSurfaceView, std::vector<double>>> packets;
    for (const auto& requirement : plan->surface_requirements) {
        if (!(requirement.roles & static_cast<std::uint8_t>(amr::AmrFluxSurfaceRole::Register)))
            continue;
        const int block = topology.pool_lowering.at(topology.active_endpoints.at(requirement.block));
        const auto surface = plan->host_blocks.at(requirement.block).registers[requirement.face];
        if (!valid_amr_flux_surface(surface) || surface.cell_count != requirement.cell_count
            || surface.species_count != plan->species_count)
            throw std::invalid_argument("CUDA flux upload surface layout is invalid");
        auto& packet = packets.emplace_back(surface, amr_flux_surface_scalar_count(
            surface.cell_count, surface.species_count)).second;
        for (int cell = 0; cell < surface.cell_count; ++cell) {
            const auto value = flux.GetSummedFlux(block, requirement.face, cell);
            const std::array fluid{value.rho, value.mom_u, value.mom_v, value.mom_w, value.eng};
            for (int field = 0; field < 5; ++field)
                packet[field * surface.cell_count + cell] = fluid[field];
            for (int species = 0; species < surface.species_count; ++species)
                packet[(5 + species) * surface.cell_count + cell] =
                    flux.GetSummedSpeciesFlux(block, requirement.face, cell, species);
        }
        if (!std::all_of(packet.begin(), packet.end(), [](double v) { return std::isfinite(v); }))
            throw std::invalid_argument("CUDA flux upload contains nonfinite accumulated values");
    }
    impl_->select_device();
    CudaQuiescenceGuard guard{*impl_};
    for (const auto& [surface, packet] : packets) {
        check_cuda(cudaMemcpyAsync(surface.rho, packet.data(), packet.size() * sizeof(double),
            cudaMemcpyHostToDevice, impl_->stream.get()), "upload accepted CUDA reflux surface");
        impl_->runtime_counters.bytes_h2d += packet.size() * sizeof(double);
    }
    impl_->checked_quiesce("complete accepted CUDA reflux surfaces");
    guard.completed = true;
    plan->register_upload_ready = true;
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

    plan->captured_stage.reset();
    plan->register_upload_ready = false;
    // The caller may have selected another device; launches use our stream.
    impl_->select_device();
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

    if (plan->distributed && !plan->register_upload_ready)
        throw std::logic_error("CUDA distributed reflux requires accepted uploaded registers");
    // A failed launch or validation cannot leave a reusable publication receipt.
    plan->register_upload_ready = false;
    // Authenticate every original plan owner/shape before rebinding or clearing
    // even an empty report. Slot rotation changes views, not owners.
    for (const auto* block : plan->runtime_blocks) {
        if (!block || block->handle.epoch != plan->epoch
            || !valid_hydro_grid(block->grid)
            || !valid_hydro_view(block->slots[slot_index(slot)])
            || block->slots[slot_index(slot)].n_species != plan->species_count
            || block->slots[slot_index(slot)].total_size != block->grid.total_size)
            throw std::logic_error("CUDA AMR reflux actual owner/chart/shape drifted");
    }
    impl_->select_device();
    auto& scratch = impl_->reflux_batch;
    const int species_count = impl_->species_view.count;
    reflux_repairs.reset(species_count);
    const int repair_stride = state::RepairView::fixed_size + 2 * species_count;
    scratch.ensure_repairs(plan->host_blocks.size(), species_count);

    // Slot rotation changes the DeviceStateView pointers.  Rebind immediately
    // before reflux so Hydro targets the rotated Current buffer and RKL targets
    // the descriptor's not-yet-published output slot.
    for (std::size_t index = 0; index < plan->host_blocks.size(); ++index) {
        CudaBlockRuntime* const block = plan->runtime_blocks[index];
        if (block == nullptr)
            throw std::logic_error("CUDA AMR flux block was retired early");
        plan->host_blocks[index].state = block->slots[slot_index(slot)];
        plan->host_blocks[index].repairs = {
            scratch.repairs->get() + index * repair_stride, species_count};
    }

    const auto& reflux = plan->compiled_reflux;
    if (reflux.targets.empty()) return expected;
    if (reflux.contributions.empty())
        throw std::logic_error("CUDA AMR reflux plan has no contributions");
    const std::size_t descriptor_bytes = plan->host_blocks.size()
        * sizeof(DeviceAmrFluxBlockView);
    CudaQuiescenceGuard reflux_guard{*impl_};
    check_cuda(cudaMemsetAsync(scratch.repairs->get(), 0,
        scratch.host_repairs.size() * sizeof(double), impl_->stream.get()),
        "reset reflux repair receipts");
    enqueue_cuda_metadata_upload(
                   plan->device_blocks.get(), plan->host_blocks.data(),
                   descriptor_bytes, impl_->stream.get(), impl_->runtime_counters,
               "rebind CUDA AMR reflux state slots");
    if (!plan->reflux_status.get()) plan->reflux_status.allocate(1);
    check_cuda(cudaMemsetAsync(plan->reflux_status.get(),0,sizeof(int),impl_->stream.get()),"reset reflux status");
    int reflux_status=0;
    check_cuda(launch_cuda_amr_reflux(
                   plan->device_blocks.get(),
                   static_cast<int>(plan->host_blocks.size()),
                   plan->reflux_targets.get(),
                   static_cast<int>(reflux.targets.size()),
                   plan->reflux_contributions.get(),
                   static_cast<int>(reflux.contributions.size()),
                   dt, impl_->stream.get(),plan->reflux_status.get(),impl_->launch.density_floor,
                   impl_->launch.minimum_internal_energy,impl_->launch.maximum_internal_energy),
               "launch CUDA AMR reflux");
    check_cuda(cudaMemcpyAsync(&reflux_status,plan->reflux_status.get(),sizeof(int),
        cudaMemcpyDeviceToHost,impl_->stream.get()),"download reflux status");
    impl_->runtime_counters.bytes_d2h += sizeof(int);
    check_cuda(cudaMemcpyAsync(scratch.host_repairs.data(), scratch.repairs->get(),
        scratch.host_repairs.size() * sizeof(double), cudaMemcpyDeviceToHost, impl_->stream.get()),
        "download reflux repair receipts");
    impl_->runtime_counters.bytes_d2h += scratch.host_repairs.size() * sizeof(double);
    quiesce();
    reflux_guard.completed=true;
    if (dt > 0.0) ++impl_->runtime_counters.kernel_count;
    if (reflux_status) throw std::runtime_error("Invalid state after CUDA AMR reflux: status="+std::to_string(reflux_status));
    for (std::size_t index = 0; index < plan->host_blocks.size(); ++index) {
        state::RepairBudget report(species_count);
        std::copy_n(scratch.host_repairs.data() + index * repair_stride,
            repair_stride, report.values.data());
        report.block_uid = plan->runtime_blocks[index]->handle.uid.value;
        reflux_repairs.combine(report);
    }
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
    impl_->select_device();
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
    return impl_->execute_physical_boundary_batch(accesses, version, expected,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            return impl_->require_block(access);
        });
}

state::CompletionToken CudaBackend::execute_physical_boundary_batch(
    backend::BackendTopologyStoreTransaction& transaction,
    std::span<const backend::BackendStateAccess> accesses,
    state::StateVersion version, state::CompletionToken expected)
{
    auto& staged = require_staged_boundary_transaction(transaction);
    return impl_->execute_physical_boundary_batch(accesses, version, expected,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            if (access.slot != state::StateSlot::Current)
                throw std::invalid_argument("staged boundary requires Current");
            return resolve_staged_block(staged, access);
        });
}

state::CompletionToken CudaBackend::Impl::execute_physical_boundary_batch(
    std::span<const backend::BackendStateAccess> accesses,
    state::StateVersion version, state::CompletionToken expected,
    const BlockResolver& resolve)
{
    const auto slot = accesses.empty() ? state::StateSlot::Current : accesses.front().slot;
    if (slot != state::StateSlot::Current && slot != state::StateSlot::Next
        && slot != state::StateSlot::Scratch)
        throw std::invalid_argument("Invalid backend batch slot");
    if (!state::is_valid(version) || !complete_token(expected))
        throw std::invalid_argument("invalid boundary batch completion contract");
    if (accesses.empty()) return expected;
    if (accesses.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::overflow_error("boundary batch exceeds launch index extent");
    std::vector<const CudaBlockRuntime*> owners;
    owners.reserve(accesses.size());
    std::vector<DeviceBoundaryBatchBlock> blocks;
    blocks.reserve(accesses.size());
    std::array<int, 3> phase_counts{};
    for (const auto access : accesses) {
        if (access.slot != slot)
            throw std::invalid_argument("Invalid backend batch access");
        const auto& block = resolve(access);
        owners.push_back(&block);
        const auto selected = block.require_access(access);
        check_cuda(validate_boundary_launch(selected, block.boundary_transfers.get(),
            block.boundary), "validate boundary batch block");
        blocks.push_back({selected, block.boundary_transfers.get(), block.boundary.phases});
        for (std::size_t phase = 0; phase < phase_counts.size(); ++phase)
            phase_counts[phase] = std::max(phase_counts[phase],
                static_cast<int>(block.boundary.phases[phase].count));
    }
    std::sort(owners.begin(), owners.end(), std::less<>{});
    if (std::adjacent_find(owners.begin(), owners.end()) != owners.end())
        throw std::invalid_argument("Duplicate Hydro batch block");
    select_device();
    auto& device_blocks = boundary_bindings;
    device_blocks.reserve(blocks.size());
    int kernels = 0;
    CudaQuiescenceGuard work_guard{*this};
    enqueue_cuda_metadata_upload(device_blocks.get(), blocks.data(),
        blocks.size() * sizeof(DeviceBoundaryBatchBlock), stream.get(),
        runtime_counters, "upload boundary batch bindings");
    check_cuda(launch_cuda_backend_boundary_batch(device_blocks.get(),
        static_cast<int>(blocks.size()), phase_counts, stream.get(), kernels),
        "launch boundary batch");
    checked_quiesce("synchronize CUDA backend");
    work_guard.completed = true;
    runtime_counters.kernel_count += static_cast<std::uint64_t>(kernels);
    return expected;
}

void CudaBackend::gather_exchange_packet(
    std::span<const backend::BackendStateAccess> accesses,
    std::span<const amr::ExchangePacketSlice> slices, std::span<std::uint64_t> packet)
{
    impl_->exchange_packet(accesses, slices, packet, {}, true,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            return impl_->require_block(access);
        });
}

void CudaBackend::scatter_exchange_packet(
    std::span<const backend::BackendStateAccess> accesses,
    std::span<const amr::ExchangePacketSlice> slices, std::span<const std::uint64_t> packet)
{
    impl_->exchange_packet(accesses, slices, {}, packet, false,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            return impl_->require_block(access);
        });
}

void CudaBackend::gather_exchange_packet(
    backend::BackendTopologyStoreTransaction& transaction,
    std::span<const backend::BackendStateAccess> accesses,
    std::span<const amr::ExchangePacketSlice> slices, std::span<std::uint64_t> packet)
{
    auto& staged = require_staged_boundary_transaction(transaction);
    impl_->exchange_packet(accesses, slices, packet, {}, true,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            if (access.slot != state::StateSlot::Current)
                throw std::invalid_argument("staged packet requires Current");
            return resolve_staged_block(staged, access);
        });
}

void CudaBackend::scatter_exchange_packet(
    backend::BackendTopologyStoreTransaction& transaction,
    std::span<const backend::BackendStateAccess> accesses,
    std::span<const amr::ExchangePacketSlice> slices, std::span<const std::uint64_t> packet)
{
    auto& staged = require_staged_boundary_transaction(transaction);
    impl_->exchange_packet(accesses, slices, {}, packet, false,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            if (access.slot != state::StateSlot::Current)
                throw std::invalid_argument("staged packet requires Current");
            return resolve_staged_block(staged, access);
        });
}

void CudaBackend::Impl::exchange_packet(
    std::span<const backend::BackendStateAccess> accesses,
    std::span<const amr::ExchangePacketSlice> slices,
    std::span<std::uint64_t> outgoing, std::span<const std::uint64_t> incoming, bool gather,
    const BlockResolver& resolve)
{
    const auto words = gather ? outgoing.size() : incoming.size();
    if (accesses.empty() || accesses.size() > static_cast<std::size_t>(INT_MAX)
        || slices.size() > static_cast<std::size_t>(INT_MAX)
        || words > static_cast<std::size_t>(INT_MAX))
        throw std::invalid_argument("invalid CUDA exchange packet extent");
    std::map<amr::BlockHandle, int> indices;
    std::vector<DeviceExchangeBlock> host_blocks;
    host_blocks.reserve(accesses.size());
    int species = -1;
    for (const auto access : accesses) {
        if (access.slot != accesses.front().slot
            || access.block.epoch != accesses.front().block.epoch)
            throw std::invalid_argument("CUDA exchange packet slot/epoch mismatch");
        auto& block = resolve(access);
        const auto selected = block.require_access(access);
        if (!indices.emplace(access.block, static_cast<int>(host_blocks.size())).second)
            throw std::invalid_argument("duplicate CUDA exchange packet block");
        if (selected.n_species < 0 || selected.n_species > INT_MAX - 6
            || (species >= 0 && species != selected.n_species))
            throw std::invalid_argument("CUDA exchange packet species mismatch");
        species = selected.n_species;
        host_blocks.push_back({selected, block.grid});
    }
    const std::size_t fields = 6 + species;
    std::vector<DeviceExchangeOperation> operations;
    operations.reserve(slices.size());
    std::vector<std::vector<amr::LogicalExchangeBox>> destinations;
    if (!gather) destinations.resize(host_blocks.size());
    for (const auto& slice : slices) {
        const auto found = indices.find(slice.block);
        if (found == indices.end())
            throw std::invalid_argument("CUDA exchange packet endpoint is missing");
        const auto& grid = host_blocks[found->second].grid;
        bool outside_interior = false;
        for (int axis = 0; axis < 3; ++axis) {
            const std::int64_t origin = axis == 0 ? grid.is : axis == 1 ? grid.js : grid.ks;
            const std::int64_t end_active = axis == 0 ? grid.ie : axis == 1 ? grid.je : grid.ke;
            const std::int64_t total = axis == 0 ? grid.total_x : axis == 1 ? grid.total_y : grid.total_z;
            const std::int64_t first = origin + slice.box.first[axis];
            const std::int64_t end = first + slice.box.extent[axis];
            if (!slice.box.extent[axis] || first < 0 || end > total)
                throw std::out_of_range("CUDA exchange packet box is outside storage");
            outside_interior |= end <= origin || first >= end_active;
        }
        if (!gather && !outside_interior)
            throw std::invalid_argument("CUDA exchange packet scatter intersects active cells");
        const auto cells = amr::exchange_detail::checked_product(slice.box.extent);
        if (slice.word_offset % fields || slice.word_offset > words
            || cells > (words - slice.word_offset) / fields)
            throw std::invalid_argument("CUDA exchange packet slice exceeds payload");
        DeviceExchangeOperation operation{};
        operation.source_block = gather ? found->second : -1;
        operation.destination_block = gather ? -1 : found->second;
        operation.scratch_first = slice.word_offset / fields;
        for (int axis = 0; axis < 3; ++axis) {
            operation.source_first[axis] = operation.destination_first[axis] = slice.box.first[axis];
            operation.extent[axis] = slice.box.extent[axis];
        }
        if (!gather) {
            auto& boxes = destinations[found->second];
            for (const auto& prior : boxes) {
                bool disjoint = false;
                for (int axis = 0; axis < 3; ++axis)
                    disjoint |= static_cast<std::int64_t>(prior.first[axis]) + prior.extent[axis]
                                    <= slice.box.first[axis]
                        || static_cast<std::int64_t>(slice.box.first[axis]) + slice.box.extent[axis]
                                    <= prior.first[axis];
                if (!disjoint)
                    throw std::invalid_argument("overlapping CUDA exchange packet destinations");
            }
            boxes.push_back(slice.box);
        }
        operations.push_back(operation);
    }
    std::sort(operations.begin(), operations.end(), [](const auto& a, const auto& b) {
        return a.scratch_first < b.scratch_first;
    });
    std::uint64_t cells = 0;
    for (const auto& operation : operations) {
        if (operation.scratch_first != cells)
            throw std::invalid_argument("CUDA exchange packet has gaps or overlapping payload");
        cells += static_cast<std::uint64_t>(operation.extent[0])
            * operation.extent[1] * operation.extent[2];
    }
    if (cells * fields != words)
        throw std::invalid_argument("CUDA exchange packet payload is not fully covered");
    if (!words) return;
    select_device();
    // Reuse the original exchange arena. Every call drains this same stream
    // before MPI, before a following axis, and before any scratch reuse.
    auto& blocks = exchange_scratch.blocks;
    auto& buffers = exchange_scratch.phases[0];
    blocks.reserve(host_blocks.size());
    buffers.operations.reserve(operations.size());
    buffers.values.reserve(words);
    CudaQuiescenceGuard work_guard{*this};
    enqueue_cuda_metadata_upload(blocks.get(), host_blocks.data(),
        host_blocks.size() * sizeof(DeviceExchangeBlock), stream.get(), runtime_counters,
        "upload CUDA packet blocks");
    enqueue_cuda_metadata_upload(buffers.operations.get(), operations.data(),
        operations.size() * sizeof(DeviceExchangeOperation), stream.get(), runtime_counters,
        "upload CUDA packet operations");
    static_assert(sizeof(double) == sizeof(std::uint64_t));
    const auto bytes = words * sizeof(std::uint64_t);
    if (!gather) {
        check_cuda(cudaMemcpyAsync(buffers.values.get(), incoming.data(), bytes,
            cudaMemcpyHostToDevice, stream.get()), "upload CUDA exchange packet");
        runtime_counters.bytes_h2d += bytes;
    }
    check_cuda(launch_cuda_backend_exchange_packet(blocks.get(), buffers.operations.get(),
        static_cast<int>(operations.size()), static_cast<int>(fields), cells,
        buffers.values.get(), stream.get(), gather), "launch CUDA exchange packet");
    ++runtime_counters.kernel_count;
    if (gather) {
        check_cuda(cudaMemcpyAsync(outgoing.data(), buffers.values.get(), bytes,
            cudaMemcpyDeviceToHost, stream.get()), "download CUDA exchange packet");
        runtime_counters.bytes_d2h += bytes;
    }
    checked_quiesce("complete CUDA exchange packet");
    work_guard.completed = true;
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

state::CompletionToken CudaBackend::execute_same_level_exchange(
    backend::BackendTopologyStoreTransaction& transaction,
    std::span<const backend::BackendStateAccess> accesses,
    const amr::SameLevelExchangePlan& plan, state::StateSlot slot,
    state::StateVersion source_version,
    state::CompletionToken expected)
{
    auto& staged = require_staged_boundary_transaction(transaction);
    if (slot != state::StateSlot::Current)
        throw std::invalid_argument("staged exchange requires Current");
    if (!state::is_valid(source_version) || !complete_token(expected))
        throw std::invalid_argument("invalid CUDA same_level exchange completion");
    impl_->execute_same_level_exchange(accesses, plan, slot,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            if (access.slot != state::StateSlot::Current)
                throw std::invalid_argument("staged exchange requires Current");
            return resolve_staged_block(staged, access);
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

state::CompletionToken CudaBackend::execute_coarse_fine_exchange(
    backend::BackendTopologyStoreTransaction& transaction,
    std::span<const backend::BackendStateAccess> accesses,
    const amr::CoarseFineTransferPlan& plan, state::StateSlot slot,
    state::StateVersion source_version,
    state::CompletionToken expected)
{
    auto& staged = require_staged_boundary_transaction(transaction);
    if (slot != state::StateSlot::Current)
        throw std::invalid_argument("staged exchange requires Current");
    if (!state::is_valid(source_version) || !complete_token(expected))
        throw std::invalid_argument("invalid CUDA coarse_fine exchange completion");
    impl_->execute_coarse_fine_exchange(accesses, plan, slot,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            if (access.slot != state::StateSlot::Current)
                throw std::invalid_argument("staged exchange requires Current");
            return resolve_staged_block(staged, access);
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
    const auto& first = resolve(accesses.front());
    const auto cells = amr::compile_coarse_fine_cell_plan(
        plan, first.require_access(accesses.front()).n_species);
    execute_coarse_fine_cells(accesses, cells, slot, resolve, false, {});
}

void CudaBackend::gather_coarse_fine_packet(
    std::span<const backend::BackendStateAccess> accesses,
    const amr::CoarseFineCellPlan& cells, std::span<double> outgoing)
{
    if (accesses.empty())
        throw std::invalid_argument("CUDA coarse-fine packet requires local source bindings");
    impl_->execute_coarse_fine_cells(accesses, cells, accesses.front().slot,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            return impl_->require_block(access);
        }, true, outgoing);
}

void CudaBackend::gather_coarse_fine_packet(
    backend::BackendTopologyStoreTransaction& transaction,
    std::span<const backend::BackendStateAccess> accesses,
    const amr::CoarseFineCellPlan& cells, std::span<double> outgoing)
{
    auto& staged = require_staged_boundary_transaction(transaction);
    if (accesses.empty())
        throw std::invalid_argument("staged coarse-fine packet requires source bindings");
    impl_->execute_coarse_fine_cells(accesses, cells, state::StateSlot::Current,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            if (access.slot != state::StateSlot::Current)
                throw std::invalid_argument("staged coarse-fine packet requires Current");
            return resolve_staged_block(staged, access);
        }, true, outgoing);
}

void CudaBackend::Impl::execute_coarse_fine_cells(
    std::span<const backend::BackendStateAccess> accesses,
    const amr::CoarseFineCellPlan& cell_plan, state::StateSlot slot,
    const BlockResolver& resolve, bool gather_only, std::span<double> outgoing)
{
    if (accesses.empty() || accesses.size() > static_cast<std::size_t>(INT_MAX)
        || cell_plan.dimension < 1 || cell_plan.dimension > 3)
        throw std::invalid_argument("invalid CUDA coarse-fine cell bindings");
    std::map<amr::BlockHandle, int> indices;
    std::vector<DeviceExchangeBlock> host_blocks;
    host_blocks.reserve(accesses.size());
    int species_count = -1;
    for (const backend::BackendStateAccess& access : accesses) {
        if (access.slot != slot
            || access.block.epoch != cell_plan.scope.from_epoch)
            throw std::invalid_argument(
                "stale CUDA coarse-fine exchange access");
        CudaBlockRuntime& block = resolve(access);
        if (!indices.emplace(
                access.block, static_cast<int>(host_blocks.size())).second)
            throw std::invalid_argument(
                "duplicate CUDA coarse-fine exchange access");
        const DeviceStateView selected = block.require_access(access);
        if (block.grid.dim != cell_plan.dimension)
            throw std::invalid_argument(
                "CUDA coarse-fine exchange dimension mismatch");
        if (species_count >= 0 && selected.n_species != species_count)
            throw std::invalid_argument(
                "CUDA coarse-fine exchange species counts differ");
        species_count = selected.n_species;
        host_blocks.push_back({selected, block.grid});
    }
    if (species_count < 0 || species_count > INT_MAX - 6)
        throw std::invalid_argument(
            "CUDA coarse-fine exchange species extent is invalid");

    if (cell_plan.species_count != species_count)
        throw std::invalid_argument("CUDA coarse-fine packet species mismatch");
    if (cell_plan.transfers.size()
        > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::overflow_error(
            "CUDA coarse-fine transfer count overflow");
    const std::uint64_t field_count = static_cast<std::uint64_t>(
        6 + species_count);
    if (gather_only && (outgoing.size() > static_cast<std::size_t>(INT_MAX)
        || cell_plan.transfers.size() > outgoing.size() / field_count
        || outgoing.size() != cell_plan.transfers.size() * field_count))
        throw std::invalid_argument("CUDA coarse-fine packet extent mismatch");
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
        if (source == indices.end() || (!gather_only && destination == indices.end())
            || transfer.source.handle.epoch != cell_plan.scope.from_epoch
            || transfer.destination.handle.epoch != cell_plan.scope.from_epoch)
            throw std::invalid_argument(
                "CUDA coarse-fine endpoint is missing");
        DeviceCoarseFineTransfer lowered{};
        lowered.source_block = source->second;
        // A source-side packet must never resolve or allocate remote storage.
        lowered.destination_block = -1;
        if (!gather_only) {
            lowered.destination_block = destination->second;
            lowered.destination_cell = cell_index(
                host_blocks[destination->second].grid, transfer.destination_cell);
        }
        const bool injection = transfer.rule == amr::RefinementRule::CoarseGhostInjection;
        if ((!injection && transfer.rule != amr::RefinementRule::FineGhostAverage)
            || transfer.source_count != (injection ? 1 : (1 << cell_plan.dimension)))
            throw std::invalid_argument("CUDA coarse-fine source recipe is invalid");
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
            // The gather carries ordinary V here; the shared kernel recovers
            // the optional Mphi W from this same native grid descriptor.
            if (transfer.rule == amr::RefinementRule::FineGhostAverage
                && source_grid.dim == 2
                && source_geometry.geometry == GridMetrics::Geometry::Cylindrical) {
                const double moment = GridMetrics::AngularMomentumVolumeMoment(
                    source_geometry, source_grid.is + logical[0],
                    source_grid.js + logical[1], source_grid.ks + logical[2]);
                if (!std::isfinite(moment) || moment <= 0.0)
                    throw std::invalid_argument(
                        "CUDA coarse-fine source moment is invalid");
            }
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
    std::size_t scratch_scalars = static_cast<std::size_t>(
        host_transfers.size() * field_count);

    scratch.reserve(scratch_scalars);
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
                       exchange_status.get(), stream.get(), gather_only),
                   "launch CUDA coarse-fine exchange");
    
    check_cuda(cudaMemcpyAsync(
                   &status, exchange_status.get(), sizeof(int),
                   cudaMemcpyDeviceToHost, stream.get()),
               "download CUDA coarse-fine exchange status");
    if (gather_only) {
        check_cuda(cudaMemcpyAsync(outgoing.data(), scratch.get(),
            outgoing.size_bytes(), cudaMemcpyDeviceToHost, stream.get()),
            "download CUDA coarse-fine packet");
        runtime_counters.bytes_d2h += outgoing.size_bytes();
    }
    checked_quiesce("synchronize CUDA coarse_fine exchange");
    work_guard.completed = true;
    runtime_counters.kernel_count += gather_only ? 1 : 2;
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
