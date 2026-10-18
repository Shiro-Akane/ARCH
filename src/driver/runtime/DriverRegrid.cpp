/**
 * @file DriverRegrid.cpp
 * @brief Rebuild AMR topology and transfer state while invalidating stale views.
 *
 * Workflow: prepare candidate topology and migration, complete its real boundary
 * exchange, publish the completed ledger and retire old Host or Device storage.
 * Failure restores original Host source arrays or discards the private Device store.
 */
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "amr/AMRControl.h"
#include "amr/topology/TopologyTransaction.h"
#include "driver/DriverUtils.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "numerics/integrator/TimeIntegratorHelper.h"
#include "physics/boundary/PhysicalBoundaryHandler.h"

namespace arch::driver {
using scheduler::StageExecutionContext;
using state::ExecutionSide;
using state::StateResidencyLedger;
using state::StateSlot;
using topology::LogicalBlockIdentity;
using topology::TopologyObservation;
namespace {
using RegridField=std::vector<double> FluidState::*;
constexpr std::array<RegridField,7> regrid_fields{
    &FluidState::rho,&FluidState::mom_u,&FluidState::mom_v,&FluidState::mom_w,
    &FluidState::eng,&FluidState::enuc_rate,&FluidState::mass_fractions};

/** One original Current copy and seven actual leases, preserving borrowed spans.
 * BC/exchange changes Current arrays and replaces diffusion controls, but does
 * not write capture pointees. Restore these values/pointers in place; allocation
 * drift is a fatal ownership violation rather than a successful parent veto.
 */
struct RegridSourceBackup {
    int pool_index;
    amr::Block* block;
    FluidState values;
    Grid original_grid; // Small value metadata; source array leases stay fixed.
    std::array<const double*,7> pointers{};
    std::array<std::size_t,7> sizes{};
    RegridSourceBackup(int id,amr::Block& live)
        :pool_index(id),block(&live),values(live.fluid_state),original_grid(live.grid) {
        for(std::size_t f=0;f<regrid_fields.size();++f) {
            const auto& field=live.fluid_state.*regrid_fields[f];
            pointers[f]=field.data();sizes[f]=field.size();
        }
    }
    /** Allocation-free rejection, with original array addresses intact. */
    void restore(amr::Block& live) noexcept {
        if(&live!=block)std::terminate();
        for(std::size_t f=0;f<regrid_fields.size();++f) {
            const auto& field=live.fluid_state.*regrid_fields[f];
            if(field.data()!=pointers[f]||field.size()!=sizes[f])std::terminate();
        }
        for(const auto field:regrid_fields)
            std::copy((values.*field).begin(),(values.*field).end(),
                (live.fluid_state.*field).begin());
        live.fluid_state.n_species_=values.n_species_;
        live.fluid_state.block_total_size_=values.block_total_size_;
        std::swap(live.fluid_state.stage_repairs,values.stage_repairs);
        live.fluid_state.diffusion_boundary=values.diffusion_boundary;
        live.fluid_state.boundary_flux_capture=values.boundary_flux_capture;
        // Swap the actual source descriptors/provenance without allocation.
        // A failed callback cannot leave a different root or periodic alias
        // associated with the restored physical arrays and boundary frame.
        static_assert(std::is_nothrow_swappable_v<Grid>);
        std::swap(live.grid,original_grid);
    }
};
} // namespace

/** Evaluate the original Device source flags before topology preparation.
 * Workflow: select indicators -> complete actual Current ghosts -> evaluate
 * original summaries/JENS -> reject finest deficits -> publish only flags.
 */
void DriverRuntime::evaluate_device_regrid_indicators(bool jeans_repair_only)
{
    const auto old_active=amr_ctrl.tree->GetActiveBlocks();
    (void)amr::indicator::make_selection(
        config.amr,config.grid.dim,amr_ctrl.tree->RefinementSpecies());
    std::vector<double> errors;
    if(!jeans_repair_only) {
        complete_device_boundary(StateSlot::Current);
        std::vector<arch::backend::BackendStateAccess> accesses;
        accesses.reserve(stage_handles.size());
        for(std::size_t index=0;index<stage_handles.size();++index)
            accesses.push_back(backend_access(index,StateSlot::Current));
        errors=compute_backend->evaluate_refinement_indicators(
            accesses,config.amr,config.numerics.sml_rho,
            amr_ctrl.tree->RefinementSpecies());
        if(errors.size()!=old_active.size())
            throw std::logic_error("backend AMR indicator count mismatch");
    }
    const auto minima=config.amr.refine_on_jeans
        ? evaluate_current_jeans_resolution() : std::vector<double>{};
    // Validate every finest-level deficit before changing any decision flag.
    if(config.amr.refine_on_jeans)
        for(std::size_t index=0;index<old_active.size();++index)
            if(minima[index]<config.amr.jeans_cells
                && amr_ctrl.pool->GetBlock(old_active[index]).level>=config.amr.lrefinemax)
                throw std::runtime_error("JENS remains underresolved at lrefinemax; increase allowed resolution.");
    for(std::size_t index=0;index<old_active.size();++index) {
        auto& block=amr_ctrl.pool->GetBlock(old_active[index]);
        block.refine_flag=jeans_repair_only?0:amr::indicator::refinement_flag(
            errors[index],block.level,config.amr.lrefinemin,config.amr.lrefinemax,
            config.amr.refine_threshold,config.amr.derefine_threshold);
        if(config.amr.refine_on_jeans && minima[index]<config.amr.jeans_cells)
            block.refine_flag=1;
    }
}

/** Stage one fresh topology transaction; ordinary/device arithmetic is shared. */
bool DriverRuntime::execute_regrid(bool jeans_repair_only)
{
    const auto make_regrid_ledger = [] (
        amr::TopologyEpoch epoch,
        std::span<const amr::BlockHandle> handles,
        arch::scheduler::PublicationWitness topology_witness) {
        auto ledger = std::make_unique<StateResidencyLedger>(epoch);
        for (const amr::BlockHandle handle : handles) {
            ledger->register_block(
                handle, topology_witness.version,
                topology_witness.completion);
        }
        return ledger;
    };
    topology_registry.validate_committed_snapshot(observe_topology());

    // Topology decisions consume compact device-computed indicators, not
    // a Host copy of every conserved/species field.
    if (!compute_backend) ensure_fluid_ghosts();
    const std::vector<int> old_active(
        amr_ctrl.tree->GetActiveBlocks().begin(),
        amr_ctrl.tree->GetActiveBlocks().end());
#if ARCH_CUDA_BUILD_ENABLED
    if (compute_backend) {
        if (!compute_backend->supports_dynamic_topology_store())
            throw std::logic_error(
                "selected backend cannot stage dynamic AMR topology");
        if (backend_storage.size() != old_active.size()
            || backend_storage.size() != stage_handles.size()) {
            throw std::logic_error(
                "device dynamic AMR source storage drifted");
        }
    }
#endif

    const auto evaluate_device_indicators = [&] {
        evaluate_device_regrid_indicators(jeans_repair_only);
    };
    const auto evaluate_device_parent = [&](const amr::Block& parent,std::span<const int> siblings) {
        return device_jeans_parent_resolved(parent,siblings);
    };
    auto prepared = compute_backend
        ? amr_ctrl.tree->PrepareRegrid(config, {}, {}, evaluate_device_indicators,
            jeans_repair_only, config.amr.refine_on_jeans
                ? std::function<bool(const amr::Block&,std::span<const int>)>(evaluate_device_parent)
                : std::function<bool(const amr::Block&,std::span<const int>)>{})
        : amr_ctrl.tree->PrepareRegrid(config, {}, {}, {}, jeans_repair_only);
    auto topology_candidate = topology_registry.stage_reconciliation(
        observe_blocks(prepared.proposed_active_blocks()));
    const auto& proposed = topology_candidate.reconciliation();
    if (proposed.topology_changed != prepared.topology_changed())
        throw std::logic_error(
            "staged AMR topology disagrees with identity reconciliation");

    if (!prepared.topology_changed()) {
        const auto reconciliation = topology_registry.commit_after_success(
            std::move(topology_candidate),
            [&](const auto&) { prepared.PublishNoChangeNoexcept(); });
        stage_handles = reconciliation.handles_in_observation_order;
        amr_ctrl.BindActiveHandles(stage_handles);
        return false;
    }

    if (stage_handles.empty())
        throw std::logic_error("AMR regrid has no source handles");
    if (next_amr_transaction_id
        == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("AMR transaction ID exhausted");
    const amr::AmrPlanScope scope{
        next_amr_transaction_id++, stage_handles.front().epoch,
        proposed.epoch};
    prepared.BuildMigrationPlans(
        stage_handles, proposed.handles_in_observation_order, scope);

    amr::TopologyTransaction transaction(
        scope.transaction_id, scope.from_epoch, scope.to_epoch);
    transaction.begin_migration();
    transaction.require_scope(prepared.prolongation_plan());
    transaction.require_scope(prepared.restriction_plan());
#if ARCH_CUDA_BUILD_ENABLED
    if (compute_backend) {
        struct DeviceRegridPublication {
            std::unique_ptr<StateResidencyLedger> ledger;
            std::vector<amr::BlockHandle> handles;
            std::vector<arch::backend::StorageGeneration> storage;
            std::vector<arch::backend::BackendTopologyBinding> bindings;
            std::unique_ptr<arch::backend::BackendTopologyStoreTransaction>
                store_transaction;
            arch::scheduler::PublicationWitness topology_witness{};
        } payload;
        bool store_published = false;
        try {
            payload.handles = proposed.handles_in_observation_order;
            if (payload.handles.empty()
                || payload.handles.size() != prepared.proposed_active_blocks().size())
                throw std::logic_error("CUDA AMR proposed topology is empty or mismatched");
            payload.topology_witness = scheduler_clock.next_publication();
            payload.storage.reserve(payload.handles.size());
            payload.bindings.reserve(payload.handles.size());
            for (std::size_t index = 0; index < payload.handles.size(); ++index) {
                const auto storage = storage_generation_issuer.issue();
                payload.storage.push_back(storage);
                auto& block=amr_ctrl.pool->GetBlock(prepared.proposed_active_blocks()[index]);
                // A new axis child borrows the same per-grid logical plan as
                // initial binding; the domain-wide plan describes outer walls.
                payload.bindings.push_back({&block, payload.handles[index], storage,
                    &bc_handler.logical_plan(block.grid)});
            }
            std::vector<arch::backend::BackendStateAccess> source_accesses;
            source_accesses.reserve(stage_handles.size());
            for (std::size_t index = 0; index < stage_handles.size(); ++index)
                source_accesses.push_back(backend_access(index, StateSlot::Current));

            // Activate only Host topology/neighbor metadata. Old accepted
            // device sources remain immutable throughout this transaction.
            prepared.ActivateForDeviceMigration();
            payload.store_transaction =
                compute_backend->begin_topology_store_transaction(scope, payload.bindings);
            const auto staged_flux_plan = amr::build_amr_flux_topology_plan(
                    *amr_ctrl.pool, amr_ctrl.tree->GetActiveBlocks(), payload.handles,
                    config.grid.dim, specs.count());
            const auto staged_reflux_plan =
                amr::build_amr_reflux_topology_plan(*amr_ctrl.pool, staged_flux_plan);
            compute_backend->stage_amr_flux_plan(
                *payload.store_transaction, staged_flux_plan, staged_reflux_plan);

            // CPU and CUDA migration share logical plans and mathematical
            // leaves; this route executes against private device storage.
            compute_backend->migrate_staged_current(*payload.store_transaction,
                source_accesses, prepared.prolongation_plan(), prepared.restriction_plan());
            const auto staged_same_level =
                amr_ctrl.ghost_exchange.BuildSameLevelPlans(amr_ctrl.pool, amr_ctrl.tree,
                    config.grid.dim, payload.handles);
            const auto staged_coarse_fine =
                amr_ctrl.ghost_exchange.BuildCoarseFinePlan(amr_ctrl.pool, amr_ctrl.tree,
                    config.grid.dim, payload.handles);
            const auto staged_coordinate_seam = amr::make_coordinate_seam_plan(
                amr_ctrl.pool, amr_ctrl.tree->GetActiveBlocks(),
                config.grid.dim);

                compute_backend->complete_staged_current_ghosts(
                    *payload.store_transaction, staged_same_level, staged_coarse_fine,
                    amr_ctrl.tree->GetActiveBlocks(), payload.handles,
                    &staged_coordinate_seam);
            
            prepared.CompleteDeviceMigration();

            // No field H2D transfer occurred: publish the actual authority of
            // these reconstructed fields. Host arrays are deliberately
            // stale and materialize only for an explicit Host consumer.

                payload.ledger = std::make_unique<StateResidencyLedger>(scope.to_epoch);
                for (const auto handle : payload.handles) {
                    payload.ledger->register_block(handle, payload.topology_witness.version,
                        payload.topology_witness.completion, ExecutionSide::Device);
                    payload.ledger->publish_ghost({handle, StateSlot::Current},
                        ExecutionSide::Device, payload.topology_witness.version,
                        payload.topology_witness.completion);
                }
            
            transaction.mark_ready();

            (void)topology_registry.commit_after_success(
                std::move(topology_candidate),
                [&](const auto& committed_topology) {
                    if (committed_topology.epoch != scope.to_epoch
                        || committed_topology.handles_in_observation_order != payload.handles)
                        throw std::logic_error("CUDA AMR publication scope drifted");
                    transaction.commit_after_success(
                        [&](const amr::AmrPlanScope&) { return std::move(payload); },
                        [&](const amr::AmrPlanScope&, DeviceRegridPublication& ready) {
                            // Last throwing action: backend publication
                            // records a checked retirement fence first.
                            compute_backend->publish_topology_store_transaction(
                                std::move(ready.store_transaction));
                            store_published = true;
                        },
                        [&](DeviceRegridPublication&& ready) noexcept {
                            prepared.PublishNoexcept();
                            static_assert(noexcept(stage_handles.swap(ready.handles)));
                            static_assert(noexcept(backend_storage.swap(ready.storage)));
                            static_assert(noexcept(residency_ledger.swap(ready.ledger)));
                            stage_handles.swap(ready.handles);
                            backend_storage.swap(ready.storage);
                            residency_ledger.swap(ready.ledger);
                            amr_ctrl.PublishActiveHandlesNoexcept(stage_handles);
                        });
                });
        } catch (...) {
            if (store_published) std::terminate();
            // Quiesce the unpublished namespace before topology rollback
            // can return new Host blocks to MemoryPool. No old fluid data
            // was overwritten, so field snapshots/restores are unnecessary.
            payload.store_transaction.reset();
            if (transaction.state() != amr::TopologyTransactionState::Committed)
                transaction.abort([&]() noexcept { prepared.AbortNoexcept(); });
            throw;
        }
        prepared.ReleaseRetiredNoexcept();
        return true;
    }
#endif

    try {
        prepared.ExecuteMigration();

        transaction.mark_ready();
    } catch(...) {
        transaction.abort([&]() noexcept {prepared.AbortNoexcept();});
        throw;
    }

    struct RegridPublication {
        std::unique_ptr<StateResidencyLedger> ledger;
        std::vector<amr::BlockHandle> handles;
        arch::scheduler::PublicationWitness topology_witness{};
        std::vector<RegridSourceBackup> source_backups;
        BCHandler::StageContextSnapshot boundary_context;
        explicit RegridPublication(const BCHandler& boundary)
            :boundary_context(boundary.snapshot_stage_context()) {}
    };
    static_assert(std::is_nothrow_swappable_v<FluidState>);

    std::unique_ptr<StateResidencyLedger> staged_ledger;
    std::vector<amr::BlockHandle> staged_handles;
    try {
        (void)topology_registry.commit_after_success(
                std::move(topology_candidate),
                [&](const auto& committed_topology) {
                    transaction.commit_after_success(
                        [&](const amr::AmrPlanScope& transaction_scope) {
                            if (transaction_scope != scope
                                || committed_topology.epoch
                                    != transaction_scope.to_epoch)
                                throw std::logic_error(
                                    "AMR publication scope drifted");
                            RegridPublication payload(bc_handler);
                            payload.handles = committed_topology
                                .handles_in_observation_order;
                            payload.topology_witness =
                                scheduler_clock.next_publication();
                            payload.ledger = make_regrid_ledger(
                                committed_topology.epoch, payload.handles,
                                payload.topology_witness);
                            payload.source_backups.reserve(
                                old_active.size());
                            for (const int pool_index : old_active) {
                                payload.source_backups.emplace_back(pool_index,
                                    amr_ctrl.pool->GetBlock(pool_index));
                            }
                            return payload;
                        },
                        [&](const amr::AmrPlanScope& transaction_scope,
                            RegridPublication& payload) {
                            const auto restore_source_states = [&]() noexcept {
                                for (auto& backup
                                     : payload.source_backups) {
                                    backup.restore(amr_ctrl.pool->GetBlock(backup.pool_index));
                                }
                                bc_handler.restore_stage_context_noexcept(payload.boundary_context);
                            };
                            try {
                                if (transaction_scope != scope)
                                    throw std::logic_error(
                                        "AMR finalizer scope drifted");
                                prepared.ActivateForFinalization();
                                StageExecutionContext staged_context{
                                    ExecutionSide::Host, *payload.ledger,
                                    scheduler_clock};
                                TimeIntegration::synchronize_domain_boundary(amr_ctrl, bc_handler,
                                    &amr::Block::fluid_state, payload.handles);

                                    (void)arch::scheduler::complete_boundary(
                                    staged_context, payload.handles,
                                    StateSlot::Current,
                                    payload.topology_witness.version,
                                    [](StateSlot,
                                       arch::state::StateVersion,
                                       arch::state::CompletionToken token) {
                                        return token;
                                    });
                                

                                for (const amr::BlockHandle handle
                                     : payload.handles) {
                                    payload.ledger->require_readable(
                                        {handle, StateSlot::Current},
                                        {ExecutionSide::Host,
                                         payload.topology_witness.version,
                                         true, true});
                                }

                            } catch (...) {
                                restore_source_states();
                                prepared.AbortNoexcept();
                                throw;
                            }
                        },
                        [&](RegridPublication&& payload) noexcept {
                            prepared.PublishNoexcept();
                            staged_ledger = std::move(payload.ledger);
                            staged_handles = std::move(payload.handles);
                        });
                });
    } catch (...) {
        if (transaction.state()
            != amr::TopologyTransactionState::Committed) {
            transaction.abort(
                [&]() noexcept { prepared.AbortNoexcept(); });
        }
        throw;
    }

    stage_handles = std::move(staged_handles);
    residency_ledger = std::move(staged_ledger);
    amr_ctrl.BindActiveHandles(stage_handles);
    prepared.ReleaseRetiredNoexcept();
    return true;
}

/** Lease the actual logical child family before provisional parent selection.
 */
bool DriverRuntime::device_jeans_parent_resolved(
    const amr::Block& parent,std::span<const int> siblings)
{
    if(!compute_backend || !config.amr.refine_on_jeans)
        throw std::logic_error("device JENS parent requires an explicit active request");

    topology_registry.validate_committed_snapshot(observe_topology());
    const auto version=current_interior_version();
    const auto& active=amr_ctrl.tree->GetActiveBlocks();
    if(active.size()!=stage_handles.size() || active.size()!=backend_storage.size())
        throw std::logic_error("device JENS parent source extent mismatch");
    std::vector<backend::BackendStateAccess> accesses;
    accesses.reserve(siblings.size());
    for(int id:siblings) {
        const auto found=std::find(active.begin(),active.end(),id);
        if(found==active.end()) throw std::logic_error("device JENS parent child is not active");
        const auto index=static_cast<std::size_t>(found-active.begin());
        residency_ledger->require_readable(
            {stage_handles[index],StateSlot::Current},{ExecutionSide::Device,version,true,false});
        const auto access=backend_access(index,StateSlot::Current);
        if(!compute_backend->contains(access))
            throw std::logic_error("device JENS parent storage is unavailable");
        accesses.push_back(access);
    }
    const auto minimum=compute_backend->evaluate_jeans_parent(accesses,parent);
    if(minimum && (!std::isfinite(*minimum) || *minimum<=0.))
        throw std::runtime_error("device JENS parent summary is invalid");
    return minimum && *minimum>=config.amr.jeans_cells;
}

/** Apply the configured regrid cadence through the actual chart transaction.
 */
bool DriverRuntime::perform_regrid(int step, double time, bool jeans_repair_only)
{
    return perform_regrid_impl(step,time,jeans_repair_only);
}
bool DriverRuntime::perform_regrid_impl(int step,double time,bool jeans_repair_only)
{
    const auto started = std::chrono::steady_clock::now();
    const auto before = compute_backend ? compute_backend->counters()
        : arch::backend::BackendCounters{};
    const auto old_blocks = stage_handles.size();
    const bool changed = execute_regrid(jeans_repair_only);
    const auto after = compute_backend ? compute_backend->counters()
        : arch::backend::BackendCounters{};
    regrid_measurements.push_back({step, time, old_blocks, stage_handles.size(), changed,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(),
        {after.kernel_count - before.kernel_count, after.bytes_h2d - before.bytes_h2d,
         after.bytes_d2h - before.bytes_d2h,
         after.stream_sync_count - before.stream_sync_count,
         after.getter_count - before.getter_count}});
    return changed;
}
/** Fully qualify accepted cells before another advance or durable output. */
void DriverRuntime::ensure_jeans_resolution(int step, double time)
{
    if (!config.amr.refine_on_jeans) return;
    // Include a final check after reaching lrefinemax. Refinement cannot hide
    // an unresolved finest-level state by exhausting the loop count.
    for (int pass=0;pass<=config.amr.lrefinemax;++pass)
        if (!perform_regrid(step,time,true)) return;
    throw std::logic_error("JENS repair did not converge within hierarchy depth");
}
} // namespace arch::driver
