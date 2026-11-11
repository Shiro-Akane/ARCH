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

/** Move one accepted Host leaf without changing its logical/numerical state.
 * Native fields (including ghosts and all three slots) travel as binary64 bits.
 * Only sender/receiver allocate the packet; original pool owns candidate storage.
 * Every fallible local phase completes collectively before subsequent transport
 * or publication. There are no outstanding MPI requests when rollback begins.
 */
void DriverRuntime::migrate_host_leaf(amr::AmrEndpoint source, int destination_rank,
    const std::function<void(LeafTransferPhase)>& observer)
{
#if ARCH_MPI_ENABLED
    using namespace amr::flux_mpi_detail;
    const auto [rank, size] = require_world();
    const auto notify = [&](LeafTransferPhase phase) {if (observer) observer(phase);};
    constexpr std::array<FluidState amr::Block::*,3> slots{
        &amr::Block::fluid_state,&amr::Block::state_next,&amr::Block::state_scratch};
    std::vector<amr::BlockTopology> global;
    int sender=-1, source_id=-1, target_id=-1;
    state::StateVersion accepted{};
    prepare([&] {
        if (size<2 || rank!=amr_ctrl.tree->PartitionRank()
            || size!=amr_ctrl.tree->PartitionSize() || compute_backend || host_hydro_attempt_
            || config.grid.geometry!="cartesian" || config.physics.burn.use_burn
            || config.physics.diffusion.use_diffusion || config.physics.gravity.type!="none"
            || bc_handler.has_user() || !distributed_hydro_topology())
            throw std::logic_error("ownership transfer requires quiescent Cartesian Host Hydro");
        topology_registry.validate_committed_snapshot(observe_topology());
        if (topology_registry.handle_for(source.logical)!=source.handle)
            throw std::invalid_argument("ownership transfer has a stale logical endpoint");
        sender=topology_registry.owner_for(source.logical);
        if (destination_rank<0 || destination_rank>=size || sender==destination_rank)
            throw std::invalid_argument("ownership transfer requires another valid rank");
        if (rank==sender && stage_handles.size()<=1)
            throw std::logic_error("ownership transfer does not yet support an empty runtime rank");
        if (next_amr_transaction_id==std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("AMR transaction ID exhausted");
        accepted=current_interior_version();
        for (auto h:stage_handles)
            residency_ledger->require_readable({h,StateSlot::Current},
                {ExecutionSide::Host,accepted,true,true});
        amr_ctrl.flux_register.RequireOwnershipTransferBoundary();
        global=topology_registry.blocks();
        for (const auto& leaf:global)
            if (leaf.logical==source.logical) source_id=leaf.pool_index;
        // Borrowed callback attachments are not transportable process identities.
        for (int id:amr_ctrl.tree->GetActiveBlocks())
            for (auto member:slots) {
                const auto& state=amr_ctrl.pool->GetBlock(id).*member;
                if (state.diffusion_boundary || state.boundary_flux_capture)
                    throw std::logic_error("ownership transfer cannot migrate boundary attachments");
            }
    });
    agree(signature(amr_ctrl.tree->GetRootGrid(),specs.count(),source.handle.epoch,
        4,next_amr_transaction_id,ctrl.t_current,0));
    std::array<std::uint64_t,21> identity{
        source.handle.uid.value,source.handle.epoch.value,
        static_cast<std::uint64_t>(source.logical.dimension),
        static_cast<std::uint64_t>(source.logical.level),
        source.logical.logical_x1,source.logical.logical_x2,source.logical.logical_x3,
        static_cast<std::uint64_t>(sender),static_cast<std::uint64_t>(destination_rank),
        accepted.value,scheduler_clock.last_token(),scheduler_clock.last_version(),
        static_cast<std::uint64_t>(ctrl.step_count),std::bit_cast<std::uint64_t>(ctrl.dt_old),
        next_amr_transaction_id,1}; // Transaction identity and native packet schema.
    agree(identity);

    std::optional<amr::AmrTree::PreparedRegrid> prepared;
    std::optional<topology::TopologyIdentityRegistry::Candidate> candidate;
    std::optional<amr::TopologyTransaction> transaction;
    std::unique_ptr<StateResidencyLedger> ledger;
    std::vector<amr::BlockHandle> handles;
    std::vector<amr::BlockTopology> proposed_global;
    // Per slot: cell count, species count, seven field lengths, repair row length.
    std::array<std::uint64_t,30> layout{};
    std::vector<std::uint64_t> packet;
    try {
        prepare([&] {
            const std::span<const int> outgoing=rank==sender
                ? std::span<const int>(&source_id,1):std::span<const int>{};
            const std::span<const amr::LogicalBlockKey> incoming=rank==destination_rank
                ? std::span<const amr::LogicalBlockKey>(&source.logical,1)
                : std::span<const amr::LogicalBlockKey>{};
            prepared.emplace(amr_ctrl.tree->PrepareLeafOwnership(config,outgoing,incoming,specs.count()));
            if (rank==destination_rank)
                for (int id:prepared->proposed_active_blocks()) {
                    const auto& block=amr_ctrl.pool->GetBlock(id);
                    if (source.logical==amr::LogicalBlockKey{config.grid.dim,block.level,
                            block.logical_x1,block.logical_x2,block.logical_x3}) target_id=id;
                }
            if (rank==destination_rank && target_id<0)
                throw std::logic_error("ownership receiver has no candidate storage");
            auto observations=observe_blocks(prepared->proposed_active_blocks());
            for (auto leaf:global) {
                if (leaf.logical==source.logical) leaf.owner_rank=destination_rank;
                if (leaf.owner_rank!=rank)
                    observations.push_back({-1,leaf.logical,leaf.owner_rank});
            }
            candidate.emplace(topology_registry.stage_reconciliation(observations));
            const auto& result=candidate->reconciliation();
            if (!result.topology_changed || !result.added.empty() || !result.retired.empty())
                throw std::logic_error("owner-only transfer changed logical block identities");
            handles=result.handles_in_observation_order;
            proposed_global=candidate->blocks();
            transaction.emplace(next_amr_transaction_id++,source.handle.epoch,result.epoch);
            transaction->begin_migration();
            if (rank==sender) {
                const auto& block=amr_ctrl.pool->GetBlock(source_id);
                if (block.refine_flag!=0)
                    throw std::logic_error("ownership transfer requires no pending refinement decision");
                for (std::size_t slot=0;slot<slots.size();++slot) {
                    const auto& state=block.*slots[slot];
                    const auto cells=static_cast<std::size_t>(block.grid.GetTotalSize());
                    if (state.block_total_size_!=static_cast<int>(cells)
                        || state.GetNumSpecies()!=specs.count())
                        throw std::invalid_argument("ownership source state layout mismatch");
                    layout[10*slot]=cells;layout[10*slot+1]=specs.count();
                    for (std::size_t field=0;field<regrid_fields.size();++field) {
                        const auto count=(state.*regrid_fields[field]).size();
                        const auto expected=field==6?FluidState::CheckedSpeciesSize(specs.count(),cells):cells;
                        if (count!=expected) throw std::invalid_argument("ownership field extent mismatch");
                        layout[10*slot+2+field]=count;
                    }
                    layout[10*slot+9]=state.stage_repairs.values.size();
                }
            }
            notify(LeafTransferPhase::Reserved);
        });
        check(MPI_Bcast(layout.data(),static_cast<int>(layout.size()),MPI_UINT64_T,sender,MPI_COMM_WORLD));
        prepare([&] {
            std::size_t count=identity.size()+layout.size();
            for (std::size_t slot=0;slot<slots.size();++slot) {
                const auto cells=layout[10*slot];
                const auto species=layout[10*slot+1];
                const auto repairs=layout[10*slot+9];
                if (!cells || cells>std::numeric_limits<int>::max() || species!=static_cast<std::uint64_t>(specs.count())
                    || repairs<state::RepairView::fixed_size
                    || repairs>state::RepairView::fixed_size+2*species || (repairs-state::RepairView::fixed_size)%2)
                    throw std::invalid_argument("ownership packet layout is invalid");
                for (int field=0;field<7;++field) {
                    const auto expected=field==6?FluidState::CheckedSpeciesSize(species,cells):cells;
                    if (layout[10*slot+2+field]!=expected)
                        throw std::invalid_argument("ownership packet field extent is invalid");
                    count+=expected;
                }
                count+=repairs+6; // UID, stage, time, and three coordinates.
            }
            if (count>static_cast<std::size_t>(std::numeric_limits<int>::max()))
                throw std::overflow_error("ownership packet exceeds MPI count");
            if (rank==sender || rank==destination_rank) packet.resize(count);
            if (rank==sender) {
                auto out=std::copy(identity.begin(),identity.end(),packet.begin());
                out=std::copy(layout.begin(),layout.end(),out);
                for (auto member:slots) {
                    const auto& state=amr_ctrl.pool->GetBlock(source_id).*member;
                    for (auto field:regrid_fields)
                        for (double value:state.*field) *out++=std::bit_cast<std::uint64_t>(value);
                    const auto& repair=state.stage_repairs;
                    for (double value:repair.values) *out++=std::bit_cast<std::uint64_t>(value);
                    *out++=repair.block_uid;
                    *out++=static_cast<std::uint64_t>(static_cast<std::int64_t>(repair.stage));
                    *out++=std::bit_cast<std::uint64_t>(repair.time);
                    for (double value:repair.position) *out++=std::bit_cast<std::uint64_t>(value);
                }
                if (out!=packet.end()) throw std::logic_error("ownership packet packing mismatch");
            }
            if (rank==destination_rank) {
                auto& block=amr_ctrl.pool->GetBlock(target_id);
                for (std::size_t slot=0;slot<slots.size();++slot) {
                    auto& state=block.*slots[slot];
                    if (state.block_total_size_!=static_cast<int>(layout[10*slot])
                        || state.GetNumSpecies()!=static_cast<int>(layout[10*slot+1]))
                        throw std::invalid_argument("ownership receiver state layout mismatch");
                    for (int field=0;field<7;++field)
                        if ((state.*regrid_fields[field]).size()!=layout[10*slot+2+field])
                            throw std::invalid_argument("ownership receiver field extent mismatch");
                    state.stage_repairs.values.resize(layout[10*slot+9]);
                    state.diffusion_boundary.reset();
                    state.boundary_flux_capture.reset();
                }
            }
        });
        // A single completed point-to-point packet; no remote pool IDs or pointers.
        constexpr int tag=49;
        if (rank==sender)
            check(MPI_Send(packet.data(),static_cast<int>(packet.size()),MPI_UINT64_T,destination_rank,tag,MPI_COMM_WORLD));
        else if (rank==destination_rank) {
            MPI_Status status{};
            check(MPI_Recv(packet.data(),static_cast<int>(packet.size()),MPI_UINT64_T,sender,tag,MPI_COMM_WORLD,&status));
            int count=0;check(MPI_Get_count(&status,MPI_UINT64_T,&count));
            // Preserve count error for the next collective, not a unilateral exit.
            if (count!=static_cast<int>(packet.size())) packet.clear();
        }
        prepare([&] {
            if (rank==destination_rank) {
                if (packet.size()<identity.size()+layout.size()
                    || !std::equal(identity.begin(),identity.end(),packet.begin())
                    || !std::equal(layout.begin(),layout.end(),packet.begin()+identity.size()))
                    throw std::invalid_argument("ownership packet identity/layout mismatch");
                auto in=packet.begin()+identity.size()+layout.size();
                auto& block=amr_ctrl.pool->GetBlock(target_id);
                for (auto member:slots) {
                    auto& state=block.*member;
                    for (auto field:regrid_fields) {
                        for (double& value:state.*field) value=std::bit_cast<double>(*in++);
                        notify(LeafTransferPhase::FieldReceived);
                    }
                    auto& repair=state.stage_repairs;
                    for (double& value:repair.values) value=std::bit_cast<double>(*in++);
                    repair.block_uid=*in++;
                    const auto stage=std::bit_cast<std::int64_t>(*in++);
                    if (stage<std::numeric_limits<int>::min() || stage>std::numeric_limits<int>::max())
                        throw std::invalid_argument("ownership repair stage exceeds int");
                    repair.stage=static_cast<int>(stage);
                    repair.time=std::bit_cast<double>(*in++);
                    for (double& value:repair.position) value=std::bit_cast<double>(*in++);
                }
            }
            prepared->CompleteOwnershipMigration();
            prepared->ActivateForFinalization(proposed_global);
            notify(LeafTransferPhase::TopologyActivated);
            // Ghost values are unchanged because this transaction changes only ownership.
            const auto witness=scheduler_clock.next_publication();
            ledger=std::make_unique<StateResidencyLedger>(candidate->reconciliation().epoch);
            for (auto handle:handles) {
                ledger->register_block(handle,witness.version,witness.completion);
                ledger->publish_ghost({handle,StateSlot::Current},ExecutionSide::Host,
                    witness.version,witness.completion);
            }
            transaction->mark_ready();
            notify(LeafTransferPhase::ReadyToPublish);
        });
        (void)topology_registry.commit_after_success(std::move(*candidate),
            [&](const auto&) {
                transaction->commit_after_success(
                    [](const auto&) {return 0;},
                    [](const auto&,int&) {},
                    [&](int&&) noexcept {
                        prepared->PublishNoexcept();
                        stage_handles.swap(handles);
                        residency_ledger.swap(ledger);
                        amr_ctrl.PublishActiveHandlesNoexcept(stage_handles);
                    });
            },[&](std::exception_ptr failure) {
                prepare([&] {if (failure) std::rethrow_exception(failure);});
            });
        // Publication is noexcept on every rank; old sender stays alive through acknowledgement.
        check(MPI_Barrier(MPI_COMM_WORLD));
        prepared->ReleaseRetiredNoexcept();
    } catch (...) {
        if (prepared) prepared->AbortNoexcept();
        // All prior transport has completed. Preserve monotonic identities after rejection.
        std::array<std::uint64_t,3> local{next_amr_transaction_id,scheduler_clock.last_token(),scheduler_clock.last_version()},high{};
        check(MPI_Allreduce(local.data(),high.data(),3,MPI_UINT64_T,MPI_MAX,MPI_COMM_WORLD));
        next_amr_transaction_id=high[0];
        scheduler_clock.retain_high_watermarks(high[1],high[2]);
        throw;
    }
#else
    (void)source;(void)destination_rank;(void)observer;
    throw std::logic_error("ownership transfer requires ARCH_ENABLE_MPI=ON");
#endif
}


/** Collective first-level Host regrid using the original selection, pool and
 * conservative migration. Unchanged local ranks still join the global epoch.
 * Families must be colocated; deeper distributed ripple propagation is separate.
 */
bool DriverRuntime::execute_distributed_regrid(int step,double time,bool jeans_repair_only,
    const std::function<void(RegridPhase)>& observer)
{
#if ARCH_MPI_ENABLED
    using namespace amr::flux_mpi_detail;
    const auto [rank,size]=require_world();
    const auto notify=[&](RegridPhase phase) {if(observer)observer(phase);};
    state::StateVersion accepted{};
    std::vector<std::uint64_t> controls;
    prepare([&] {
        if(size<2 || rank!=amr_ctrl.tree->PartitionRank() || size!=amr_ctrl.tree->PartitionSize()
            || compute_backend || host_hydro_attempt_ || config.grid.geometry!="cartesian"
            || config.grid.dim!=2 || config.amr.lrefinemin!=0 || config.amr.lrefinemax!=1
            || config.physics.burn.use_burn || config.physics.diffusion.use_diffusion
            || config.physics.gravity.type!="none" || config.amr.refine_on_jeans
            || jeans_repair_only || bc_handler.has_user())
            throw std::logic_error("distributed regrid requires first-level Cartesian Host Hydro");
        if(step!=ctrl.step_count || !std::isfinite(time) || time!=ctrl.t_current
            || next_amr_transaction_id==std::numeric_limits<std::uint64_t>::max())
            throw std::invalid_argument("distributed regrid is not at the accepted controller boundary");
        topology_registry.validate_committed_snapshot(observe_topology());
        if(stage_handles.empty())throw std::logic_error("distributed regrid requires a nonempty rank");
        accepted=current_interior_version();
        for(auto handle:stage_handles)
            residency_ledger->require_readable({handle,StateSlot::Current},
                {ExecutionSide::Host,accepted,true,true});
        amr_ctrl.flux_register.RequireOwnershipTransferBoundary();
        std::map<amr::LogicalBlockKey,int> family_owner;
        for(const auto& leaf:topology_registry.blocks()) {
            if(leaf.logical.level>1)throw std::logic_error("distributed regrid requires levels zero/one");
            if(leaf.logical.level==1) {
                const amr::LogicalBlockKey parent{leaf.logical.dimension,0,
                    leaf.logical.logical_x1>>1,leaf.logical.logical_x2>>1,0};
                const auto [it,inserted]=family_owner.emplace(parent,leaf.owner_rank);
                if(!inserted && it->second!=leaf.owner_rank)
                    throw std::logic_error("distributed regrid requires colocated sibling families");
            }
        }
        for(int id:amr_ctrl.tree->GetActiveBlocks())
            for(auto member:{&amr::Block::fluid_state,&amr::Block::state_next,&amr::Block::state_scratch}) {
                const auto& state=amr_ctrl.pool->GetBlock(id).*member;
                if(state.diffusion_boundary || state.boundary_flux_capture)
                    throw std::logic_error("distributed regrid cannot borrow boundary attachments");
            }
        regrid_measurements.reserve(regrid_measurements.size()+1);
        const auto& a=config.amr;
        controls={static_cast<std::uint64_t>(config.grid.amr_max_blocks),
            static_cast<std::uint64_t>(a.lrefinemin),static_cast<std::uint64_t>(a.lrefinemax),
            std::bit_cast<std::uint64_t>(a.refine_threshold),std::bit_cast<std::uint64_t>(a.derefine_threshold),
            std::bit_cast<std::uint64_t>(config.numerics.sml_rho),
            std::bit_cast<std::uint64_t>(config.numerics.min_eint),
            a.refine_on_rho,a.refine_on_p,a.refine_on_temp,a.refine_on_velx,a.refine_on_vely,
            a.refine_on_velz,a.refine_on_eng,a.refine_on_vorticity,a.refine_on_div_v,
            a.refine_on_entropy,a.refine_on_enuc,a.refine_on_species,a.refine_all_species};
        const auto species=amr_ctrl.tree->RefinementSpecies();
        controls.push_back(species.size());
        for(int index:species)controls.push_back(static_cast<std::uint64_t>(index));
        for(const auto* face:{&config.grid.x1l_boundary_type,&config.grid.x1r_boundary_type,
            &config.grid.x2l_boundary_type,&config.grid.x2r_boundary_type,
            &config.grid.x3l_boundary_type,&config.grid.x3r_boundary_type}) {
            controls.push_back(face->size());
            for(unsigned char ch:*face)controls.push_back(ch);
        }
    });
    agree(signature(amr_ctrl.tree->GetRootGrid(),specs.count(),topology_registry.epoch(),
        5,next_amr_transaction_id,time,0));
    std::array<std::uint64_t,21> identity{
        static_cast<std::uint64_t>(step),accepted.value,scheduler_clock.last_token(),
        scheduler_clock.last_version(),next_amr_transaction_id,controls.size(),
        std::bit_cast<std::uint64_t>(ctrl.dt_old)};
    agree(identity);
    for(std::size_t offset=0;offset<controls.size();offset+=identity.size()) {
        identity.fill(0);
        std::copy_n(controls.begin()+offset,std::min(identity.size(),controls.size()-offset),identity.begin());
        agree(identity);
    }

    std::optional<amr::AmrTree::PreparedRegrid> prepared;
    std::optional<topology::TopologyIdentityRegistry::Candidate> candidate;
    std::optional<amr::TopologyTransaction> transaction;
    std::vector<TopologyObservation> observations;
    std::vector<amr::BlockTopology> proposed_global;
    std::vector<amr::BlockHandle> handles;
    std::vector<RegridSourceBackup> backups;
    std::optional<BCHandler::StageContextSnapshot> boundary_context;
    std::unique_ptr<StateResidencyLedger> ledger;
    scheduler::PublicationWitness witness{};
    try {
        prepare([&] {
            prepared.emplace(amr_ctrl.tree->SelectRegrid(config));
            notify(RegridPhase::Selected);
        });
        std::array<std::uint64_t,4> local{
            static_cast<std::uint64_t>(amr_ctrl.pool->GetCapacity()),
            static_cast<std::uint64_t>(amr_ctrl.pool->GetNumActiveBlocks()),
            prepared->required_slots(),prepared->proposed_leaf_count()},global{};
        check(MPI_Allreduce(local.data(),global.data(),4,MPI_UINT64_T,MPI_SUM,MPI_COMM_WORLD));
        prepare([&] {
            if(config.grid.amr_max_blocks<=0 || global[0]>static_cast<std::uint64_t>(config.grid.amr_max_blocks)
                || global[1]+global[2]>static_cast<std::uint64_t>(config.grid.amr_max_blocks)
                || global[3]>static_cast<std::uint64_t>(config.grid.amr_max_blocks))
                throw std::runtime_error("distributed regrid exceeds global max_blocks including the temporary peak");
            if(amr_ctrl.pool->GetNumReservedBlocks()!=0
                || local[1]!=stage_handles.size() || prepared->proposed_leaf_count()==0)
                throw std::logic_error("distributed regrid pool has foreign storage or an empty candidate rank");
            prepared->ConstructCandidates();
            notify(RegridPhase::Constructed);
            observations=observe_blocks(prepared->proposed_active_blocks());
        });
        observations=topology::collect_topology_observations(observations,rank,size);
        prepare([&] {
            candidate.emplace(topology_registry.stage_reconciliation(observations));
            proposed_global=candidate->blocks();
            handles=candidate->reconciliation().handles_in_observation_order;
            if(candidate->reconciliation().topology_changed==false
                && (prepared->topology_changed() || handles!=stage_handles))
                throw std::logic_error("distributed no-change reconciliation drifted");
        });
        if(!candidate->reconciliation().topology_changed) {
            (void)topology_registry.commit_after_success(std::move(*candidate),
                [&](const auto&) {prepared->PublishNoChangeNoexcept();},
                [&](std::exception_ptr failure) {prepare([&] {if(failure)std::rethrow_exception(failure);});});
            check(MPI_Barrier(MPI_COMM_WORLD));
            return false;
        }
        prepare([&] {
            const amr::AmrPlanScope scope{next_amr_transaction_id++,topology_registry.epoch(),
                candidate->reconciliation().epoch};
            prepared->BuildMigrationPlans(stage_handles,handles,scope);
            transaction.emplace(scope.transaction_id,scope.from_epoch,scope.to_epoch);
            transaction->begin_migration();
            transaction->require_scope(prepared->prolongation_plan());
            transaction->require_scope(prepared->restriction_plan());
            prepared->ExecuteMigration();
            notify(RegridPhase::Migrated);
            // Boundary completion writes unchanged leaves too. Lease the original
            // arrays before activating candidates, not a second MPI field store.
            boundary_context.emplace(bc_handler.snapshot_stage_context());
            backups.reserve(amr_ctrl.tree->GetActiveBlocks().size());
            for(int id:amr_ctrl.tree->GetActiveBlocks())
                backups.emplace_back(id,amr_ctrl.pool->GetBlock(id));
            witness=scheduler_clock.next_publication();
            ledger=std::make_unique<StateResidencyLedger>(candidate->reconciliation().epoch);
            for(auto handle:handles)ledger->register_block(handle,witness.version,witness.completion);
            prepared->ActivateForFinalization(proposed_global);
            notify(RegridPhase::TopologyActivated);
        });
        std::optional<StageExecutionContext> context;
        std::optional<scheduler::ScopedStageBinding> binding;
        prepare([&] {
            context.emplace(ExecutionSide::Host,*ledger,scheduler_clock);
            context->hydro_rank_completion=[this](std::exception_ptr failure) {
                amr_ctrl.CompleteDistributedHydroPhase(failure);
            };
            for(int id:amr_ctrl.tree->GetActiveBlocks())
                (void)bc_handler.logical_plan(amr_ctrl.pool->GetBlock(id).grid);
            binding.emplace(*context,handles);
        });
        TimeIntegration::synchronize_domain_boundary(amr_ctrl,bc_handler,&amr::Block::fluid_state,handles);
        prepare([&] {
            (void)scheduler::complete_boundary(*context,handles,StateSlot::Current,witness.version,
                [](StateSlot,state::StateVersion,state::CompletionToken token) {return token;});
            for(auto handle:handles)ledger->require_readable({handle,StateSlot::Current},
                {ExecutionSide::Host,witness.version,true,true});
            notify(RegridPhase::GhostsSynchronized);
            transaction->mark_ready();
            notify(RegridPhase::ReadyToPublish);
        });
        binding.reset();
        (void)topology_registry.commit_after_success(std::move(*candidate),
            [&](const auto&) {
                transaction->commit_after_success(
                    [](const auto&) {return 0;},[](const auto&,int&) {},
                    [&](int&&) noexcept {
                        prepared->PublishNoexcept();
                        stage_handles.swap(handles);
                        residency_ledger.swap(ledger);
                        amr_ctrl.PublishActiveHandlesNoexcept(stage_handles);
                    });
            },[&](std::exception_ptr failure) {
                prepare([&] {if(failure)std::rethrow_exception(failure);});
            });
        check(MPI_Barrier(MPI_COMM_WORLD));
        prepared->ReleaseRetiredNoexcept();
        return true;
    } catch(...) {
        for(auto& backup:backups)backup.restore(amr_ctrl.pool->GetBlock(backup.pool_index));
        if(boundary_context)bc_handler.restore_stage_context_noexcept(*boundary_context);
        if(prepared)prepared->AbortNoexcept();
        std::array<std::uint64_t,3> local{next_amr_transaction_id,scheduler_clock.last_token(),
            scheduler_clock.last_version()},high{};
        check(MPI_Allreduce(local.data(),high.data(),3,MPI_UINT64_T,MPI_MAX,MPI_COMM_WORLD));
        next_amr_transaction_id=high[0];scheduler_clock.retain_high_watermarks(high[1],high[2]);
        throw;
    }
#else
    (void)step;(void)time;(void)jeans_repair_only;(void)observer;
    throw std::logic_error("distributed regrid requires ARCH_ENABLE_MPI=ON");
#endif
}

/** Evaluate the original Device source flags before topology preparation.
 * Workflow: select indicators -> complete actual Current ghosts -> evaluate
 * original summaries/JENS -> reject finest deficits -> publish only flags.
 */
void DriverRuntime::evaluate_device_regrid_indicators(bool jeans_repair_only)
{
    const auto old_active=amr_ctrl.tree->GetActiveBlocks();
    (void)amr::indicator::make_selection(
        config.amr,config.grid.dim,amr_ctrl.tree->RefinementSpecies(),
        GridMetrics::resolve_public_chart(config.grid.geometry,config.grid.dim));
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
        topology::collect_topology_observations(observe_blocks(prepared.proposed_active_blocks()),
            amr_ctrl.tree->PartitionRank(), amr_ctrl.tree->PartitionSize()));
    const auto& proposed = topology_candidate.reconciliation();
    const auto proposed_blocks = topology_candidate.blocks();
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
            // Candidate Tree membership is authentic only after activation.
            // Failure still rolls this unpublished topology/store back together.
            for (auto& binding : payload.bindings)
                binding.hydro_boundary=bc_handler.hydro_physical_faces(
                    *amr_ctrl.tree,*binding.block);
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
                                amr_ctrl.tree->UpdateNeighbors(config, proposed_blocks);
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
bool DriverRuntime::perform_regrid(int step, double time, bool jeans_repair_only,
    const std::function<void(RegridPhase)>& observer)
{
    return perform_regrid_impl(step,time,jeans_repair_only,observer);
}
bool DriverRuntime::perform_regrid_impl(int step,double time,bool jeans_repair_only,
    const std::function<void(RegridPhase)>& observer)
{
    if(host_hydro_attempt_)throw std::logic_error("Cannot regrid during Host Hydro attempt");
    const auto started = std::chrono::steady_clock::now();
    const auto before = compute_backend ? compute_backend->counters()
        : arch::backend::BackendCounters{};
    const auto old_blocks = stage_handles.size();
    if(amr_ctrl.tree->PartitionSize()==1 && observer)
        throw std::logic_error("collective regrid observers require a distributed topology");
    const bool changed = amr_ctrl.tree->PartitionSize()>1
        ? execute_distributed_regrid(step,time,jeans_repair_only,observer)
        : execute_regrid(jeans_repair_only);
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
