/**
 * @file DriverBoundary.cpp
 * @brief Apply boundary and ghost operations at the required stage state generation.
 *
 * Workflow:
 * 1. Select the actual Current/Next/Scratch slot and freeze its domain context.
 * 2. Native Device borrows the original resident savepoint, builtin seed,
 *    two identical domain exchanges, compact ordered surfaces and final axis.
 *    Existing geometry retains its original physical/single-exchange path.
 * 3. Validate native-RZ closure with the real EOS before publishing ghosts.
 * 4. Record only successfully completed BC bindings. Completed Native ghosts
 *    may reuse field work, retaining their actual Host or Device EOS gate.
 */

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "amr/AMRControl.h"
#include "driver/DriverUtils.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/runtime/RuntimeStateTransaction.h"
#include "driver/schedule/DriverControl.h"
#include "numerics/integrator/TimeIntegratorHelper.h"

namespace arch::driver {
using scheduler::StageExecutionContext;
using state::ExecutionSide;
using state::StateResidencyLedger;
using state::StateSlot;
using topology::LogicalBlockIdentity;
using topology::TopologyObservation;
/** Metadata for one genuine completed native Host Current boundary. Empty
 * opaque frames authenticate BC binding, root/grid and all seven allocations;
 * no conserved array is copied and no EOS/physics acceptance is cached.
 */
struct DriverRuntime::NativeHostCurrentBoundaryStamp {
    amr::TopologyEpoch epoch;
    std::uint64_t revision;
    std::uint64_t boundary_binding;
    state::StateVersion version;
    state::CompletionToken ghost_completion;
    const StateResidencyLedger* ledger;
    const void* pool;
    const void* tree;
    NativeRzEosBindingWitness eos;
    std::vector<BCHandler::HydroInputFrame> frames;
};
/** Execute the original resident boundary owner for one actual slot/version.
 * Workflow:
 * 1. Borrow the existing accesses and original cached exchange plan. Native RZ
 *    authenticates its already armed Runtime transaction and explicit RZ chart.
 * 2. Seed original builtin ghosts, then complete the first domain exchange.
 * 3. Prepare ALL compact Native surfaces against their actual Runtime Grids;
 *    validate the complete domain before the first fallible existing scatter.
 * 4. Repeat the SAME exchange and finish original cached signed-axis copies.
 *    The calling scheduler retains final actual EOS and GhostValid publication.
 * Existing geometry retains builtin/user followed by its single exchange.
 */
state::CompletionToken DriverRuntime::execute_device_boundary(StateSlot requested, state::StateVersion version, state::CompletionToken token)
{
    const bool native = geometry_semantics_ == GridMetrics::GeometrySemantics::AxisymmetricRz;
    auto* const transaction = native ? active_runtime_state_transaction() : nullptr;
    if (native) {
        if (!transaction)
            throw std::logic_error("Native Device boundary requires its actual Runtime state transaction");
        transaction->validate_storage();
    }
    auto& accesses = boundary_accesses;
    accesses.clear();
    accesses.reserve(stage_handles.size());
    for (std::size_t index = 0; index < stage_handles.size(); ++index) {
        const auto access = backend_access(index, requested);
        accesses.push_back(access);
    }
    const auto& plans = amr_ctrl.ghost_exchange.GetPlans(
        amr_ctrl.pool, amr_ctrl.tree, config.grid.dim, stage_handles,
        native ? amr::CoordinateSeamGeometry::RzAxisymmetric
               : amr::CoordinateSeamGeometry::ExistingChart);
    /** Reuse the same actual same-level, coarse-fine and seam operations. */
    const auto exchange = [&]() -> state::CompletionToken {
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
        // The original physical donor map serves Host and CUDA; only storage
        // and kernels differ, with no whole-state Host staging here.
        return compute_backend->execute_coordinate_seam_exchange(
            accesses, amr_ctrl.tree->GetActiveBlocks(), plans.coordinate_seam,
            requested, version, token);
    };
    const auto seeded = compute_backend->execute_physical_boundary_batch(accesses, version, token);
    if (native) {
        if (seeded != token || exchange() != token)
            throw std::logic_error("Native Device seed/exchange returned incomplete work");
        const auto& active = amr_ctrl.tree->GetActiveBlocks();
        if (active.size() != accesses.size())
            throw std::logic_error("Native Device boundary lost the actual Runtime domain");
        std::vector<const Grid*> grids;
        std::vector<BCHandler::NativeDeviceCandidate> candidates;
        grids.reserve(active.size()); candidates.reserve(active.size());
        for (std::size_t index = 0; index < active.size(); ++index)
            grids.push_back(&amr_ctrl.pool->GetBlock(active[index]).grid);
        for (std::size_t index = 0; index < accesses.size(); ++index)
            candidates.push_back(bc_handler.prepare_native_device(
                *compute_backend, accesses[index], *grids[index]));
        // The one actual macro owner rechecks domain/storage; the original
        // candidate validates exact BC binding/root/Grid before ALL scatters.
        transaction->validate_storage();
        for (std::size_t index = 0; index < accesses.size(); ++index)
            bc_handler.validate_native_device_candidate(candidates[index],
                *compute_backend, accesses[index], *grids[index]);
        for (std::size_t index = 0; index < accesses.size(); ++index)
            bc_handler.publish_native_device(std::move(candidates[index]),
                *compute_backend, accesses[index], *grids[index]);
        if (exchange() != token)
            throw std::logic_error("Native Device final exchange returned incomplete work");
        return compute_backend->execute_native_axis_boundary_batch(accesses, version, token);
    }
    // Preserve the original ExistingChart user-before-single-exchange path.
    if (bc_handler.has_user()) {
        const auto& active = amr_ctrl.tree->GetActiveBlocks();
        for (std::size_t b = 0; b < accesses.size(); ++b)
            bc_handler.apply_device(*compute_backend, accesses[b], amr_ctrl.pool->GetBlock(active[b]).grid);
    }
    return exchange();
}

/** Wait for and validate a device boundary publication. */
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
    auto& stamp = user_boundary_stamps_[static_cast<std::size_t>(slot)];
    if (geometry_semantics_ == GridMetrics::GeometrySemantics::AxisymmetricRz) {
        const auto boundary = bc_handler.snapshot_stage_context();
        const auto eos_binding = native_rz_eos_binding_;
        if (!eos_binding || !native_rz_eos_binding_matches(*eos_binding)
            || !bc_handler.native_point_eos_bound())
            throw std::logic_error("Native Device boundary lost its real EOS/BC binding");
        const bool same_context = stamp.epoch == stage_handles.front().epoch
            && stamp.revision == boundary.revision()
            && stamp.boundary_binding == boundary.binding_revision();
        /** Gate completed ghosts or refresh through the same actual transaction.
         * This never creates a nested savepoint or replaces the bound stage.
         * A reused boundary still evaluates the genuine resident EOS each time;
         * only a fresh completed boundary obtains a scheduler completion token.
         */
        const auto complete_native = [&](StageExecutionContext& context,
            RuntimeStateTransaction* transaction) {
            if (transaction) transaction->validate_storage();
            if (context.side != ExecutionSide::Device
                || &context.ledger != residency_ledger.get()
                || &context.clock != &scheduler_clock
                || !context.post_boundary_acceptance
                || !bc_handler.stage_context_matches(boundary))
                throw std::logic_error("Native Device boundary requires its real bound stage/EOS frame");
            if ((needs_device_ghosts || !same_context) && !transaction)
                throw std::logic_error("Native Device boundary refresh requires its actual Runtime state transaction");
            const auto before = compute_backend->counters();
            if (!needs_device_ghosts && same_context) {
                std::vector<state::SlotCoherence> accepted;
                accepted.reserve(stage_handles.size());
                for (const auto handle : stage_handles)
                    accepted.push_back(residency_ledger->inspect({handle, slot}));
                const auto token_before = scheduler_clock.last_token();
                const auto version_before = scheduler_clock.last_version();
                context.post_boundary_acceptance(context, slot, version);
                if (scheduler_clock.last_token() != token_before
                    || scheduler_clock.last_version() != version_before)
                    throw std::logic_error("Native Device reused EOS gate changed its publication");
                /** Compare original selected-slot metadata without another lease. */
                const auto same_region = [](const state::RegionCoherence& a,
                    const state::RegionCoherence& b) {
                    return a.residency == b.residency && a.version == b.version
                        && a.completion == b.completion && a.pending_transfer == b.pending_transfer;
                };
                for (std::size_t index = 0; index < stage_handles.size(); ++index) {
                    const auto after = residency_ledger->inspect({stage_handles[index], slot});
                    const auto& prior = accepted[index];
                    if (!same_region(after.interior, prior.interior)
                        || !same_region(after.ghost, prior.ghost)
                        || after.ghost_source_version != prior.ghost_source_version)
                        throw std::logic_error("Native Device reused EOS gate changed selected-slot residency");
                }
            } else {
                (void)arch::scheduler::complete_boundary(context, stage_handles, slot, version,
                    [&](StateSlot requested, state::StateVersion input,
                        state::CompletionToken token) {
                        return execute_device_boundary(requested, input, token);
                    });
            }
            if (!native_rz_eos_binding_matches(*eos_binding)
                || !bc_handler.stage_context_matches(boundary))
                throw std::logic_error("Native Device completed boundary changed its EOS/BC binding");
            if (transaction) transaction->validate_storage();
            trace_backend_operation(backend::BackendOperation::PhysicalBoundary, slot, before);
            stamp = {stage_handles.front().epoch, boundary.revision(), boundary.binding_revision()};
        };
        if (auto* transaction = active_runtime_state_transaction()) {
            // The existing macro owns the real context and all three resident
            // slots. Capacity was prepared before this savepoint was armed.
            auto& context = scheduler::current_stage_binding().context;
            complete_native(context, transaction);
            return;
        }
        auto context = stage_context();
        context.step_start_time = boundary.time();
        context.step_dt = 0.; // Actual nondynamical refresh, never a fabricated timestep.
        context.boundary_start_time = boundary.time();
        scheduler::ScopedStageBinding binding(context, stage_handles);
        if (!needs_device_ghosts && same_context) {
            // The actual backend EOS reads only completed resident fields.
            // A genuine cache hit needs neither capacity work nor a D2D backup.
            complete_native(context, nullptr);
            return;
        }
        // The original constructor alone prepares physical controls after
        // complete domain preflight and before freezing/arming its savepoint.
        RuntimeStateTransaction transaction(*this, context, nullptr);
        complete_native(context, &transaction);
        transaction.commit();
        return;
    }
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
    // Repeated Current observations are read-only owners. Re-filling already
    // accepted ghosts would mint a new completion token without new physics.
    // Reuse is limited to quiescent native Host Hydro; active macro stages,
    // other slots/purposes and all Existing geometry retain their original path.
    const auto boundary = bc_handler.snapshot_stage_context();
    const bool native_current = context && slot == StateSlot::Current
        && !runtime_state_transaction_
        && !native_macro_retry_attempt_
        && geometry_semantics_ == GridMetrics::GeometrySemantics::AxisymmetricRz
        && bc_handler.native_point_eos_bound()
        && boundary.purpose() == arch::boundary::BoundaryPurpose::Hydro;
    if (native_current && native_host_current_boundary_stamp_) {
        // Retain the immutable owner even if a faulty EOS tries to rebind the
        // Runtime during its call; the frame check will reject that rebind.
        const auto accepted_stamp = native_host_current_boundary_stamp_;
        const auto& stamp = *accepted_stamp;
        const auto& active = amr_ctrl.tree->GetActiveBlocks();
        bool same = stamp.epoch == residency_ledger->active_epoch()
            && stamp.revision == boundary.revision()
            && stamp.boundary_binding == boundary.binding_revision()
            && stamp.version == version
            && stamp.ledger == residency_ledger.get()
            && stamp.pool == amr_ctrl.pool.get()
            && stamp.tree == amr_ctrl.tree.get()
            && native_rz_eos_binding_matches(stamp.eos)
            && stamp.frames.size() == stage_handles.size()
            && active.size() == stage_handles.size();
        for (std::size_t index = 0; same && index < stage_handles.size(); ++index) {
            const auto handle = stage_handles[index];
            const auto coherence = residency_ledger->inspect({handle, slot});
            same = handle.epoch == stamp.epoch
                && coherence.interior.version == version
                && coherence.ghost.version == version
                && coherence.ghost_source_version == version
                && state::side_can_read(coherence.interior.residency, ExecutionSide::Host)
                && state::side_can_read(coherence.ghost.residency, ExecutionSide::Host)
                && state::is_complete(coherence.interior.completion)
                && state::is_complete(coherence.ghost.completion)
                && coherence.ghost.completion == stamp.ghost_completion
                && coherence.interior.pending_transfer == state::PendingTransferPhase::None
                && coherence.ghost.pending_transfer == state::PendingTransferPhase::None;
        }
        if (same) {
            // A stale opaque lease is a fatal ownership error, not a cache miss
            // that silently repairs a changed allocation or boundary binding.
            for (std::size_t index = 0; index < active.size(); ++index) {
                const auto& block = amr_ctrl.pool->GetBlock(active[index]);
                bc_handler.require_hydro_input_frame(stamp.frames[index],
                    block.fluid_state, block.grid, boundary.time());
            }
            const auto ledger_before = residency_ledger->snapshot_metadata(arch::state::ExecutionSide::Host);
            const auto token_before = scheduler_clock.last_token();
            const auto version_before = scheduler_clock.last_version();
            // Evaluate every actual completed cell using the freshly bound real
            // EOS callback. A corrupt ghost or rebind remains a hard failure.
            context->post_boundary_acceptance(*context, slot, version);
            if (!residency_ledger->metadata_snapshot_matches(ledger_before)
                || scheduler_clock.last_token() != token_before
                || scheduler_clock.last_version() != version_before
                || !native_rz_eos_binding_matches(stamp.eos)
                || !bc_handler.stage_context_matches(boundary))
                throw std::logic_error("Native RZ reused boundary changed its publication owner");
            for (std::size_t index = 0; index < active.size(); ++index) {
                const auto& block = amr_ctrl.pool->GetBlock(active[index]);
                bc_handler.require_hydro_input_frame(stamp.frames[index],
                    block.fluid_state, block.grid, boundary.time());
            }
            return;
        }
    }
    // Failed refreshes cannot leave a reusable previous completion identity.
    if (native_current) native_host_current_boundary_stamp_.reset();
    // Allocate metadata before any ghost write/publication. The same empty
    // frames are checked after the real EOS gate; final installation is noexcept.
    std::shared_ptr<NativeHostCurrentBoundaryStamp> prepared_stamp;
    if (native_current) {
        if (!native_rz_eos_binding_ || !native_rz_eos_binding_matches(*native_rz_eos_binding_))
            throw std::logic_error("Native RZ boundary lost its binding identity");
        prepared_stamp = std::make_shared<NativeHostCurrentBoundaryStamp>(
            NativeHostCurrentBoundaryStamp{residency_ledger->active_epoch(),
                boundary.revision(), boundary.binding_revision(), version, {},
                residency_ledger.get(), amr_ctrl.pool.get(), amr_ctrl.tree.get(),
                *native_rz_eos_binding_, {}});
        const auto& active = amr_ctrl.tree->GetActiveBlocks();
        prepared_stamp->frames.reserve(active.size());
        for (const auto id : active) {
            const auto& block = amr_ctrl.pool->GetBlock(id);
            prepared_stamp->frames.push_back(bc_handler.capture_hydro_input_frame(
                block.fluid_state, block.grid, boundary.time()));
        }
        const auto real_acceptance = context->post_boundary_acceptance;
        context->post_boundary_acceptance = [this, real_acceptance, prepared_stamp, boundary](
            const StageExecutionContext& actual, StateSlot requested, state::StateVersion input) {
            real_acceptance(actual, requested, input);
            if (!native_rz_eos_binding_matches(prepared_stamp->eos)
                || !bc_handler.stage_context_matches(boundary)
                || residency_ledger.get() != prepared_stamp->ledger
                || amr_ctrl.pool.get() != prepared_stamp->pool
                || amr_ctrl.tree.get() != prepared_stamp->tree)
                throw std::logic_error("Native RZ completed boundary lost its binding identity");
            const auto& completed_active = amr_ctrl.tree->GetActiveBlocks();
            if (completed_active.size() != prepared_stamp->frames.size())
                throw std::logic_error("Native RZ boundary changed its domain extent");
            for (std::size_t index = 0; index < completed_active.size(); ++index) {
                const auto& block = amr_ctrl.pool->GetBlock(completed_active[index]);
                bc_handler.require_hydro_input_frame(prepared_stamp->frames[index],
                    block.fluid_state, block.grid, boundary.time());
            }
        };
    }
    for (int id:amr_ctrl.tree->GetActiveBlocks())
        (void)bc_handler.logical_plan(amr_ctrl.pool->GetBlock(id).grid);
    TimeIntegration::synchronize_domain_boundary(amr_ctrl, bc_handler,
        member, stage_handles, geometry_semantics_,
        {config.numerics.sml_rho,config.numerics.min_eint,config.numerics.max_eint});
    if (context) {
        const auto completion = arch::scheduler::complete_boundary(*context, stage_handles, slot, version,
            [](StateSlot, arch::state::StateVersion, arch::state::CompletionToken token) { return token; });
        if (prepared_stamp) prepared_stamp->ghost_completion = completion;
    }
    if (native_current) {
        // Every fallible frame/EOS check ran before ghost publication. This
        // final metadata installation cannot turn accepted work into a failure.
        native_host_current_boundary_stamp_ = std::move(prepared_stamp);
    }
}

/** Download accepted resident state only when host output needs it. */
void DriverRuntime::materialize_current_for_host()
{
    if(runtime_state_transaction_&&compute_backend)
        throw std::logic_error("Active resident macro excludes Host materialization");
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
