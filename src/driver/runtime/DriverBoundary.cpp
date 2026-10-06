/**
 * @file DriverBoundary.cpp
 * @brief Apply boundary and ghost operations at the required stage state generation.
 *
 * Workflow:
 * 1. Select the actual Current/Next/Scratch slot and freeze its domain context.
 * 2. Apply physical boundaries and whole-domain halo exchange to that slot.
 * 3. Validate native-RZ closure with the real EOS before publishing ghosts.
 * 4. Record only successfully completed Device boundary context/cache stamps.
 */

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <utility>

#include "amr/AMRControl.h"
#include "driver/DriverUtils.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "numerics/integrator/TimeIntegratorHelper.h"

namespace arch::driver {
using scheduler::StageExecutionContext;
using state::ExecutionSide;
using state::StateResidencyLedger;
using state::StateSlot;
using topology::LogicalBlockIdentity;
using topology::TopologyObservation;
/** Launch the device boundary plan for the requested state version. */
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
    if (bc_handler.has_user()) {
        const auto& active = amr_ctrl.tree->GetActiveBlocks();
        for (std::size_t b = 0; b < accesses.size(); ++b)
            bc_handler.apply_device(*compute_backend, accesses[b], amr_ctrl.pool->GetBlock(active[b]).grid);
    }
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
    const auto coarse_fine_completed = compute_backend->execute_coarse_fine_exchange(
        accesses, plans.coarse_fine, requested, version, token);
    if (coarse_fine_completed != token)
        throw std::logic_error("Device coarse-fine exchange returned incomplete work");
    if (plans.coordinate_seam.transfers.empty()) return coarse_fine_completed;
    // The same physical donor map serves Host and CUDA. Only the field
    // storage and kernel launch differ; no whole-state Host staging occurs.
    return compute_backend->execute_coordinate_seam_exchange(
        accesses, amr_ctrl.tree->GetActiveBlocks(), plans.coordinate_seam,
        requested, version, token);
}

/** Wait for and validate a device boundary publication. */
void DriverRuntime::complete_device_boundary(StateSlot slot)
{
    if (geometry_semantics_ == GridMetrics::GeometrySemantics::AxisymmetricRz)
        throw std::logic_error("Native RZ EOS boundary acceptance is unavailable on Device");
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
    auto& stamp = user_boundary_stamps_[static_cast<std::size_t>(slot)];
    const bool same_context = !bc_handler.has_user()
        || (stamp.epoch == stage_handles.front().epoch
            && stamp.revision == bc_handler.stage_revision());
    if (!needs_device_ghosts && same_context) return;
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
    // Publish the context stamp only after every boundary/exchange and the
    // residency transaction completed. Failed work cannot become a cache hit.
    if (bc_handler.has_user())
        stamp = {stage_handles.front().epoch, bc_handler.stage_revision()};
}

/** Fill missing host or device ghosts before a stage reads the state. */
void DriverRuntime::ensure_fluid_ghosts(StateSlot slot)
{
    const auto member = TimeIntegration::hydro_boundary_state_member(slot);
    if (compute_backend) { complete_device_boundary(slot); return; }
    // Freeze the actual domain/ledger/BC time before any fallible ghost writes.
    // Existing pre-initialization callers retain their interior-only behavior;
    // native candidates must have their real EOS and staged ledger available.
    std::optional<StageExecutionContext> context;
    state::StateVersion version{};
    if (residency_ledger && !stage_handles.empty()) {
        context.emplace(stage_context());
        version = residency_ledger->inspect({stage_handles.front(), slot}).interior.version;
    } else if (geometry_semantics_ == GridMetrics::GeometrySemantics::AxisymmetricRz) {
        throw std::logic_error("Native RZ boundary acceptance requires initialized residency");
    }
    for (int id:amr_ctrl.tree->GetActiveBlocks())
        (void)bc_handler.logical_plan(amr_ctrl.pool->GetBlock(id).grid);
    TimeIntegration::apply_domain_boundary(amr_ctrl, bc_handler, member);
    amr_ctrl.ghost_exchange.ExecuteExchange(amr_ctrl.pool, amr_ctrl.tree,
                                            config.grid.dim, member, stage_handles,
                                            geometry_semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz
                                                ? amr::CoordinateSeamGeometry::RzAxisymmetric
                                                : amr::CoordinateSeamGeometry::ExistingChart,
                {config.numerics.sml_rho,config.numerics.min_eint,config.numerics.max_eint});
    if (context) {
        (void)arch::scheduler::complete_boundary(*context, stage_handles, slot, version,
            [](StateSlot, arch::state::StateVersion, arch::state::CompletionToken token) { return token; });
    }
}

/** Download accepted resident state only when host output needs it. */
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
