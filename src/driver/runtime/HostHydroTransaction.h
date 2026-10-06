/**
 * @file HostHydroTransaction.h
 * @brief Runtime-owned, internal Host/RZ rejection scope.
 *
 * Workflow:
 * 1. Validate Runtime/context/topology and fixed-allocation Hydro ownership.
 * 2. Allocate all backups before ghost, observer, register or stage mutations.
 * 3. Compose tentative source, repair and boundary receipts through advance_hydro.
 * 4. Commit after final rotation/reflux, or invalidate the source and restore
 *    the original allocations, slot mapping and complete owner metadata.
 *
 * No physical formula, time tableau, floor or acceptance tolerance is changed.
 * This internal qualification scope does not promote public RZ or Device use.
 */
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "amr/AMRControl.h"
#include "data/FluidState.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "numerics/integrator/IHydroSolver.h"
#include "physics/boundary/PhysicalBoundaryHandler.h"
#include "physics/species/Species.h"

namespace arch::driver {

/** Explicit verification profile; never selected by SimConfig or capability discovery. */
enum class HostHydroQualification { Production, NativeRzRollback };

/** All fallible setup belongs to construction; no allocation is needed to reject. */
class HostHydroTransaction final {
    using Field = std::vector<double> FluidState::*;
    static constexpr std::array<Field,7> fields_{
        &FluidState::rho,&FluidState::mom_u,&FluidState::mom_v,
        &FluidState::mom_w,&FluidState::eng,&FluidState::enuc_rate,
        &FluidState::mass_fractions};
    struct FieldLease { const double* address{}; std::size_t size{}; };
    struct SlotBackup {
        FluidState values;
        std::array<FieldLease,7> leases{};
        explicit SlotBackup(const FluidState& live) : values(live) {
            for(std::size_t f=0;f<fields_.size();++f) {
                const auto& v=live.*fields_[f]; leases[f]={v.data(),v.size()};
            }
        }
        /** Match allocation groups, not a version number or copied rho values. */
        bool owns(const FluidState& live) const noexcept {
            for(std::size_t f=0;f<fields_.size();++f) {
                const auto& v=live.*fields_[f];
                if(v.data()!=leases[f].address||v.size()!=leases[f].size)return false;
            }
            return true;
        }
        /** Restore values into the original arrays; ephemeral receipts can swap. */
        void restore(FluidState& live) noexcept {
            if(!owns(live))std::terminate();
            for(Field field:fields_)
                std::copy((values.*field).begin(),(values.*field).end(),(live.*field).begin());
            live.n_species_=values.n_species_; live.block_total_size_=values.block_total_size_;
            std::swap(live.stage_repairs,values.stage_repairs);
            live.diffusion_boundary=values.diffusion_boundary;
            live.boundary_flux_capture=values.boundary_flux_capture;
        }
    };
    struct BlockBackup {
        int id;
        amr::Block* address;
        const Grid* grid_address;
        std::array<SlotBackup,3> slots;
        BlockBackup(int pool_id,amr::Block& b)
            : id(pool_id),address(&b),grid_address(&b.grid),
              slots{SlotBackup(b.fluid_state),SlotBackup(b.state_next),SlotBackup(b.state_scratch)} {}
    };
    struct CaptureBackup {
        std::shared_ptr<boundary::BoundaryFluxCaptureStorage> owner;
        boundary::BoundaryFluxCaptureStorage values;
        explicit CaptureBackup(std::shared_ptr<boundary::BoundaryFluxCaptureStorage> p)
            : owner(std::move(p)),values(*owner) {}
        /** Keep the pointee and alias graph; reuse existing plane allocations when possible. */
        void restore() noexcept {
            for(std::size_t f=0;f<6;++f) {
                auto restore_plane=[](std::vector<double>& live,std::vector<double>& saved) noexcept {
                    if(live.size()==saved.size())std::copy(saved.begin(),saved.end(),live.begin());
                    else live.swap(saved); // Only ephemeral observer planes, never fluid leases.
                };
                restore_plane(owner->stage[f],values.stage[f]);
                restore_plane(owner->initial[f],values.initial[f]);
            }
            owner->weight=values.weight;owner->initial_weight=values.initial_weight;
            owner->save_initial=values.save_initial;
        }
    };
    DriverRuntime& runtime_;
    scheduler::StageExecutionContext& context_;
    scheduler::StageExecutionContext context_before_;
    scheduler::HydroStagePreparation* preparation_;
    std::shared_ptr<amr::MemoryPool> pool_;
    std::shared_ptr<amr::AmrTree> tree_;
    std::vector<int> active_;
    std::vector<amr::BlockHandle> handles_;
    std::vector<backend::StorageGeneration> storage_;
    const amr::BlockHandle* handles_address_;
    const backend::StorageGeneration* storage_address_;
    const scheduler::StageBinding* binding_address_;
    const amr::BlockHandle* binding_handles_address_;
    std::size_t binding_handles_size_;
    state::StateResidencyLedger::HostSnapshot ledger_;
    amr::FluxRegister::HostSnapshot flux_;
    scheduler::MonotonicSchedulerClock clock_;
    BCHandler::StageContextSnapshot boundary_context_;
    std::vector<BlockBackup> blocks_;
    std::vector<CaptureBackup> captures_;
    state::RepairBudget accepted_repairs_,tentative_repairs_;
    amr::TopologyEpoch boundary_epoch_;
    std::vector<backend::BoundaryFluxPlanes> surface_layout_;
    std::vector<double> hydro_before_,diffusion_before_,rkl_previous_,rkl_older_,hydro_tentative_;
    backend::BackendCounters observer_before_;
    std::array<DriverRuntime::UserBoundaryStamp,3> stamps_;
    bool committed_=false,leased_=false;

    /** Fail before dereferencing or allocating against an unsupported owner. */
    static DriverRuntime& preflight(DriverRuntime& runtime,
        scheduler::StageExecutionContext& context,const Numerics::IHydroSolver& hydro) {
        if(!runtime.residency_ledger||!runtime.amr_ctrl.pool||!runtime.amr_ctrl.tree
            ||runtime.host_hydro_transaction_||runtime.compute_backend
            ||!std::isfinite(context.step_start_time)||!std::isfinite(context.step_dt)||context.step_dt<=0.
            ||(context.hydro_preparation&&!context.hydro_preparation->supports_host_macro_step_journal())
            ||context.side!=state::ExecutionSide::Host
            ||&context.ledger!=runtime.residency_ledger.get()
            ||&context.clock!=&runtime.scheduler_clock
            ||runtime.geometry_semantics_!=GridMetrics::GeometrySemantics::AxisymmetricRz
            ||hydro.geometry_semantics()!=runtime.geometry_semantics_
            ||hydro.host_storage_contract()!=Numerics::HostHydroStorageContract::FixedExtentSlotPermutation)
            throw std::logic_error("Host/RZ rollback requires its exact quiescent Runtime and Hydro owner");
        const auto& binding=scheduler::current_stage_binding();
        if(&binding.context!=&context||binding.handles.data()!=runtime.stage_handles.data()
            ||binding.handles.size()!=runtime.stage_handles.size()
            ||!std::equal(binding.handles.begin(),binding.handles.end(),runtime.stage_handles.begin()))
            throw std::logic_error("Host/RZ rollback requires exact bound stage context and borrowed Runtime handles");
        return runtime;
    }
    /** Source invalidation precedes restoring any old field or lease. */
    void discard_source_noexcept() noexcept {
        if(!preparation_)return;
        try {preparation_->invalidate();}catch(...) {std::terminate();}
        preparation_->discard_macro_step();
    }
    /** Check identities without issuing generations or changing accepted data. */
    void require_owner() const {
        // RK3 reads the actual thread-local binding, not this function's context argument.
        // Check original borrowed span identity and content before touching any owner.
        const auto& binding=scheduler::current_stage_binding();
        if(&binding!=binding_address_||&binding.context!=&context_
            ||binding.handles.data()!=binding_handles_address_||binding.handles.size()!=binding_handles_size_
            ||binding.handles.size()!=handles_.size()
            ||!std::equal(binding.handles.begin(),binding.handles.end(),handles_.begin()))
            throw std::logic_error("Host Hydro transaction actual stage binding changed");
        if(runtime_.compute_backend||context_.side!=state::ExecutionSide::Host
            || &context_.ledger!=runtime_.residency_ledger.get()
            || &context_.clock!=&runtime_.scheduler_clock
            || runtime_.amr_ctrl.pool!=pool_||runtime_.amr_ctrl.tree!=tree_
            || runtime_.stage_handles!=handles_||runtime_.backend_storage!=storage_
            ||runtime_.stage_handles.data()!=handles_address_||runtime_.backend_storage.data()!=storage_address_
            ||context_.step_start_time!=context_before_.step_start_time||context_.step_dt!=context_before_.step_dt
            || runtime_.amr_ctrl.tree->GetActiveBlocks()!=active_
            || context_.hydro_preparation!=preparation_
            || bool(context_.post_boundary_acceptance)
                !=bool(context_before_.post_boundary_acceptance))
            throw std::logic_error("Host Hydro transaction owner/frame changed");
        for(const auto& b:blocks_) {
            auto& live=pool_->GetBlock(b.id);
            if(&live!=b.address||&live.grid!=b.grid_address)
                throw std::logic_error("Host Hydro transaction block/grid identity changed");
            const std::array<const FluidState*,3> slots{&live.fluid_state,&live.state_next,&live.state_scratch};
            std::array<bool,3> found{};
            for(const auto* slot:slots) {
                int which=-1;
                for(int s=0;s<3;++s)if(b.slots[s].owns(*slot))which=s;
                if(which<0||found[which])throw std::logic_error("Host Hydro violated fixed allocation contract");
                found[which]=true;
                if(slot->GetNumSpecies()!=b.slots[which].values.GetNumSpecies()
                    ||slot->block_total_size_!=b.slots[which].values.block_total_size_)
                    throw std::logic_error("Host Hydro changed fixed field layout");
            }
        }
    }
    /** Restore saved context callbacks by noexcept swap; their referenced owners survive. */
    void restore_context() noexcept {
        context_.side=context_before_.side;context_.hydro_preparation=context_before_.hydro_preparation;
        context_.step_start_time=context_before_.step_start_time;context_.step_dt=context_before_.step_dt;
        context_.boundary_start_time=context_before_.boundary_start_time;
        context_.boundary_step_dt=context_before_.boundary_step_dt;
        context_.hydro_acceptance.swap(context_before_.hydro_acceptance);
        context_.physical_boundary_preparation.swap(context_before_.physical_boundary_preparation);
        context_.hydro_flux_capture_begin.swap(context_before_.hydro_flux_capture_begin);
        context_.hydro_flux_capture_accept.swap(context_before_.hydro_flux_capture_accept);
        context_.rkl_flux_capture_begin.swap(context_before_.rkl_flux_capture_begin);
        context_.rkl_flux_capture_accept.swap(context_before_.rkl_flux_capture_accept);
        context_.rkl_acceptance.swap(context_before_.rkl_acceptance);
        context_.post_boundary_acceptance.swap(context_before_.post_boundary_acceptance);
    }
    /** End only this exact owner lease, without unbinding another transaction. */
    void release() noexcept {
        if(!leased_)return;
        if(runtime_.host_hydro_transaction_!=this)std::terminate();
        runtime_.amr_ctrl.flux_register.release_host_snapshot(flux_);
        runtime_.residency_ledger->release_host_snapshot(ledger_);
        runtime_.tentative_hydro_boundary_budget_=nullptr;
        runtime_.host_hydro_transaction_=nullptr;leased_=false;
    }
public:
    /** Read-only exact owner witness; used at a quiescent boundary for scoped negatives.
     * It does not own a transaction or imply scientific capability acceptance. */
    struct OwnerWitness {
        std::shared_ptr<amr::MemoryPool> pool;
        std::shared_ptr<amr::AmrTree> tree;
        std::vector<int> active;
        std::vector<amr::BlockHandle> handles;
        std::vector<backend::StorageGeneration> storage;
        const amr::BlockHandle* handles_address;
        const backend::StorageGeneration* storage_address;
        state::StateResidencyLedger::HostSnapshot ledger;
        amr::FluxRegister::HostSnapshot flux;
        std::uint64_t token,version;
        BCHandler::StageContextSnapshot boundary;
        state::RepairBudget repairs;
        amr::TopologyEpoch budget_epoch;
        std::vector<backend::BoundaryFluxPlanes> layout;
        std::vector<double> hydro,diffusion,previous,older;
        backend::BackendCounters counters;
        std::array<DriverRuntime::UserBoundaryStamp,3> stamps;
        state::ExecutionSide side;
        scheduler::HydroStagePreparation* preparation;
        double start,dt,boundary_start,boundary_dt;
        std::array<bool,8> callbacks;
    };
    /** Allocate a diagnostic witness only when explicitly requested by owner verification. */
    static OwnerWitness snapshot_owner(DriverRuntime& r,const scheduler::StageExecutionContext& c) {
        return {r.amr_ctrl.pool,r.amr_ctrl.tree,r.amr_ctrl.tree->GetActiveBlocks(),r.stage_handles,r.backend_storage,
            r.stage_handles.data(),r.backend_storage.data(),r.residency_ledger->snapshot_host(),
            r.amr_ctrl.flux_register.snapshot_host(),r.scheduler_clock.last_token(),r.scheduler_clock.last_version(),
            r.bc_handler.snapshot_stage_context(),r.repair_budget(),r.boundary_budget_epoch_,r.boundary_surface_layout_,
            r.hydro_boundary_budget_,r.diffusion_boundary_budget_,r.boundary_rkl_previous_,r.boundary_rkl_older_,
            r.boundary_observer_operations_,r.user_boundary_stamps_,c.side,c.hydro_preparation,c.step_start_time,
            c.step_dt,c.boundary_start_time,c.boundary_step_dt,{bool(c.hydro_acceptance),
            bool(c.physical_boundary_preparation),bool(c.hydro_flux_capture_begin),bool(c.hydro_flux_capture_accept),
            bool(c.rkl_flux_capture_begin),bool(c.rkl_flux_capture_accept),bool(c.rkl_acceptance),
            bool(c.post_boundary_acceptance)}};
    }
    /** Compare every actual mutable owner; field/capture values are checked separately by the fixture. */
    static bool owner_matches(DriverRuntime& r,const scheduler::StageExecutionContext& c,const OwnerWitness& s) {
        const auto bits=[](double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);};
        const auto array_bits=[&](const std::vector<double>& a,const std::vector<double>& b) {
            return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),bits);
        };
        const auto& repairs=r.repair_budget();
        if(r.amr_ctrl.pool!=s.pool||r.amr_ctrl.tree!=s.tree||r.amr_ctrl.tree->GetActiveBlocks()!=s.active
            ||r.stage_handles!=s.handles||r.backend_storage!=s.storage||r.stage_handles.data()!=s.handles_address
            ||r.backend_storage.data()!=s.storage_address||!r.residency_ledger->host_snapshot_matches(s.ledger)
            ||!r.amr_ctrl.flux_register.host_snapshot_matches(s.flux)||r.scheduler_clock.last_token()!=s.token
            ||r.scheduler_clock.last_version()!=s.version||!r.bc_handler.stage_context_matches(s.boundary)
            ||!array_bits(repairs.values,s.repairs.values)||repairs.semantics!=s.repairs.semantics
            ||repairs.block_uid!=s.repairs.block_uid||repairs.stage!=s.repairs.stage||!bits(repairs.time,s.repairs.time)
            ||!std::equal(std::begin(repairs.position),std::end(repairs.position),std::begin(s.repairs.position),bits)
            ||r.boundary_budget_epoch_!=s.budget_epoch||r.boundary_surface_layout_.size()!=s.layout.size()
            ||!array_bits(r.hydro_boundary_budget_,s.hydro)||!array_bits(r.diffusion_boundary_budget_,s.diffusion)
            ||!array_bits(r.boundary_rkl_previous_,s.previous)||!array_bits(r.boundary_rkl_older_,s.older)
            ||r.boundary_observer_operations_!=s.counters||c.side!=s.side||c.hydro_preparation!=s.preparation
            ||!bits(c.step_start_time,s.start)||!bits(c.step_dt,s.dt)||!bits(c.boundary_start_time,s.boundary_start)
            ||!bits(c.boundary_step_dt,s.boundary_dt)
            ||std::array<bool,8>{bool(c.hydro_acceptance),bool(c.physical_boundary_preparation),
                bool(c.hydro_flux_capture_begin),bool(c.hydro_flux_capture_accept),bool(c.rkl_flux_capture_begin),
                bool(c.rkl_flux_capture_accept),bool(c.rkl_acceptance),
                bool(c.post_boundary_acceptance)}!=s.callbacks)return false;
        for(std::size_t i=0;i<3;++i)
            if(r.user_boundary_stamps_[i].epoch!=s.stamps[i].epoch
                ||r.user_boundary_stamps_[i].revision!=s.stamps[i].revision)return false;
        for(std::size_t i=0;i<s.layout.size();++i) {
            const auto& a=r.boundary_surface_layout_[i];const auto& b=s.layout[i];
            if(a.block!=b.block)return false;
            for(std::size_t f=0;f<6;++f)
                if(!array_bits(a.stage[f],b.stage[f])||!array_bits(a.initial[f],b.initial[f]))return false;
        }
        return !r.host_hydro_transaction_&&!r.tentative_hydro_boundary_budget_;
    }
    HostHydroTransaction(DriverRuntime& runtime,scheduler::StageExecutionContext& context,
        const Numerics::IHydroSolver& hydro)
        : runtime_(preflight(runtime,context,hydro)),context_(context),context_before_(context),preparation_(context.hydro_preparation),
          pool_(runtime.amr_ctrl.pool),tree_(runtime.amr_ctrl.tree),
          active_(tree_->GetActiveBlocks()),handles_(runtime.stage_handles),storage_(runtime.backend_storage),
          handles_address_(runtime.stage_handles.data()),storage_address_(runtime.backend_storage.data()),
          binding_address_(&scheduler::current_stage_binding()),
          binding_handles_address_(binding_address_->handles.data()),binding_handles_size_(binding_address_->handles.size()),
          ledger_(runtime.residency_ledger->snapshot_host()),flux_(runtime.amr_ctrl.flux_register.snapshot_host()),
          clock_(runtime.scheduler_clock),boundary_context_(runtime.bc_handler.snapshot_stage_context()),
          accepted_repairs_(runtime.repair_budget()),tentative_repairs_(accepted_repairs_),
          boundary_epoch_(runtime.boundary_budget_epoch_),surface_layout_(runtime.boundary_surface_layout_),
          hydro_before_(runtime.hydro_boundary_budget_),diffusion_before_(runtime.diffusion_boundary_budget_),
          rkl_previous_(runtime.boundary_rkl_previous_),rkl_older_(runtime.boundary_rkl_older_),
          hydro_tentative_(hydro_before_),observer_before_(runtime.boundary_observer_operations_),
          stamps_(runtime.user_boundary_stamps_) {
        static_assert(std::is_nothrow_swappable_v<FluidState>);
        static_assert(std::is_nothrow_swappable_v<state::RepairBudget>);
        if(runtime.host_hydro_transaction_||runtime.compute_backend
            ||runtime.geometry_semantics_!=GridMetrics::GeometrySemantics::AxisymmetricRz
            ||hydro.host_storage_contract()!=Numerics::HostHydroStorageContract::FixedExtentSlotPermutation
            ||hydro.geometry_semantics()!=runtime.geometry_semantics_
            ||active_.empty()||active_.size()!=handles_.size()
            ||runtime.amr_ctrl.flux_register.GetNumSpecies()!=runtime.specs.count())
            throw std::logic_error("Host/RZ rollback qualification is unavailable or overlapping");
        const auto field_extent=static_cast<std::size_t>(state::RepairView::fixed_size+2*runtime.specs.count());
        if(accepted_repairs_.values.size()!=field_extent
            ||accepted_repairs_.semantics!=state::RepairSemantics::RzVolumeAngular)
            throw std::logic_error("Host/RZ accepted repair owner layout mismatch");
        if(!hydro_before_.empty()&&hydro_before_.size()!=static_cast<std::size_t>(6+runtime.specs.count()))
            throw std::logic_error("Host/RZ boundary receipt layout mismatch");
        blocks_.reserve(active_.size());captures_.reserve(3*active_.size());
        std::map<boundary::BoundaryFluxCaptureStorage*,bool> unique;
        for(std::size_t b=0;b<active_.size();++b) {
            runtime.residency_ledger->quiesce(handles_[b]);
            auto& block=pool_->GetBlock(active_[b]);
            blocks_.emplace_back(active_[b],block);
            for(const auto& slot:blocks_.back().slots) {
                const auto& s=slot.values;
                const auto cells=static_cast<std::size_t>(block.grid.GetTotalSize());
                if(s.rho.empty()||s.GetNumSpecies()!=runtime.specs.count()
                    ||s.block_total_size_!=static_cast<int>(cells))
                    throw std::logic_error("Host/RZ slot field/species extent mismatch");
                for(std::size_t f=0;f<fields_.size();++f)
                    if(slot.leases[f].size!=(f==6?cells*runtime.specs.count():cells))
                        throw std::logic_error("Host/RZ slot vector extent mismatch");
                if(s.boundary_flux_capture&&unique.emplace(s.boundary_flux_capture.get(),true).second)
                    captures_.emplace_back(s.boundary_flux_capture);
            }
        }
        require_owner();
        runtime.residency_ledger->freeze_host_snapshot(ledger_);
        try {runtime.amr_ctrl.flux_register.freeze_host_snapshot(flux_);}
        catch(...) {runtime.residency_ledger->release_host_snapshot(ledger_);throw;}
        runtime.host_hydro_transaction_=this;leased_=true;
        runtime.tentative_hydro_boundary_budget_=&hydro_tentative_;
        try {if(preparation_)preparation_->begin_macro_step();}
        catch(...) {discard_source_noexcept();release();throw;}
    }
    HostHydroTransaction(const HostHydroTransaction&)=delete;
    HostHydroTransaction& operator=(const HostHydroTransaction&)=delete;
    HostHydroTransaction(HostHydroTransaction&&)=delete;
    HostHydroTransaction& operator=(HostHydroTransaction&&)=delete;
    /** Driver receipt collection remains tentative until the macro-step succeeds. */
    state::RepairBudget& repair_receipts() noexcept {return tentative_repairs_;}
    /** Validate physical allocation groups after a stage, including partial rotations. */
    void validate_storage() const {require_owner();}
    /** Final preflight can throw; publication tail is allocation-free and noexcept-owned. */
    void commit() {
        require_owner();
        runtime_.residency_ledger->quiesce();
        if(preparation_)preparation_->commit_macro_step();
        std::swap(runtime_.repair_budget(),tentative_repairs_);
        runtime_.hydro_boundary_budget_.swap(hydro_tentative_);
        committed_=true;release();
    }
    /** Reject after all selected Host workers have joined; invalidate source first. */
    ~HostHydroTransaction() noexcept {
        if(!leased_)return;
        if(committed_) {release();return;}
        discard_source_noexcept();
        // Regrid/backend mutations are forbidden by Runtime entry guards. A solver
        // violating its fixed-allocation declaration is fatal, never silently repaired.
        if(runtime_.amr_ctrl.pool!=pool_||runtime_.amr_ctrl.tree!=tree_
            ||runtime_.stage_handles!=handles_||tree_->GetActiveBlocks()!=active_)std::terminate();
        for(auto& b:blocks_) {
            auto& block=pool_->GetBlock(b.id);
            std::array<FluidState*,3> live{&block.fluid_state,&block.state_next,&block.state_scratch};
            for(int destination=0;destination<3;++destination) {
                int original=-1;
                for(int location=destination;location<3;++location)
                    if(b.slots[destination].owns(*live[location]))original=location;
                if(original<0)std::terminate();
                if(original!=destination)std::swap(*live[destination],*live[original]);
            }
            for(int s=0;s<3;++s)b.slots[s].restore(*live[s]);
        }
        for(auto& capture:captures_)capture.restore();
        runtime_.boundary_budget_epoch_=boundary_epoch_;
        runtime_.boundary_surface_layout_.swap(surface_layout_);
        runtime_.hydro_boundary_budget_.swap(hydro_before_);
        runtime_.diffusion_boundary_budget_.swap(diffusion_before_);
        runtime_.boundary_rkl_previous_.swap(rkl_previous_);
        runtime_.boundary_rkl_older_.swap(rkl_older_);
        runtime_.boundary_observer_operations_=observer_before_;
        runtime_.user_boundary_stamps_=stamps_;
        std::swap(runtime_.repair_budget(),accepted_repairs_);
        runtime_.bc_handler.restore_stage_context_noexcept(boundary_context_);
        runtime_.amr_ctrl.flux_register.restore_host_snapshot_noexcept(flux_);
        runtime_.residency_ledger->restore_host_snapshot_noexcept(ledger_);
        runtime_.scheduler_clock=clock_;
        restore_context();release();
    }
};
} // namespace arch::driver
