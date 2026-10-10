/**
 * @file DriverRegrid.cpp
 * @brief Rebuild AMR topology and transfer state while invalidating stale stage views.
 *
 * Workflow:
 * 1. Prepare candidate topology, migration plans and replacement state ledger.
 * 2. Finalize real candidate BC/halo exchange with its own handles and ledger.
 * 3. Gate native-RZ thermodynamics before ghost/topology publication; on any
 *    failure restore Host source arrays or discard private Device storage, then
 *    abort staged mesh resources.
 * 4. For Device coarse parents inspect the staged active interior closure
 *    before the real BC, while the completed EOS and staged Jeans gates stay
 *    mandatory after it; early success publishes nothing.
 * 5. Publish the successful topology and its completed state identities.
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

/** Retry evidence is created only by this attempt's authentic restriction owner. */
class NativeCoarseningVeto final : public std::runtime_error {
public:
    const NativeCoarseningVetoRecord record;
    NativeCoarseningVeto(const std::string& message,NativeCoarseningVetoRecord value)
        :std::runtime_error(message),record(std::move(value)) {}
};

/** Exact thermal phase/status filter; never classify EOS exceptions by strings. */
bool native_thermal_veto(const RzThermodynamics::AcceptanceDiagnostic& d) noexcept
{
    return d.phase==RzThermodynamics::AcceptancePhase::effective_thermal
        &&d.inertia_mapping_valid&&d.node==-1
        &&(d.status==state::Status::unresolved_energy
            ||d.status==state::Status::energy_ceiling
            ||d.status==state::Status::invalid_thermodynamics);
}

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

/** Metadata-only identity of an accepted source, not another hierarchy copy. */
struct NativeRegridSource {
    int id;
    const amr::Block* block;
    LogicalBlockIdentity logical;
    std::array<const double*,7> pointers{};
    std::array<std::size_t,7> sizes{};
    int species,extent;
    std::array<int,8> layout;
    std::array<double,9> coordinates;
    GridMetrics::DyadicGridIdentity dyadic_identity;
    NativeRegridSource(int pool_id,const amr::Block& b,int dimension)
        :id(pool_id),block(&b),logical{dimension,b.level,b.logical_x1,b.logical_x2,b.logical_x3},
          species(b.fluid_state.GetNumSpecies()),extent(b.fluid_state.block_total_size_),
          layout{b.grid.dim,b.grid.ng,b.grid.stride_y,b.grid.stride_z,b.grid.total_size,
              b.grid.nblockx1,b.grid.nblockx2,b.grid.nblockx3},
          coordinates{b.grid.x1_min,b.grid.x1_max,b.grid.x2_min,b.grid.x2_max,
              b.grid.x3_min,b.grid.x3_max,b.grid.dx1,b.grid.dx2,b.grid.dx3},
          dyadic_identity(b.grid.dyadic_identity) {
        for(std::size_t f=0;f<regrid_fields.size();++f) {
            const auto& field=b.fluid_state.*regrid_fields[f];
            pointers[f]=field.data();sizes[f]=field.size();
        }
    }
    /** Reject pool, key, grid or field-allocation drift before the next attempt. */
    bool matches(const amr::Block& b,int dimension) const noexcept {
        const LogicalBlockIdentity key{dimension,b.level,b.logical_x1,b.logical_x2,b.logical_x3};
        const std::array<double,9> geometry{b.grid.x1_min,b.grid.x1_max,b.grid.x2_min,b.grid.x2_max,
            b.grid.x3_min,b.grid.x3_max,b.grid.dx1,b.grid.dx2,b.grid.dx3};
        const std::array<int,8> actual_layout{b.grid.dim,b.grid.ng,b.grid.stride_y,b.grid.stride_z,
            b.grid.total_size,b.grid.nblockx1,b.grid.nblockx2,b.grid.nblockx3};
        if(&b!=block||b.id!=id||!b.active||key!=logical||geometry!=coordinates
            ||actual_layout!=layout||b.grid.geometry!="cylindrical"
            ||!GridMetrics::equal_identity(b.grid.dyadic_identity,dyadic_identity)
            ||b.fluid_state.GetNumSpecies()!=species||b.fluid_state.block_total_size_!=extent)return false;
        for(std::size_t f=0;f<regrid_fields.size();++f) {
            const auto& field=b.fluid_state.*regrid_fields[f];
            if(field.data()!=pointers[f]||field.size()!=sizes[f])return false;
        }
        return true;
    }
};
} // namespace

/** One ordinary/device attempt, or bounded exact native-parent veto retries.
 * Workflow: accept source ghosts/EOS once, freeze exact source identities and
 * evaluated flags, then retry fresh transactions only for authentic restricted
 * interior thermal/JENS failures. Host failures restore Current values/leases;
 * Device failures discard private storage before topology abort and this loop.
 */
bool DriverRuntime::execute_regrid(bool jeans_repair_only,bool native_rz_candidate,
    const std::function<void()>& after_host_finalization)
{
    if(!native_rz_candidate)
        return execute_regrid_attempt(jeans_repair_only,false,after_host_finalization);
    if(runtime_state_transaction_
        ||geometry_semantics_!=GridMetrics::GeometrySemantics::AxisymmetricRz
        ||(compute_backend&&compute_backend->side()!=ExecutionSide::Device))
        throw std::logic_error("Native coarsening retry requires its actual quiescent RZ owner");
    if(compute_backend&&after_host_finalization)
        throw std::logic_error("Native Host finalization verification excludes Device regrid");
    ensure_fluid_ghosts(); // actual selected EOS and completed source qualification
    topology_registry.validate_committed_snapshot(observe_topology());
    const auto source_active=amr_ctrl.tree->GetActiveBlocks();
    const auto source_handles=stage_handles;
    const auto* const handle_address=stage_handles.data();
    auto* const source_backend=compute_backend.get();
    const auto source_storage=backend_storage;
    const auto* const storage_address=backend_storage.data();
    const auto source_side=source_backend?ExecutionSide::Device:ExecutionSide::Host;
    const auto* const source_pool=amr_ctrl.pool.get();
    const auto* const source_tree=amr_ctrl.tree.get();
    auto* const source_ledger=residency_ledger.get();
    const auto source_epoch=source_ledger->active_epoch();
    const auto source_version=current_interior_version();
    const auto boundary_context=bc_handler.snapshot_stage_context();
    std::vector<NativeRegridSource> sources;
    sources.reserve(source_active.size());
    for(const int id:source_active)sources.emplace_back(id,amr_ctrl.pool->GetBlock(id),config.grid.dim);
    const auto require_sources=[&] {
        if(compute_backend.get()!=source_backend
            ||(source_backend&&(backend_storage.data()!=storage_address||backend_storage!=source_storage))
            ||amr_ctrl.pool.get()!=source_pool||amr_ctrl.tree.get()!=source_tree
            ||residency_ledger.get()!=source_ledger
            ||source_ledger->active_epoch()!=source_epoch
            ||stage_handles.data()!=handle_address||stage_handles!=source_handles
            ||amr_ctrl.tree->GetActiveBlocks()!=source_active
            ||!bc_handler.stage_context_matches(boundary_context))
            throw std::logic_error("Native coarsening retry source owner/epoch changed");
        topology_registry.validate_committed_snapshot(observe_topology());
        for(std::size_t index=0;index<sources.size();++index) {
            if(!sources[index].matches(amr_ctrl.pool->GetBlock(sources[index].id),config.grid.dim))
                throw std::logic_error("Native coarsening retry source allocation/layout changed");
            source_ledger->require_readable({source_handles[index],StateSlot::Current},
                {source_side,source_version,true,true});
            if(source_backend&&!source_backend->contains(
                {source_handles[index],source_storage[index],StateSlot::Current}))
                throw std::logic_error("Native coarsening retry source storage changed");
        }
    };
    std::vector<std::pair<int,int>> frozen_flags;
    std::set<amr::LogicalBlockKey> potential_parents;
    bool evaluated=false;
    const auto evaluate_native=[&] {
        require_sources();
        if(evaluated) {
            for(const auto& [id,flag]:frozen_flags)amr_ctrl.pool->GetBlock(id).refine_flag=flag;
            return;
        }
        if(source_backend)evaluate_device_regrid_indicators(jeans_repair_only);
        else if(jeans_repair_only)amr_ctrl.tree->EvaluateJeansRepair(config);
        else amr_ctrl.tree->EvaluateRefinement(config);
        frozen_flags.reserve(source_active.size());
        std::map<amr::LogicalBlockKey,std::set<amr::LogicalBlockKey>> groups;
        for(const int id:source_active) {
            const auto& b=amr_ctrl.pool->GetBlock(id);
            frozen_flags.emplace_back(id,b.refine_flag);
            if(b.refine_flag==-1&&b.level>0) {
                const amr::LogicalBlockKey parent{config.grid.dim,b.level-1,
                    b.logical_x1>>1,b.logical_x2>>1,b.logical_x3>>1};
                groups[parent].insert({config.grid.dim,b.level,b.logical_x1,b.logical_x2,b.logical_x3});
            }
        }
        for(const auto& [parent,children]:groups)
            if(children.size()==static_cast<std::size_t>(1<<config.grid.dim))potential_parents.insert(parent);
        evaluated=true;
    };
    std::vector<amr::LogicalBlockKey> vetoed;
    native_coarsening_veto_records_.clear();
    for(;;) {
        require_sources();
        try {
            return execute_regrid_attempt(jeans_repair_only,true,after_host_finalization,vetoed,evaluate_native);
        } catch(const NativeCoarseningVeto& failure) {
            require_sources(); // attempt restored arrays/BC and aborted before retry
            if(failure.record.scope.from_epoch!=source_epoch
                ||!potential_parents.contains(failure.record.parent)
                ||std::find(vetoed.begin(),vetoed.end(),failure.record.parent)!=vetoed.end()
                ||vetoed.size()>=potential_parents.size())
                throw std::logic_error("Native coarsening veto is duplicate, foreign or outside finite source families");
            vetoed.push_back(failure.record.parent);
            native_coarsening_veto_records_.push_back(failure.record);
        } catch(...) {
            // No physics/output rollback is invented. The attempt owns source
            // restoration; preserve the accepted BC frame before propagating.
            if(residency_ledger.get()==source_ledger&&stage_handles==source_handles)
                bc_handler.restore_stage_context_noexcept(boundary_context);
            throw;
        }
    }
}

/** Evaluate the original Device source flags before topology preparation.
 * Workflow: select indicators -> complete actual Current ghosts -> evaluate
 * original summaries/JENS -> reject finest deficits -> publish only flags.
 * The Native retry owner freezes these flags once and replays them unchanged.
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
bool DriverRuntime::execute_regrid_attempt(bool jeans_repair_only,bool native_rz_candidate,
    const std::function<void()>& after_host_finalization,
    std::span<const amr::LogicalBlockKey> vetoed_coarsenings,
    const std::function<void()>& evaluate_native_indicators)
{
    if(runtime_state_transaction_)throw std::logic_error("Active Host Hydro owner excludes regrid");
    if(after_host_finalization&&(!native_rz_candidate||compute_backend))
        throw std::logic_error("Native finalization verification requires its actual Host candidate");
    if(native_rz_candidate&&(geometry_semantics_!=GridMetrics::GeometrySemantics::AxisymmetricRz
        ||(compute_backend&&compute_backend->side()!=ExecutionSide::Device)))
        throw std::logic_error("Native RZ transaction verification requires its actual chart/side");
    if (geometry_semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz&&!native_rz_candidate)
        throw std::logic_error("RZ regrid migration and angular-momentum contract are incomplete");
    // Internal transaction qualification uses backend-local JENS consumers.
    // Public configuration/startup gates remain until full lifecycle acceptance.
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
    if (!compute_backend&&!native_rz_candidate) ensure_fluid_ghosts();
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
        ? amr_ctrl.tree->PrepareRegrid(
            config, {}, {}, native_rz_candidate?evaluate_native_indicators
                :std::function<void()>(evaluate_device_indicators), jeans_repair_only,
            (native_rz_candidate||config.amr.refine_on_jeans)
                ? std::function<bool(const amr::Block&,std::span<const int>)>(evaluate_device_parent)
                : std::function<bool(const amr::Block&,std::span<const int>)>{},vetoed_coarsenings)
        : amr_ctrl.tree->PrepareRegrid(config, {}, {},
            native_rz_candidate?evaluate_native_indicators:std::function<void()>{},
            jeans_repair_only,{},vetoed_coarsenings);
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

    // Only this attempt's authentic restricted-parent active-cell refusal may
    // enter the finite loop. Ghost/survivor/prolongation/other failures stay fatal.
    const auto veto_native_parent_thermal = [&](const NativeBoundaryAcceptanceError& error,
        std::span<const amr::BlockHandle> candidate_handles, state::StateVersion version) {
        if(native_rz_candidate&&!after_host_finalization&&error.slot==StateSlot::Current
            &&error.version==version
            &&native_thermal_veto(error.diagnostic)) {
            const auto& active=amr_ctrl.tree->GetActiveBlocks();
            const auto found=std::find(active.begin(),active.end(),error.pool_index);
            if(found!=active.end()) {
                const auto patch=static_cast<std::size_t>(found-active.begin());
                const auto& grid=amr_ctrl.pool->GetBlock(error.pool_index).grid;
                const auto& d=error.diagnostic;
                if(patch<candidate_handles.size()&&candidate_handles[patch]==error.handle
                    &&d.i>=grid.Is()&&d.i<grid.Ie()&&d.j>=grid.Js()&&d.j<grid.Je()
                    &&d.index==grid.GetIndex(d.i,d.j,0)) {
                    const auto key=prepared.restricted_parent_key(error.pool_index);
                    if(key)
                        throw NativeCoarseningVeto(error.what(),
                            {*key,NativeCoarseningVetoKind::EffectiveThermal,d,scope,
                                error.handle,error.version,0.});
                }
            }
        }
    };
    // Validate each actual completed patch in the original Host evaluation order.
    // The caller obtains its key from this live prepared restriction relation.
    const auto validate_completed_parent_jeans = [&](
        std::span<const amr::BlockHandle> candidate_handles, state::StateVersion version,
        std::size_t patch, const std::optional<amr::LogicalBlockKey>& key, double minimum) {
        const auto& actual_ids=amr_ctrl.tree->GetActiveBlocks();
        if(candidate_handles.size()!=actual_ids.size()||patch>=actual_ids.size())
            throw std::logic_error("Completed candidate JENS result extent mismatch");
        if(!std::isfinite(minimum)||minimum<=0.)
            throw std::runtime_error("Completed candidate JENS result is invalid");
        if(key&&minimum<config.amr.jeans_cells)
            throw NativeCoarseningVeto("Completed restricted parent remains JENS underresolved",
                {*key,NativeCoarseningVetoKind::JeansResolution,std::nullopt,
                    scope,candidate_handles[patch],version,minimum});
    };

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
            const bool native_staged =
                geometry_semantics_ == GridMetrics::GeometrySemantics::AxisymmetricRz;
            const auto staged_flux_plan = native_staged
                ? amr::build_amr_flux_topology_plan(
                    *amr_ctrl.pool, amr_ctrl.tree->GetActiveBlocks(), payload.handles,
                    config.grid.dim, specs.count(), geometry_semantics_, true)
                : amr::build_amr_flux_topology_plan(
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
            if (native_staged) {
                // Migration has joined the actual Current interiors. Keep this
                // Device ledger private; only the real boundary/EOS gate may
                // publish its ghosts before the staged completion joins.
                payload.ledger = std::make_unique<StateResidencyLedger>(scope.to_epoch);
                for (const auto handle : payload.handles)
                    payload.ledger->register_block(handle, payload.topology_witness.version,
                        payload.topology_witness.completion, ExecutionSide::Device);
                StageExecutionContext staged_context{
                    ExecutionSide::Device, *payload.ledger, scheduler_clock};
                auto* const staged_backend = compute_backend.get();
                auto* const staged_store = payload.store_transaction.get();
                auto* const staged_ledger = payload.ledger.get();
                auto* const staged_pool = amr_ctrl.pool.get();
                auto* const staged_tree = amr_ctrl.tree.get();
                const auto migrated = payload.topology_witness;
                const std::span<const amr::BlockHandle> staged_handles = payload.handles;
                const std::span<const backend::StorageGeneration> staged_storage = payload.storage;
                const std::vector<int> staged_ids = amr_ctrl.tree->GetActiveBlocks();
                const std::vector<backend::BackendStateAccess> staged_accesses = [&] {
                    std::vector<backend::BackendStateAccess> result;
                    result.reserve(staged_handles.size());
                    for (std::size_t index = 0; index < staged_handles.size(); ++index)
                        result.push_back({staged_handles[index], staged_storage[index], StateSlot::Current});
                    return result;
                }();
                bind_native_boundary_acceptance(staged_context, staged_handles,
                    staged_store, staged_storage);
                const std::function<void()> validate_storage = [&] {
                    if (compute_backend.get() != staged_backend
                        || payload.store_transaction.get() != staged_store
                        || payload.ledger.get() != staged_ledger
                        || residency_ledger.get() == staged_ledger
                        || staged_ledger->active_epoch() != scope.to_epoch
                        || staged_context.side != ExecutionSide::Device
                        || &staged_context.ledger != staged_ledger
                        || &staged_context.clock != &scheduler_clock
                        || !staged_context.post_boundary_acceptance
                        || payload.topology_witness.version != migrated.version
                        || payload.topology_witness.completion != migrated.completion
                        || payload.handles.data() != staged_handles.data()
                        || payload.storage.data() != staged_storage.data()
                        || payload.handles.size() != staged_accesses.size()
                        || payload.storage.size() != staged_accesses.size()
                        || amr_ctrl.pool.get() != staged_pool
                        || amr_ctrl.tree.get() != staged_tree
                        || amr_ctrl.tree->GetActiveBlocks() != staged_ids)
                        throw std::logic_error("Native staged boundary candidate identity changed");
                    for (std::size_t index = 0; index < staged_accesses.size(); ++index) {
                        const auto access = staged_accesses[index];
                        if (payload.handles[index] != access.block
                            || payload.storage[index] != access.storage
                            || !staged_backend->contains(*staged_store, access))
                            throw std::logic_error("Native staged boundary storage identity changed");
                        staged_ledger->require_readable({access.block, StateSlot::Current},
                            {ExecutionSide::Device, migrated.version, true, false});
                        const auto current = staged_ledger->inspect({access.block, StateSlot::Current});
                        if (current.interior.residency != state::StateResidency::DeviceValid
                            || current.interior.completion != migrated.completion
                            || current.interior.pending_transfer != state::PendingTransferPhase::None
                            || current.ghost.pending_transfer != state::PendingTransferPhase::None)
                            throw std::logic_error("Native staged boundary Current interior changed");
                    }
                };
                compute_backend->complete_staged_current_ghosts(
                    *payload.store_transaction, staged_same_level, staged_coarse_fine,
                    amr_ctrl.tree->GetActiveBlocks(), payload.handles,
                    &staged_coordinate_seam,
                    [&](backend::BackendTopologyStoreTransaction& actual_store) {
                        if (&actual_store != staged_store)
                            throw std::logic_error("Native staged boundary callback store changed");
                        validate_storage();
                        // Early failure-only closure evidence of the actual
                        // coarse restricted parents, taken BEFORE the real BC
                        // touches these staged interiors. Only the prepared
                        // restriction relation names the eligible subset.
                        const auto& actual_ids=amr_ctrl.tree->GetActiveBlocks();
                        if(staged_accesses.size()!=actual_ids.size())
                            throw std::logic_error("Native staged early closure access extent mismatch");
                        std::vector<backend::BackendStateAccess> restricted_accesses;
                        std::vector<std::size_t> restricted_patches;
                        for(std::size_t patch=0;patch<actual_ids.size();++patch)
                            if(prepared.restricted_parent_key(actual_ids[patch])) {
                                restricted_accesses.push_back(staged_accesses[patch]);
                                restricted_patches.push_back(patch);
                            }
                        const auto early=staged_backend->inspect_native_restricted_interiors(
                            actual_store,restricted_accesses,native_rz_eos_bounds());
                        if(early) {
                            const auto found=std::find_if(restricted_accesses.begin(),
                                restricted_accesses.end(),[&](const backend::BackendStateAccess& access) {
                                    return access.block==early->access.block
                                        &&access.storage==early->access.storage;
                                });
                            if(found==restricted_accesses.end()
                                ||early->access.slot!=StateSlot::Current)
                                throw std::logic_error("Native staged early closure access is outside the restricted subset");
                            const auto patch=restricted_patches[
                                static_cast<std::size_t>(found-restricted_accesses.begin())];
                            const auto& grid=amr_ctrl.pool->GetBlock(actual_ids[patch]).grid;
                            const auto& diagnostic=early->diagnostic;
                            if(diagnostic.i<grid.Is()+1||diagnostic.i>=grid.Ie()-1
                                ||diagnostic.j<grid.Js()||diagnostic.j>=grid.Je()
                                ||diagnostic.index!=grid.GetIndex(diagnostic.i,diagnostic.j,0))
                                throw std::logic_error("Native staged early closure diagnostic is outside the eligible interior");
                            // Same veto classifier as the completed path: only a
                            // typed exact thermal refusal retries. Anything else,
                            // including provisional/density/inertia, stays fatal.
                            NativeBoundaryAcceptanceError error(
                                "Restricted native parent interior cannot represent its conservative thermal state",
                                actual_ids[patch],staged_handles[patch],StateSlot::Current,
                                migrated.version,diagnostic);
                            veto_native_parent_thermal(error,staged_handles,migrated.version);
                            throw error;
                        }
                        try {
                            (void)scheduler::complete_boundary(staged_context, staged_handles,
                                StateSlot::Current, migrated.version,
                                [&](StateSlot slot, state::StateVersion version,
                                    state::CompletionToken token) {
                                    return execute_device_boundary_domain(staged_accesses,
                                        staged_handles, slot, version, token,
                                        &actual_store, validate_storage);
                                });
                        } catch(const NativeBoundaryAcceptanceError& error) {
                            veto_native_parent_thermal(error,staged_handles,migrated.version);
                            throw;
                        }
                        for(const auto handle:staged_handles)
                            staged_ledger->require_readable({handle,StateSlot::Current},
                                {ExecutionSide::Device,migrated.version,true,true});
                        if(config.amr.refine_on_jeans) {
                            // The actual callback still owns migrated-interior
                            // storage; markComplete follows only after return.
                            const auto minima=staged_backend->evaluate_jeans_resolution(
                                actual_store,staged_accesses);
                            const auto& actual_ids=amr_ctrl.tree->GetActiveBlocks();
                            if(minima.size()!=actual_ids.size())
                                throw std::logic_error("Completed candidate JENS result extent mismatch");
                            for(std::size_t patch=0;patch<actual_ids.size();++patch) {
                                const auto key=prepared.restricted_parent_key(actual_ids[patch]);
                                validate_completed_parent_jeans(staged_handles,migrated.version,
                                    patch,key,minima[patch]);
                            }
                        }
                    });
            } else {
                compute_backend->complete_staged_current_ghosts(
                    *payload.store_transaction, staged_same_level, staged_coarse_fine,
                    amr_ctrl.tree->GetActiveBlocks(), payload.handles,
                    &staged_coordinate_seam);
            }
            prepared.CompleteDeviceMigration();

            // No field H2D transfer occurred: publish the actual authority of
            // these reconstructed fields. Host arrays are deliberately
            // stale and materialize only for an explicit Host consumer.
            if (!native_staged) {
                payload.ledger = std::make_unique<StateResidencyLedger>(scope.to_epoch);
                for (const auto handle : payload.handles) {
                    payload.ledger->register_block(handle, payload.topology_witness.version,
                        payload.topology_witness.completion, ExecutionSide::Device);
                    payload.ledger->publish_ghost({handle, StateSlot::Current},
                        ExecutionSide::Device, payload.topology_witness.version,
                        payload.topology_witness.completion);
                }
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
        if(native_rz_candidate) {
            const auto bounds=native_rz_eos_bounds();
            std::vector<double> fractions(static_cast<std::size_t>(specs.count()));
            const auto proposed_ids=prepared.proposed_active_blocks();
            for(std::size_t patch=0;patch<proposed_ids.size();++patch) {
                const int id=proposed_ids[patch];
                const auto key=prepared.restricted_parent_key(id);
                if(!key)continue;
                const auto& block=amr_ctrl.pool->GetBlock(id);
                const auto& fluid=block.fluid_state;const auto& grid=block.grid;
                const auto geometry=GridMetrics::make_geometry_view(grid,geometry_semantics_);
                const auto read=[&fluid](int cell) {return fluid.get(cell);};
                // Failure-only: these three migrated active rho observations
                // are final and independent of unknown candidate BC/ghosts. The
                // shared leaf owns the original provisional/closure arithmetic;
                // messages, classifier and meta provenance stay unchanged.
                for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is()+1;i<grid.Ie()-1;++i) {
                    const int cell=grid.GetIndex(i,j,0);
                    for(int sp=0;sp<specs.count();++sp)fractions[sp]=fluid.X(sp,cell);
                    const auto diagnostic=RzThermodynamics::check_restricted_interior_cell(
                        read,geometry,i,j,cell,fractions.data(),specs.count(),bounds);
                    if(diagnostic.status==state::Status::valid)continue;
                    if(diagnostic.phase==RzThermodynamics::AcceptancePhase::provisional)
                        throw std::runtime_error("Restricted native parent provisional state is invalid");
                    if(!native_thermal_veto(diagnostic))
                        throw RzThermodynamics::AcceptanceError("Restricted native parent early closure is invalid",diagnostic);
                    throw NativeCoarseningVeto("Restricted native parent interior cannot represent its conservative thermal state",
                        {*key,NativeCoarseningVetoKind::EffectiveThermal,diagnostic,scope,
                            proposed.handles_in_observation_order[patch],{},0.});
                }
            }
        }
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
                                bind_native_boundary_acceptance(
                                    staged_context, payload.handles);
                                TimeIntegration::synchronize_domain_boundary(amr_ctrl, bc_handler,
                                    &amr::Block::fluid_state, payload.handles, geometry_semantics_,
                                    {config.numerics.sml_rho,config.numerics.min_eint,config.numerics.max_eint});
                                // Borrowed internal verification callback only.
                                // It runs inside the same fallible finalizer,
                                // after real BC/ghost work and before publication.
                                if(after_host_finalization)after_host_finalization();
                                try {
                                    (void)arch::scheduler::complete_boundary(
                                    staged_context, payload.handles,
                                    StateSlot::Current,
                                    payload.topology_witness.version,
                                    [](StateSlot,
                                       arch::state::StateVersion,
                                       arch::state::CompletionToken token) {
                                        return token;
                                    });
                                } catch(const NativeBoundaryAcceptanceError& error) {
                                    veto_native_parent_thermal(error,payload.handles,
                                        payload.topology_witness.version);
                                    throw; // ghost/EOS/survivor/prolongation failures stay fatal
                                }

                                for (const amr::BlockHandle handle
                                     : payload.handles) {
                                    payload.ledger->require_readable(
                                        {handle, StateSlot::Current},
                                        {ExecutionSide::Host,
                                         payload.topology_witness.version,
                                         true, true});
                                }
                                if(native_rz_candidate&&config.amr.refine_on_jeans) {
                                    const auto& actual_ids=amr_ctrl.tree->GetActiveBlocks();
                                    for(std::size_t patch=0;patch<actual_ids.size();++patch) {
                                        const int id=actual_ids[patch];
                                        const auto key=prepared.restricted_parent_key(id);
                                        const double minimum=amr_ctrl.tree->MinimumJeansCells(amr_ctrl.pool->GetBlock(id));
                                        validate_completed_parent_jeans(payload.handles,
                                            payload.topology_witness.version,patch,key,minimum);
                                    }
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
 * Workflow: validate Native geometry and committed Current/ghost storage, then
 * defer its thermal/JENS gate to the real completed staged candidate. Existing
 * geometry retains the original private parent EOS/JENS call below.
 * Tree owns sibling order/parent construction; no Host field arrays are read.
 */
bool DriverRuntime::device_jeans_parent_resolved(
    const amr::Block& parent,std::span<const int> siblings)
{
    const bool native=geometry_semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz;
    if(!compute_backend || (!native&&!config.amr.refine_on_jeans))
        throw std::logic_error("device JENS parent requires an explicit active request");
    if(native) {
        if(compute_backend->side()!=ExecutionSide::Device)
            throw std::logic_error("Native provisional parent requires its actual Device owner");
        const auto expected=amr_ctrl.tree->CandidateParentGeometry(siblings);
        parent.RequireNativeGeometryIdentity();
        (void)GridMetrics::make_geometry_view(parent.grid,geometry_semantics_);
        if(!GridMetrics::equal_identity(parent.grid.dyadic_identity,expected.grid.dyadic_identity))
            throw std::logic_error("Native provisional parent geometry changed its actual child family");
    }
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
            {stage_handles[index],StateSlot::Current},{ExecutionSide::Device,version,true,native});
        const auto access=backend_access(index,StateSlot::Current);
        if(!compute_backend->contains(access))
            throw std::logic_error("device JENS parent storage is unavailable");
        accesses.push_back(access);
    }
    // Native is provisional only: migration and the completed staged owner
    // must still gate actual parent thermal closure and Jeans before publication.
    if(native)return true;
    const auto minimum=compute_backend->evaluate_jeans_parent(accesses,parent);
    if(minimum && (!std::isfinite(*minimum) || *minimum<=0.))
        throw std::runtime_error("device JENS parent summary is invalid");
    return minimum && *minimum>=config.amr.jeans_cells;
}

/** Apply the configured regrid cadence through the actual chart transaction.
 * Native RZ on either actual side uses the completed-EOS/thermal/JENS finalizer
 * and one bounded coarsening-veto owner; Existing keeps its original path.
 * Both ordinary indicators and JENS-only repair keep their existing formulas. */
bool DriverRuntime::perform_regrid(int step, double time, bool jeans_repair_only)
{
    const bool native=geometry_semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz;
    return perform_regrid_impl(step,time,jeans_repair_only,native);
}
/** Internal qualification consumes the same full migration/finalizer transaction.
 * No alternative transfer math, namespace publication, or physical capability. */
bool DriverRuntime::regrid_native_rz_candidate(int step,double time,
    const std::function<void()>& after_host_finalization)
{
    return perform_regrid_impl(step,time,false,true,after_host_finalization);
}
bool DriverRuntime::perform_regrid_impl(int step,double time,bool jeans_repair_only,
    bool native_rz_candidate,const std::function<void()>& after_host_finalization)
{
    if(runtime_state_transaction_)throw std::logic_error("Active Host Hydro owner excludes regrid");
    const auto started = std::chrono::steady_clock::now();
    const auto before = compute_backend ? compute_backend->counters()
        : arch::backend::BackendCounters{};
    const auto old_blocks = stage_handles.size();
    const bool changed = execute_regrid(jeans_repair_only,native_rz_candidate,after_host_finalization);
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
