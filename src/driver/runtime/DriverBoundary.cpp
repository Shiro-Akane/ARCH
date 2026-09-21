#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "driver/DriverUtils.h"
#include "amr/AMRControl.h"
#include <algorithm>
#include <stdexcept>
#include <utility>
namespace arch::driver {
using scheduler::StageExecutionContext;
using state::ExecutionSide;
using state::StateResidencyLedger;
using state::StateSlot;
using topology::LogicalBlockIdentity;
using topology::TopologyObservation;
void DriverRuntime::publish_current_ghost()
{
    StageExecutionContext context{
        ExecutionSide::Host, *residency_ledger, scheduler_clock};
    (void)arch::scheduler::complete_boundary(
        context, stage_handles, StateSlot::Current,
        current_interior_version(),
        [](StateSlot, arch::state::StateVersion,
           arch::state::CompletionToken token) { return token; });
}

state::CompletionToken DriverRuntime::execute_device_boundary(StateSlot requested, state::StateVersion version, state::CompletionToken token)
{
    auto& accesses = boundary_accesses;
    accesses.clear();
    accesses.reserve(stage_handles.size());
    for (std::size_t index = 0; index < stage_handles.size(); ++index) {
        const auto access = backend_access(index, requested);
        accesses.push_back(access);
    }
    const auto& plans = amr_ctrl.ghost_exchange.GetPlans(
        amr_ctrl.pool, amr_ctrl.tree, config.grid.dim, stage_handles);
    (void)compute_backend->execute_physical_boundary_batch(accesses, version, token);
    for (std::size_t group = 0; group < plans.same_level.size(); ++group) {
        const auto& plan = plans.same_level[group];
        auto& level_accesses = boundary_level_accesses;
        level_accesses.clear();
        level_accesses.reserve(plan.blocks.size());
        for (const auto index : plans.level_indices[group])
            level_accesses.push_back(accesses[index]);
        (void)compute_backend->execute_same_level_exchange(
            level_accesses, plan, requested, version, token);
    }
    return compute_backend->execute_coarse_fine_exchange(
        accesses, plans.coarse_fine, requested, version, token);
}

void DriverRuntime::complete_device_boundary(StateSlot slot)
{
    if (stage_handles.empty())
        throw std::logic_error("CUDA boundary requires active blocks");
    const auto version = residency_ledger->inspect(
        {stage_handles.front(), slot}).interior.version;
    bool needs_device_ghosts = false;
    for (const auto handle : stage_handles) {
        residency_ledger->require_readable(
            {handle, slot},
            {ExecutionSide::Device, version, true, false});
        const auto coherence = residency_ledger->inspect({handle, slot});
        needs_device_ghosts = needs_device_ghosts
            || !arch::state::side_can_read(
                coherence.ghost.residency, ExecutionSide::Device)
            || coherence.ghost.version != version
            || coherence.ghost_source_version != version
            || !arch::state::is_complete(coherence.ghost.completion)
            || coherence.ghost.pending_transfer
                != arch::state::PendingTransferPhase::None;
    }
    // Reuse the completed boundary for this field version. Re-publishing
    // identical Device ghosts would invalidate a synchronized Host copy
    // while leaving the interior synchronized, breaking the whole-state
    // materialization contract required by Host consumers.
    if (!needs_device_ghosts) return;
    const auto before = compute_backend->counters();
    StageExecutionContext context{
        ExecutionSide::Device, *residency_ledger, scheduler_clock};
    (void)arch::scheduler::complete_boundary(
        context, stage_handles, slot, version,
        [&](StateSlot requested, arch::state::StateVersion version,
            arch::state::CompletionToken token) {
            return execute_device_boundary(requested, version, token);
        });
    trace_backend_operation(
        arch::backend::BackendOperation::PhysicalBoundary, slot, before);
}

void DriverRuntime::ensure_fluid_ghosts(StateSlot slot)
{
    if (compute_backend) { complete_device_boundary(slot); return; }
    if (slot != StateSlot::Current)
        throw std::logic_error("Host stage-slot boundaries belong to the integrator");
#pragma omp parallel for schedule(dynamic, 1)
    for (size_t i = 0; i < amr_ctrl.tree->GetActiveBlocks().size(); ++i) {
        amr::Block& block = amr_ctrl.pool->GetBlock(amr_ctrl.tree->GetActiveBlocks()[i]);
        bc_handler.apply(block.fluid_state, block.grid);
    }
    amr_ctrl.ghost_exchange.ExecuteExchange(amr_ctrl.pool, amr_ctrl.tree,
                                            config.grid.dim,
                                            &amr::Block::fluid_state,
                                            stage_handles);
    if (residency_ledger && !stage_handles.empty())
        publish_current_ghost();
}

void DriverRuntime::materialize_current_for_host()
{
    ensure_fluid_ghosts(StateSlot::Current);
    if (!compute_backend) return;
    const auto& active = amr_ctrl.tree->GetActiveBlocks();
    for (std::size_t index = 0; index < stage_handles.size(); ++index) {
        const auto coherence = residency_ledger->inspect(
            {stage_handles[index], StateSlot::Current});
        const bool host_current = arch::state::side_can_read(
                coherence.interior.residency, ExecutionSide::Host)
            && arch::state::side_can_read(
                coherence.ghost.residency, ExecutionSide::Host)
            && coherence.ghost.version == coherence.interior.version
            && coherence.ghost_source_version
                == coherence.interior.version;
        if (!host_current) {
            amr::Block& block = amr_ctrl.pool->GetBlock(active[index]);
            (void)arch::backend::transfer_state_regions(
                *compute_backend, *residency_ledger, scheduler_clock,
                backend_access(index, StateSlot::Current),
                host_transfer_view(block.fluid_state),
                arch::state::PendingTransferPhase::PendingD2H,
                static_cast<std::uint64_t>(ctrl.step_count),
                arch::backend::BackendOperation::Materialize);
        }
    }
}
} // namespace arch::driver
