/**
 * @file DriverBoundary.cpp
 * @brief Apply boundary and ghost operations at the required stage state generation.
 *
 * Workflow:
 * 1. Select the actual Current/Next/Scratch slot and freeze its domain context.
 * 2. Apply physical boundaries and domain exchange in the existing order.
 * 3. Record only successfully completed Host or Device boundary bindings.
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
#include "driver/schedule/DriverControl.h"
#include "physics/boundary/PhysicalBoundaryHandler.h"
#include "numerics/integrator/TimeIntegratorHelper.h"

namespace arch::driver {
using scheduler::StageExecutionContext;
using state::ExecutionSide;
using state::StateResidencyLedger;
using state::StateSlot;
using topology::LogicalBlockIdentity;
using topology::TopologyObservation;
state::CompletionToken DriverRuntime::execute_device_boundary(StateSlot requested, state::StateVersion version, state::CompletionToken token)
{
    auto& accesses = boundary_accesses;
    std::exception_ptr failure;
    try {
        accesses.clear();
        accesses.reserve(stage_handles.size());
        for (std::size_t index = 0; index < stage_handles.size(); ++index)
            accesses.push_back(backend_access(index, requested));
    } catch (...) { failure = std::current_exception(); }
    if (amr_ctrl.tree->PartitionSize() > 1)
        amr::GhostExchange::CompleteDistributedPhase(failure);
    else if (failure) std::rethrow_exception(failure);
    return execute_device_boundary_domain(accesses, stage_handles, requested,
        version, token, *residency_ledger);
}

/** Reuse the ordered recipe over the caller's exact backend storage domain. */
state::CompletionToken DriverRuntime::execute_device_boundary_domain(
    std::span<const backend::BackendStateAccess> accesses,
    std::span<const amr::BlockHandle> handles, StateSlot requested,
    state::StateVersion version, state::CompletionToken token, StateResidencyLedger& ledger,
    backend::BackendTopologyStoreTransaction* staged)
{
    if (amr_ctrl.tree->PartitionSize() > 1) {
        amr::GhostExchange::DistributedStorage storage;
        FluidState amr::Block::* member = nullptr;
        std::exception_ptr failure;
        try {
            if (!state::is_valid(version) || !state::is_complete(token)
                || handles.empty() || accesses.size() != handles.size())
                throw std::logic_error("distributed Device boundary identity is invalid");
            if (bc_handler.has_user())
                throw std::logic_error("distributed Device user boundary is not yet available");
            for (std::size_t i = 0; i < handles.size(); ++i) {
                if (accesses[i].block != handles[i] || accesses[i].slot != requested
                    || !(staged ? compute_backend->contains(*staged, accesses[i])
                         : compute_backend->contains(accesses[i])))
                    throw std::logic_error("distributed Device boundary storage is stale");
                ledger.require_ghost_publication(
                    {handles[i], requested}, ExecutionSide::Device, version, token);
            }
            switch (requested) {
            case StateSlot::Current: member = &amr::Block::fluid_state; break;
            case StateSlot::Next: member = &amr::Block::state_next; break;
            case StateSlot::Scratch: member = &amr::Block::state_scratch; break;
            default: throw std::invalid_argument("invalid distributed Device boundary slot");
            }
            storage.source_version = version.value;
            storage.begin = [&] {
                // Called only after every rank has validated topology/layout.
                // A failed sweep cannot leave formerly cached ghosts readable.
                for (const auto handle : handles)
                    ledger.invalidate_ghost({handle, requested}, ExecutionSide::Device);
                const auto completed = staged
                    ? compute_backend->execute_physical_boundary_batch(*staged, accesses, version, token)
                    : compute_backend->execute_physical_boundary_batch(accesses, version, token);
                if (completed != token)
                    throw std::logic_error("distributed Device physical boundary is incomplete");
            };
            storage.gather = [&](auto slices, auto packet) {
                if (staged) compute_backend->gather_exchange_packet(*staged, accesses, slices, packet);
                else compute_backend->gather_exchange_packet(accesses, slices, packet);
            };
            storage.scatter = [&](auto slices, auto packet) {
                if (staged) compute_backend->scatter_exchange_packet(*staged, accesses, slices, packet);
                else compute_backend->scatter_exchange_packet(accesses, slices, packet);
            };
            storage.gather_coarse_fine = [&](const auto& cells, auto packet) {
                if (staged) compute_backend->gather_coarse_fine_packet(*staged, accesses, cells, packet);
                else compute_backend->gather_coarse_fine_packet(accesses, cells, packet);
            };
        } catch (...) { failure = std::current_exception(); }
        amr::GhostExchange::CompleteDistributedPhase(failure);
        amr_ctrl.ghost_exchange.ExecuteDistributedExchange(
            amr_ctrl.pool, amr_ctrl.tree, config.grid.dim, member, handles, &storage);
        return token;
    }
    if (staged) throw std::logic_error("staged Runtime domain requires distributed topology");
    if (accesses.size() != handles.size())
        throw std::logic_error("Device boundary access/handle extent mismatch");
    for (std::size_t index = 0; index < accesses.size(); ++index)
        if (accesses[index].block != handles[index] || accesses[index].slot != requested)
            throw std::logic_error("Device boundary access/handle order or selected slot changed");
    const auto& plans = amr_ctrl.ghost_exchange.GetPlans(
        amr_ctrl.pool, amr_ctrl.tree, config.grid.dim, handles);
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
        for(std::size_t group=0;group<plans.same_level_corners.size();++group) {
            auto& level_accesses=boundary_level_accesses;level_accesses.clear();
            for(const auto index:plans.level_indices[group])level_accesses.push_back(accesses[index]);
            if(compute_backend->execute_same_level_exchange(level_accesses,
                plans.same_level_corners[group],requested,version,token)!=token)
                throw std::logic_error("Device corner completion returned incomplete work");
        }
        if (plans.coordinate_seam.transfers.empty()) return coarse_fine_completed;
        // The original physical donor map serves Host and CUDA; only storage
        // and kernels differ, with no whole-state Host staging here.
        return compute_backend->execute_coordinate_seam_exchange(
                accesses, amr_ctrl.tree->GetActiveBlocks(), plans.coordinate_seam,
                requested, version, token);
    };
    (void)compute_backend->execute_physical_boundary_batch(accesses, version, token);

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
    const bool distributed = amr_ctrl.tree->PartitionSize() > 1;
    state::StateVersion version{};
    std::array<std::uint64_t, 3> identity{};
    const auto prepare = [&] {
        if (stage_handles.empty())
            throw std::logic_error("CUDA boundary requires active blocks");
        if (slot != StateSlot::Current && slot != StateSlot::Next && slot != StateSlot::Scratch)
            throw std::invalid_argument("invalid CUDA boundary slot");
        version = residency_ledger->inspect({stage_handles.front(), slot}).interior.version;
        identity = {static_cast<std::uint64_t>(slot), version.value, stage_handles.front().epoch.value};
        bool needed = false;
        for (const auto handle : stage_handles) {
            residency_ledger->require_readable({handle, slot},
                {ExecutionSide::Device, version, true, false});
            const auto coherence = residency_ledger->inspect({handle, slot});
            needed |= !state::side_can_read(coherence.ghost.residency, ExecutionSide::Device)
                || coherence.ghost.version != version || coherence.ghost_source_version != version
                || !state::is_complete(coherence.ghost.completion)
                || coherence.ghost.pending_transfer != state::PendingTransferPhase::None;
        }
        const auto& stamp = user_boundary_stamps_[static_cast<std::size_t>(slot)];
        return needed || (bc_handler.has_user()
            && (stamp.epoch != stage_handles.front().epoch || stamp.revision != bc_handler.stage_revision()));
    };
    // Preserve synchronized Host ghosts on cache hits; every rank joins a
    // refresh requested by any peer. Local validation failures are collective.
    if (!(distributed ? amr::GhostExchange::PrepareDistributedBoundary(prepare, identity) : prepare()))
        return;
    auto& stamp = user_boundary_stamps_[static_cast<std::size_t>(slot)];
    const auto before = compute_backend->counters();
    StageExecutionContext context{
        ExecutionSide::Device, *residency_ledger, scheduler_clock};
    if (distributed)
        context.hydro_rank_completion = &amr::GhostExchange::CompleteDistributedPhase;
    (void)arch::scheduler::complete_boundary(
        context, stage_handles, slot, version,
        [&](StateSlot requested, arch::state::StateVersion version,
            arch::state::CompletionToken token) {
            return execute_device_boundary(requested, version, token);
        });
    std::exception_ptr trace_failure;
    try {
        trace_backend_operation(
            arch::backend::BackendOperation::PhysicalBoundary, slot, before);
    } catch (...) { trace_failure = std::current_exception(); }
    if (distributed) {
        try { amr::GhostExchange::CompleteDistributedPhase(trace_failure); }
        catch (...) {
            for (const auto handle : stage_handles)
                residency_ledger->invalidate_ghost({handle, slot}, ExecutionSide::Device);
            throw;
        }
    } else if (trace_failure) std::rethrow_exception(trace_failure);
    // Publish the context stamp only after every boundary/exchange and the
    // residency transaction completed. Failed work cannot become a cache hit.
    if (bc_handler.has_user())
        stamp = {stage_handles.front().epoch, bc_handler.stage_revision()};
}

/** Fill missing host or device ghosts before a stage reads the state. */
void DriverRuntime::ensure_fluid_ghosts(StateSlot slot)
{
    if (compute_backend) { complete_device_boundary(slot); return; }
    std::optional<StageExecutionContext> context;
    std::optional<scheduler::ScopedStageBinding> distributed_binding;
    state::StateVersion version{};
    FluidState amr::Block::* member=nullptr;
    std::exception_ptr failure;
    try {
        member=TimeIntegration::hydro_boundary_state_member(slot);
        if(residency_ledger&&!stage_handles.empty()) {
            context.emplace(stage_context());
            version = residency_ledger->inspect({stage_handles.front(), slot}).interior.version;
            if(context->hydro_rank_completion&&!scheduler::distributed_hydro_bound())
                distributed_binding.emplace(*context,stage_handles);
        }
        for(int id:amr_ctrl.tree->GetActiveBlocks())
            (void)bc_handler.logical_plan(amr_ctrl.pool->GetBlock(id).grid);
    } catch(...) {failure=std::current_exception();}
    if(amr_ctrl.tree->PartitionSize()>1&&residency_ledger&&distributed_hydro_topology())
        amr_ctrl.CompleteDistributedHydroPhase(failure);
    else if(failure)std::rethrow_exception(failure);
    TimeIntegration::synchronize_domain_boundary(amr_ctrl,bc_handler,member,stage_handles);
    if(context)
        (void)scheduler::complete_boundary(*context, stage_handles, slot, version,
            [](StateSlot,state::StateVersion,state::CompletionToken token){return token;});

}

/** Download accepted resident state only when host output needs it. */
void DriverRuntime::materialize_current_for_host()
{
    // A call from inside one rank's Hydro callback is a local contract error;
    // the active macro owns its collective rejection. Never enter new MPI work.
    if(hydro_attempt_)throw std::logic_error("Cannot expose unaccepted Hydro state");
    const auto complete = [&](auto&& work) {
        std::exception_ptr failure;
        try { work(); } catch (...) { failure = std::current_exception(); }
        if (amr_ctrl.tree->PartitionSize() > 1) amr_ctrl.CompleteDistributedHydroPhase(failure);
        else if (failure) std::rethrow_exception(failure);
    };
    ensure_fluid_ghosts(StateSlot::Current);
    if (!compute_backend) return;
    complete([&] {
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
    });
}
} // namespace arch::driver
