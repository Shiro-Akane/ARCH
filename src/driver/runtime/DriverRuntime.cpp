/**
 * @file DriverRuntime.cpp
 * @brief Own live mesh, state residency and backend resources for the integration loop.
 *
 * Workflow:
 * 1. Borrow resolved configuration, species and the actual run EOS.
 * 2. Stage topology/ledger identity before real physical BC and halo exchange.
 * 3. For native RZ, preflight the whole Host domain and validate the shared
 *    thermodynamic closure against the actual EOS before ghost publication.
 * 4. Publish completed topology/state identities to the next scheduled stage.
 */

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "driver/runtime/DriverRuntime.h"

#include "amr/AMRControl.h"
#include "driver/DriverUtils.h"
#include "driver/runtime/HostHydroTransaction.h"
#include "driver/schedule/DriverControl.h"
#include "numerics/diffusion/DiffFunction.h"
#include "numerics/integrator/TimeIntegratorHelper.h"

namespace arch::driver {
/** Return the shared floor-repair budget for the active run. */
state::RepairBudget& DriverRuntime::repair_budget() { return ctrl.repairs; }
using scheduler::StageExecutionContext;
using state::ExecutionSide;
using state::StateResidencyLedger;
using state::StateSlot;
using topology::LogicalBlockIdentity;
using topology::TopologyObservation;
/** Freeze only layout/geometry identity, never a second copy of the fluid arrays. */
namespace {
struct NativeBoundaryPatch {
    int pool_index;
    const amr::Block* block;
    const Grid* grid;
    int level;
    std::array<std::uint32_t, 3> logical;
    std::array<int, 8> layout;
    std::array<double, 9> coordinates;
    GridMetrics::DyadicGridIdentity provenance;
};

/** Capture the actual active grid layout used by one candidate handle. */
NativeBoundaryPatch native_boundary_patch(int id, const amr::Block& block)
{
    const auto& grid = block.grid;
    return {id, &block, &grid, block.level,
        {block.logical_x1, block.logical_x2, block.logical_x3},
        {grid.dim, grid.ng, grid.stride_y, grid.stride_z, grid.total_size,
         grid.nblockx1, grid.nblockx2, grid.nblockx3},
        {grid.x1_min, grid.x1_max, grid.x2_min, grid.x2_max,
         grid.x3_min, grid.x3_max, grid.dx1, grid.dx2, grid.dx3},grid.dyadic_identity};
}

/** Compare real pool/grid identity and geometry before touching the EOS. */
bool native_boundary_patch_matches(const NativeBoundaryPatch& before,
    int id, const amr::Block& block)
{
    const auto after = native_boundary_patch(id, block);
    return before.pool_index == after.pool_index && before.block == after.block
        && before.grid == after.grid && before.level == after.level
        && before.logical == after.logical && before.layout == after.layout
        && before.coordinates == after.coordinates
        && GridMetrics::equal_identity(before.provenance,after.provenance)
        && block.id == id && block.active
        && block.grid.dim == 2 && block.grid.geometry == "cylindrical";
}

/** Preflight all selected vectors without recovering a single thermodynamic cell. */
void require_native_boundary_layout(const FluidState& fluid,
    const Grid& grid, int species)
{
    const int signed_cells = grid.GetTotalSize();
    if (signed_cells <= 0 || species < 0
        || fluid.block_total_size_ != signed_cells || fluid.GetNumSpecies() != species)
        throw std::logic_error("Native RZ boundary slot/species extent mismatch");
    const auto cells = static_cast<std::size_t>(signed_cells);
    for (const auto* values : {&fluid.rho, &fluid.mom_u, &fluid.mom_v,
                              &fluid.mom_w, &fluid.eng, &fluid.enuc_rate})
        if (values->size() != cells)
            throw std::logic_error("Native RZ boundary vector extent mismatch");
    if (fluid.mass_fractions.size() != cells * static_cast<std::size_t>(species))
        throw std::logic_error("Native RZ boundary composition extent mismatch");
}
} // namespace

/** Lower existing physical controls once when the actual EOS is borrowed. */
state::Bounds DriverRuntime::native_rz_eos_bounds() const
{
    const state::Bounds bounds{config.numerics.sml_rho,
        config.numerics.min_eint, config.numerics.max_eint};
    if (!state::valid_bounds(bounds))
        throw std::invalid_argument("Native RZ EOS binding requires valid physical bounds");
    return bounds;
}

/** Borrow the existing immutable species layout; no new parameter is introduced. */
int DriverRuntime::native_rz_species_count() const { return specs.count(); }

/** Attach the real EOS gate to the exact staged ledger and borrowed handle order.
 * The callback takes its actual calling context explicitly: stage_context()
 * returns by value, so capturing a reference to its local return object would
 * dangle. Only owned Runtime state and bounded immutable identity snapshots are
 * captured. Initial/regrid candidates may have an uncommitted replacement
 * ledger; neither is substituted with the accepted Runtime ledger.
 */
void DriverRuntime::bind_native_boundary_acceptance(StageExecutionContext& context,
    std::span<const amr::BlockHandle> handles)
{
    if (geometry_semantics_ != GridMetrics::GeometrySemantics::AxisymmetricRz)
        return;
    if (!native_rz_eos_acceptance_)
        throw std::logic_error("Native RZ boundary acceptance requires an explicitly bound EOS");
    if (compute_backend || context.side != ExecutionSide::Host
        || &context.clock != &scheduler_clock)
        throw std::logic_error("Native RZ EOS boundary acceptance requires the real Host clock/side");
    const auto& active = amr_ctrl.tree->GetActiveBlocks();
    if (handles.empty() || handles.size() != active.size())
        throw std::logic_error("Native RZ EOS boundary domain extent mismatch");
    auto* const ledger = &context.ledger;
    const bool committed_domain = ledger == residency_ledger.get();
    if (committed_domain)
        topology_registry.validate_committed_snapshot(observe_topology());
    const auto epoch = ledger->active_epoch();
    const int species = specs.count();
    std::vector<amr::BlockHandle> frozen_handles(handles.begin(), handles.end());
    std::unordered_set<std::uint64_t> unique_uids;
    unique_uids.reserve(handles.size());
    std::vector<NativeBoundaryPatch> patches;
    patches.reserve(active.size());
    for (std::size_t index = 0; index < active.size(); ++index) {
        if (!amr::is_valid(handles[index]) || handles[index].epoch != epoch
            || !unique_uids.insert(handles[index].uid.value).second)
            throw std::logic_error("Native RZ EOS boundary handles are invalid or repeated");
        (void)ledger->inspect({handles[index], StateSlot::Current});
        if (committed_domain
            && handles[index] != topology_registry.handle_for_pool(active[index]))
            throw std::logic_error("Native RZ EOS boundary handle/pool correspondence changed");
        const auto& block = amr_ctrl.pool->GetBlock(active[index]);
        patches.push_back(native_boundary_patch(active[index], block));
    }
    auto* const pool = amr_ctrl.pool.get();
    auto* const tree = amr_ctrl.tree.get();
    const auto boundary_snapshot = bc_handler.snapshot_stage_context();
    const auto eos_acceptance = native_rz_eos_acceptance_;
    context.post_boundary_acceptance = [this, ledger, committed_domain, epoch, species, pool, tree,
        handles, frozen_handles = std::move(frozen_handles), patches = std::move(patches),
        boundary_snapshot, eos_acceptance](const StageExecutionContext& actual,
            StateSlot slot, state::StateVersion version) {
        const auto member = TimeIntegration::hydro_boundary_state_member(slot);
        const auto require_frame = [&] {
            if (actual.side != ExecutionSide::Host || compute_backend
                || &actual.ledger != ledger || &actual.clock != &scheduler_clock
                || (committed_domain && (residency_ledger.get() != ledger
                    || stage_handles.data() != handles.data()
                    || stage_handles.size() != handles.size()))
                || ledger->active_epoch() != epoch || specs.count() != species
                || amr_ctrl.pool.get() != pool || amr_ctrl.tree.get() != tree
                || !bc_handler.stage_context_matches(boundary_snapshot))
                throw std::logic_error("Native RZ EOS boundary context/owner changed");
            // Gate before ghost publication: only accepted exact-version Host
            // INTERIORS are required. A pending ghost transfer also rejects.
            for (const auto handle : frozen_handles) {
                ledger->require_readable({handle, slot},
                    {ExecutionSide::Host, version, true, false});
                const auto coherence = ledger->inspect({handle, slot});
                if (coherence.interior.pending_transfer != state::PendingTransferPhase::None
                    || coherence.ghost.pending_transfer != state::PendingTransferPhase::None)
                    throw std::logic_error("Native RZ EOS boundary has pending state transfer");
            }
            const auto& current_active = amr_ctrl.tree->GetActiveBlocks();
            if (current_active.size() != patches.size()
                || handles.size() != frozen_handles.size()
                || !std::equal(handles.begin(), handles.end(), frozen_handles.begin()))
                throw std::logic_error("Native RZ EOS boundary borrowed domain changed");
            // Complete domain/layout preflight precedes the first EOS call;
            // a late invalid handle/slot cannot partly accept earlier patches.
            for (std::size_t index = 0; index < patches.size(); ++index) {
                const auto& block = amr_ctrl.pool->GetBlock(current_active[index]);
                if (!native_boundary_patch_matches(patches[index], current_active[index], block))
                    throw std::logic_error("Native RZ EOS boundary active pool/grid order changed");
                if (committed_domain && frozen_handles[index]
                    != topology_registry.handle_for_pool(current_active[index]))
                    throw std::logic_error("Native RZ EOS boundary handle/pool correspondence changed");
                require_native_boundary_layout(block.*member, block.grid, species);
            }
        };
        require_frame();
        for (std::size_t index=0;index<patches.size();++index) {
            const auto& patch=patches[index];
            const auto& block = amr_ctrl.pool->GetBlock(patch.pool_index);
            try {
                eos_acceptance(block.*member, block.grid);
            } catch(const RzThermodynamics::AcceptanceError& error) {
                // Frame/owner drift is fatal before attaching provenance. The
                // original phase/status survives; mean/physical EOS faults are
                // not converted into an effective-thermal failure.
                require_frame();
                const NativeBoundaryAcceptanceError failure(error.what(),patch.pool_index,
                    frozen_handles[index],slot,version,error.diagnostic());
                if(native_macro_retry_attempt_)qualify_native_thermal_rejection(actual,failure);
                throw failure;
            }
        }
        // EOS and callbacks must not change the BC time/purpose/revision or
        // any publication/layout owner during the real whole-domain gate.
        require_frame();
    };
}

namespace {
/** Compare actual frame numbers by bits, including signed zero. */
bool retry_same(double a,double b) noexcept {
    return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);
}
/** Reject any selected physical/gravity user BC, without case-sensitive gaps. */
bool retry_config_matches(const NumericsConfig& a,const NumericsConfig& b,
    const DiffusionConfig& d,const DiffusionConfig& e) noexcept {
    return a.solver_name==b.solver_name&&a.reconstruction==b.reconstruction&&a.limiter==b.limiter
        &&a.time_integrator==b.time_integrator&&a.hll_roe_wave_speed==b.hll_roe_wave_speed
        &&retry_same(a.dt_init,b.dt_init)&&retry_same(a.dt_max,b.dt_max)&&retry_same(a.dt_min,b.dt_min)
        &&retry_same(a.tstep_change_factor,b.tstep_change_factor)&&retry_same(a.cfl,b.cfl)
        &&retry_same(a.entropy_fix_coeff,b.entropy_fix_coeff)&&retry_same(a.sml_rho,b.sml_rho)
        &&retry_same(a.min_eint,b.min_eint)&&retry_same(a.max_eint,b.max_eint)
        &&d.use_diffusion==e.use_diffusion&&d.integrator==e.integrator&&d.max_stages==e.max_stages
        &&d.use_thermal_diffusion==e.use_thermal_diffusion&&d.use_viscous_diffusion==e.use_viscous_diffusion
        &&d.use_species_diffusion==e.use_species_diffusion&&retry_same(d.diff_cfl,e.diff_cfl)
        &&retry_same(d.nu_visc,e.nu_visc)&&retry_same(d.alpha_therm,e.alpha_therm)&&retry_same(d.D_spec,e.D_spec);
}
/** Case-independent real selected method comparison, never an unknown fallback. */
bool retry_selected_method(const std::string& name,scheduler::RklMethod method) noexcept {
    return name.size()==4&&(name[0]=='r'||name[0]=='R')&&(name[1]=='k'||name[1]=='K')
        &&(name[2]=='l'||name[2]=='L')&&name[3]==(method==scheduler::RklMethod::RKL1?'1':'2');
}
bool retry_user_word(const std::string& s) noexcept {
    return s.size()==4&&(s[0]=='u'||s[0]=='U')&&(s[1]=='s'||s[1]=='S')
        &&(s[2]=='e'||s[2]=='E')&&(s[3]=='r'||s[3]=='R');
}
}
/** Match the published actual EOS borrower, its binding epoch and real bounds.
 * Frozen metadata is not reconstructed from current configuration. Both must
 * agree by bits before entry and before the private live refusal is minted.
 */
bool DriverRuntime::native_rz_eos_binding_matches(const NativeRzEosBindingWitness& expected) const {
    if(!native_rz_eos_binding_||!native_rz_eos_acceptance_||!native_rz_active_thermal_classification_)return false;
    const auto& actual=*native_rz_eos_binding_;
    const auto& n=config.numerics;
    return actual.owner&&actual.revision!=0&&actual.owner==expected.owner
        &&actual.revision==expected.revision&&actual.revision==native_rz_eos_binding_revision_
        &&actual.species==expected.species&&actual.species==native_rz_species_count()
        &&state::valid_bounds(actual.bounds)
        &&retry_same(actual.bounds.density,expected.bounds.density)
        &&retry_same(actual.bounds.internal_min,expected.bounds.internal_min)
        &&retry_same(actual.bounds.internal_max,expected.bounds.internal_max)
        &&retry_same(actual.bounds.density,n.sml_rho)
        &&retry_same(actual.bounds.internal_min,n.min_eint)
        &&retry_same(actual.bounds.internal_max,n.max_eint);
}
/** Verify accepted entry before the first B/D/H write; unsupported user BC simply
 * disables retries while preserving its original normal/fatal execution path. */
NativeMacroRetryAttempt::NativeMacroRetryAttempt(DriverRuntime& runtime,
    StageExecutionContext& context,std::uint64_t attempt,std::optional<scheduler::RklMethod> method,double dt_fe)
    :runtime_(runtime),context_(context),attempt_(attempt),start_(context.step_start_time),dt_(context.step_dt),
      dt_fe_(dt_fe),method_(method),numerics_(runtime.config.numerics),diffusion_(runtime.config.physics.diffusion),
      eos_binding_(runtime.native_rz_eos_binding_),handles_(runtime.stage_handles) {
    if(runtime.compute_backend||runtime.geometry_semantics_!=GridMetrics::GeometrySemantics::AxisymmetricRz)return;
    const auto& g=runtime.config.grid;
    if(runtime.bc_handler.has_user()||retry_user_word(runtime.config.physics.gravity.boundary)
        ||retry_user_word(g.x1l_boundary_type)||retry_user_word(g.x1r_boundary_type)
        ||retry_user_word(g.x2l_boundary_type)||retry_user_word(g.x2r_boundary_type)
        ||retry_user_word(g.x3l_boundary_type)||retry_user_word(g.x3r_boundary_type))return;
    const auto& binding=scheduler::current_stage_binding();
    if(runtime.native_macro_retry_attempt_||runtime.host_hydro_transaction_||!attempt
        ||&binding.context!=&context||binding.handles.data()!=handles_.data()||binding.handles.size()!=handles_.size()
        ||&context.ledger!=runtime.residency_ledger.get()||&context.clock!=&runtime.scheduler_clock
        ||context.side!=ExecutionSide::Host||!std::isfinite(start_)||!std::isfinite(dt_)||dt_<=0.
        ||!eos_binding_||!runtime.native_rz_eos_binding_matches(*eos_binding_)
        ||!context.post_boundary_acceptance||!context.configure_boundary_context)
        throw std::logic_error("Native retry requires its exact quiescent macro entry");
    if(diffusion_.use_diffusion&&(!method_||!std::isfinite(dt_fe_)||dt_fe_<=0.
        ||!retry_selected_method(diffusion_.integrator,*method_)))
        throw std::logic_error("Native retry lacks the selected diffusion FE owner");
    runtime.topology_registry.validate_committed_snapshot(runtime.observe_topology());
    const auto active=runtime.amr_ctrl.tree->GetActiveBlocks();
    const std::vector<amr::BlockHandle> entry_handles(handles_.begin(),handles_.end());
    if(active.size()!=handles_.size())throw std::logic_error("Native retry entry domain mismatch");
    const auto* const entry_pool=runtime.amr_ctrl.pool.get();
    const auto* const entry_tree=runtime.amr_ctrl.tree.get();
    const auto boundary=runtime.bc_handler.snapshot_stage_context();
    const auto entry_token=context.clock.last_token(),entry_version=context.clock.last_version();
    std::vector<NativeBoundaryPatch> entry_patches;
    std::vector<state::StateVersion> entry_versions;
    entry_patches.reserve(active.size());entry_versions.reserve(active.size());
    for(std::size_t i=0;i<active.size();++i) {
        entry_patches.push_back(native_boundary_patch(active[i],runtime.amr_ctrl.pool->GetBlock(active[i])));
        entry_versions.push_back(context.ledger.inspect({handles_[i],StateSlot::Current}).interior.version);
    }
    // Only metadata is frozen: no extra conserved-field backup or EOS model.
    // A synchronous EOS callback must leave the complete entry frame unchanged.
    const auto require_entry=[&] {
        const auto& actual_binding=scheduler::current_stage_binding();
        if(&actual_binding.context!=&context||actual_binding.handles.data()!=handles_.data()
            ||actual_binding.handles.size()!=handles_.size()||runtime.host_hydro_transaction_
            ||runtime.native_macro_retry_attempt_||runtime.compute_backend||context.side!=ExecutionSide::Host
            ||&context.ledger!=runtime.residency_ledger.get()||&context.clock!=&runtime.scheduler_clock
            ||context.clock.last_token()!=entry_token||context.clock.last_version()!=entry_version
            ||!retry_same(context.step_start_time,start_)||!retry_same(context.step_dt,dt_)
            ||!runtime.native_rz_eos_binding_matches(*eos_binding_)
            ||!retry_config_matches(runtime.config.numerics,numerics_,runtime.config.physics.diffusion,diffusion_)
            ||!runtime.bc_handler.stage_context_matches(boundary)
            ||runtime.amr_ctrl.pool.get()!=entry_pool||runtime.amr_ctrl.tree.get()!=entry_tree
            ||runtime.amr_ctrl.tree->GetActiveBlocks()!=active
            ||runtime.stage_handles.data()!=handles_.data()||runtime.stage_handles.size()!=handles_.size()
            ||!std::equal(handles_.begin(),handles_.end(),entry_handles.begin()))
            throw std::logic_error("Native retry accepted-entry EOS changed its live owner frame");
        runtime.topology_registry.validate_committed_snapshot(runtime.observe_topology());
        for(std::size_t i=0;i<active.size();++i) {
            const auto& block=runtime.amr_ctrl.pool->GetBlock(active[i]);
            if(!native_boundary_patch_matches(entry_patches[i],active[i],block)
                ||entry_handles[i]!=runtime.topology_registry.handle_for_pool(active[i]))
                throw std::logic_error("Native retry entry grid/UID correspondence changed");
            const auto key=state::StateKey{handles_[i],StateSlot::Current};
            context.ledger.require_readable(key,{ExecutionSide::Host,entry_versions[i],true,true});
            const auto coherence=context.ledger.inspect(key);
            if(coherence.interior.pending_transfer!=state::PendingTransferPhase::None
                ||coherence.ghost.pending_transfer!=state::PendingTransferPhase::None)
                throw std::logic_error("Native retry entry has a pending state transfer");
            require_native_boundary_layout(block.fluid_state,block.grid,runtime.specs.count());
        }
    };
    require_entry();
    for(const int id:active) {
        const auto& block=runtime.amr_ctrl.pool->GetBlock(id);
        runtime.native_rz_eos_acceptance_(block.fluid_state,block.grid);
    }
    require_entry();
    runtime.native_macro_retry_attempt_=this;enabled_=true;
}
/** Release metadata only after the real outer transaction finished/unwound. */
NativeMacroRetryAttempt::~NativeMacroRetryAttempt() noexcept {
    if(!enabled_)return;
    if(runtime_.native_macro_retry_attempt_!=this||runtime_.host_hydro_transaction_)std::terminate();
    runtime_.native_macro_retry_attempt_=nullptr;
}
/** Preserve the existing half_dt/stage schedule; this names, never executes it. */
void NativeMacroRetryAttempt::begin_diffusion_half(int half,double interval) {
    if(!enabled_||!diffusion_.use_diffusion)return;
    if((half!=1&&half!=2)||half_||!retry_same(interval,.5*dt_)
        ||!eos_binding_||!runtime_.native_rz_eos_binding_matches(*eos_binding_)
        ||!retry_config_matches(runtime_.config.numerics,numerics_,runtime_.config.physics.diffusion,diffusion_))
        throw std::logic_error("Native retry diffusion half changed its frozen macro");
    const auto order=*method_==scheduler::RklMethod::RKL1?DiffFunction::RKLOrder::First:DiffFunction::RKLOrder::Second;
    stages_=DiffFunction::compute_stages(order,interval,dt_fe_,diffusion_.diff_cfl,diffusion_.max_stages);half_=half;
}
/** Authenticate a still-live active candidate before any macro rollback.
 * All active cells use the same completed density support and selected EOS;
 * unrelated EOS/input/ghost failures remain fatal, never silently retried.
 */
void DriverRuntime::qualify_native_thermal_rejection(const StageExecutionContext& actual,
    const NativeBoundaryAcceptanceError& error) {
    auto& attempt=*native_macro_retry_attempt_;
    const auto* frame=scheduler::current_rkl_completed_boundary();
    const auto& d=error.diagnostic;
    if(d.phase!=RzThermodynamics::AcceptancePhase::effective_thermal
        ||d.status!=state::Status::unresolved_energy||!d.inertia_mapping_valid)return;
    if(!frame||!frame->descriptor)
        throw std::logic_error("Native thermal refusal lacks its actual completed RKL descriptor");
    const auto descriptor_before=*frame->descriptor;
    const auto completion_before=frame->completion;
    const auto boundary_snapshot=bc_handler.snapshot_stage_context();
    const auto* const pool_before=amr_ctrl.pool.get();
    const auto* const tree_before=amr_ctrl.tree.get();
    const auto active_before=amr_ctrl.tree->GetActiveBlocks();
    std::vector<NativeBoundaryPatch> patches_before;
    patches_before.reserve(active_before.size());
    for(const int id:active_before)
        patches_before.push_back(native_boundary_patch(id,amr_ctrl.pool->GetBlock(id)));
    // The classifier borrows the actual EOS. It cannot grant a retry after a
    // supported synchronous callback changes configuration, stage or ownership.
    const auto require_live_failure=[&] {
    if(scheduler::current_rkl_completed_boundary()!=frame||!frame->descriptor
        ||frame->completion!=completion_before||!frame->completed||frame->context!=&actual||&actual!=&attempt.context_
        ||!host_hydro_transaction_||!attempt.enabled_||!attempt.method_||attempt.half_==0
        ||!attempt.eos_binding_||!native_rz_eos_binding_matches(*attempt.eos_binding_)
        ||actual.side!=ExecutionSide::Host||compute_backend||&actual.ledger!=residency_ledger.get()
        ||&actual.clock!=&scheduler_clock||frame->handles.data()!=stage_handles.data()
        ||frame->handles.size()!=stage_handles.size()||attempt.handles_.data()!=stage_handles.data()
        ||attempt.handles_.size()!=stage_handles.size()||frame->slot!=error.slot||frame->version!=error.version
        ||frame->completion.value!=actual.clock.last_token()||!state::is_complete(frame->completion)
        ||frame->version.value!=actual.clock.last_version()
        ||!retry_same(actual.step_start_time,attempt.start_)||!retry_same(actual.step_dt,attempt.dt_)
        ||!retry_config_matches(config.numerics,attempt.numerics_,config.physics.diffusion,attempt.diffusion_))
        throw std::logic_error("Native thermal refusal lost its live macro/RKL owner");
    host_hydro_transaction_->validate_storage();
    const auto plan=scheduler::make_rkl_plan(*attempt.method_,attempt.stages_);
    const auto& observed=*frame->descriptor;
    if(observed.stage<1||observed.stage>attempt.stages_)
        throw std::logic_error("Native thermal refusal has an invalid actual RKL stage");
    const auto& expected=plan.stages[static_cast<std::size_t>(observed.stage-1)];
    if(observed.stage!=expected.stage||observed.state_n_slot!=expected.state_n_slot
        ||observed.previous_slot!=expected.previous_slot||observed.older_slot!=expected.older_slot
        ||observed.output_slot!=expected.output_slot||observed.reflux_before_publish!=expected.reflux_before_publish
        ||observed.refresh_ghost_after!=expected.refresh_ghost_after)
        throw std::logic_error("Native thermal refusal changed its selected RKL descriptor");
    const double half_start=attempt.start_+(attempt.half_==2?.5*attempt.dt_:0.);
    const double half_dt=.5*attempt.dt_;
    const double expected_time=half_start+scheduler::rkl_stage_time_fraction(
        *attempt.method_,observed.stage,attempt.stages_)*half_dt;
    if(!retry_same(actual.boundary_start_time,half_start)||!retry_same(actual.boundary_step_dt,half_dt)
        ||!retry_same(boundary_snapshot.time(),expected_time)||boundary_snapshot.purpose()!=boundary::BoundaryPurpose::Diffusion)
        throw std::logic_error("Native thermal refusal changed its actual diffusion BC time/purpose");
    if(scheduler::current_rkl_completed_boundary()!=frame
        ||(frame->descriptor->stage!=descriptor_before.stage
            ||frame->descriptor->state_n_slot!=descriptor_before.state_n_slot
            ||frame->descriptor->previous_slot!=descriptor_before.previous_slot
            ||frame->descriptor->older_slot!=descriptor_before.older_slot
            ||frame->descriptor->output_slot!=descriptor_before.output_slot
            ||frame->descriptor->reflux_before_publish!=descriptor_before.reflux_before_publish
            ||frame->descriptor->refresh_ghost_after!=descriptor_before.refresh_ghost_after))
        throw std::logic_error("Native thermal refusal changed its live RKL frame during classification");
    if(!bc_handler.stage_context_matches(boundary_snapshot)
        ||amr_ctrl.pool.get()!=pool_before||amr_ctrl.tree.get()!=tree_before
        ||amr_ctrl.tree->GetActiveBlocks()!=active_before||stage_handles.size()!=active_before.size())
        throw std::logic_error("Native thermal refusal changed its actual domain or boundary frame");
    const auto& binding=scheduler::current_stage_binding();
    if(&binding.context!=&actual||binding.handles.data()!=stage_handles.data()
        ||binding.handles.size()!=stage_handles.size())
        throw std::logic_error("Native thermal refusal changed its actual stage binding");
    host_hydro_transaction_->validate_storage();
    topology_registry.validate_committed_snapshot(observe_topology());
    const auto input_member=TimeIntegration::hydro_boundary_state_member(error.slot);
    for(std::size_t b=0;b<active_before.size();++b) {
        const auto& block=amr_ctrl.pool->GetBlock(active_before[b]);
        if(!native_boundary_patch_matches(patches_before[b],active_before[b],block)
            ||stage_handles[b]!=topology_registry.handle_for_pool(active_before[b]))
            throw std::logic_error("Native thermal refusal changed actual grid/UID correspondence");
        require_native_boundary_layout(block.*input_member,block.grid,specs.count());
        const auto key=state::StateKey{stage_handles[b],error.slot};
        actual.ledger.require_readable(key,{ExecutionSide::Host,error.version,true,false});
        const auto coherence=actual.ledger.inspect(key);
        if(coherence.interior.pending_transfer!=state::PendingTransferPhase::None
            ||coherence.ghost.pending_transfer!=state::PendingTransferPhase::None)
            throw std::logic_error("Native thermal refusal has a pending state transfer");
    }
    };
    require_live_failure();
    const auto member=TimeIntegration::hydro_boundary_state_member(error.slot);
    const auto& active=amr_ctrl.tree->GetActiveBlocks();bool requested=false;
    for(std::size_t b=0;b<active.size();++b) {
        const auto& block=amr_ctrl.pool->GetBlock(active[b]);
        const auto key=state::StateKey{stage_handles[b],error.slot};
        actual.ledger.require_readable(key,{ExecutionSide::Host,error.version,true,false});
        const bool target=active[b]==error.pool_index&&stage_handles[b]==error.handle;
        if(target&&(d.i<block.grid.Is()||d.i>=block.grid.Ie()||d.j<block.grid.Js()||d.j>=block.grid.Je()))return;
        if(target&&d.index!=block.grid.GetIndex(d.i,d.j,0))
            throw std::logic_error("Native thermal refusal index does not match its actual active cell");
        try {
            const auto classification=native_rz_active_thermal_classification_(block.*member,block.grid,target?d.index:-1);
            if(target)requested=classification.requested_failure;
        } catch(const RzThermodynamics::AcceptanceError& other) {
            require_live_failure();
            // Preserve the actual later-cell phase/status and patch provenance.
            // This fatal error is never recursively reconsidered for retry.
            throw NativeBoundaryAcceptanceError(other.what(),active[b],stage_handles[b],
                error.slot,error.version,other.diagnostic());
        }
    }
    require_live_failure();
    if(!requested||!native_rz_eos_binding_matches(*attempt.eos_binding_)
        ||!bc_handler.stage_context_matches(boundary_snapshot))
        throw std::logic_error("Native thermal refusal lacks the original live active failure");
    throw NativeThermalStepRejection(error,this,attempt.attempt_,attempt.start_,attempt.dt_);
}

/** Return a context bound to the live ledger, with mandatory native-RZ EOS gate. */
StageExecutionContext DriverRuntime::stage_context()
{
    if (!residency_ledger || stage_handles.empty())
        throw std::logic_error("state residency is not initialized");
    StageExecutionContext context{
        compute_backend ? ExecutionSide::Device : ExecutionSide::Host,
        *residency_ledger, scheduler_clock};
    bind_native_boundary_acceptance(context, stage_handles);
    return context;
}
/** Borrow core run owners and initialize state/version bookkeeping. */
DriverRuntime::DriverRuntime(amr::AMRControl& control, BCHandler& boundaries,
    const SimConfig& settings, const SpeciesManager& species, SimulationController& controller)
    : geometry_semantics_(boundaries.geometry_semantics()), amr_ctrl(control), bc_handler(boundaries), config(settings), specs(species), ctrl(controller),
      topology_registry(topology::TopologyDomainBounds{
          config.grid.dim,
          {static_cast<std::uint32_t>(std::max(1, config.grid.nblockx1)),
           static_cast<std::uint32_t>(std::max(1, config.grid.nblockx2)),
           static_cast<std::uint32_t>(std::max(1, config.grid.nblockx3))},
          config.amr.lrefinemax}) {
    ctrl.repairs.bind_semantics(geometry_semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz
        ? state::RepairSemantics::RzVolumeAngular : state::RepairSemantics::ExistingVolume);
}
DriverRuntime::~DriverRuntime() = default;
/** Snapshot logical identities for the requested active block order. */
std::vector<TopologyObservation> DriverRuntime::observe_blocks(std::span<const int> active) const
{
    std::vector<TopologyObservation> observations;
    observations.reserve(active.size());
    for (const int pool_index : active) {
        const amr::Block& block = amr_ctrl.pool->GetBlock(pool_index);
        observations.push_back({
            pool_index,
            LogicalBlockIdentity{
                config.grid.dim, block.level, block.logical_x1,
                block.logical_x2, block.logical_x3}});
    }
    return observations;
}

/** Snapshot the complete current AMR topology for reconciliation. */
std::vector<TopologyObservation> DriverRuntime::observe_topology() const
{
    const auto& active = amr_ctrl.tree->GetActiveBlocks();
    return observe_blocks(active);
}

/** Return the committed interior version of the current state. */
state::StateVersion DriverRuntime::current_interior_version() const
{
    if (!residency_ledger || stage_handles.empty())
        throw std::logic_error("state residency is not initialized");
    const arch::state::StateVersion version = residency_ledger->inspect(
        {stage_handles.front(), StateSlot::Current}).interior.version;
    for (const amr::BlockHandle handle : stage_handles) {
        if (residency_ledger->inspect({handle, StateSlot::Current})
                .interior.version != version) {
            throw std::logic_error(
                "active Current interiors do not share one version");
        }
    }
    return version;
}

/** Consume accepted interiors only after whole-domain publication preflight.
 * Current slot/storage identity alone is not a publication lease. Keep the
 * physical EOS/Jeans computation in the existing tree/backend owners.
 */
std::vector<double> DriverRuntime::evaluate_current_jeans_resolution()
{
    topology_registry.validate_committed_snapshot(observe_topology());
    const auto version = current_interior_version();
    const auto& active = amr_ctrl.tree->GetActiveBlocks();
    if (active.size() != stage_handles.size()
        || (compute_backend && backend_storage.size() != active.size()))
        throw std::logic_error("JENS Current topology/storage extent mismatch");
    const auto side = compute_backend ? ExecutionSide::Device : ExecutionSide::Host;
    std::vector<backend::BackendStateAccess> accesses;
    if (compute_backend) accesses.reserve(active.size());
    // Native density/inertia recovery reads real neighboring ghost means.
    // Require their matching publication across the whole domain before any
    // patch EOS; this reader never configures/fills BC or guesses a time.
    for (std::size_t index = 0; index < active.size(); ++index) {
        residency_ledger->require_readable(
            {stage_handles[index],StateSlot::Current},
            {side,version,true,geometry_semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz});
        if (compute_backend) {
            const auto access = backend_access(index, StateSlot::Current);
            if (!compute_backend->contains(access))
                throw std::logic_error("JENS Current backend storage is unavailable");
            accesses.push_back(access);
        }
    }
    std::vector<double> result;
    if (compute_backend) result = compute_backend->evaluate_jeans_resolution(accesses);
    else {
        result.reserve(active.size());
        for (int id : active)
            result.push_back(amr_ctrl.tree->MinimumJeansCells(amr_ctrl.pool->GetBlock(id)));
    }
    if (result.size() != active.size())
        throw std::logic_error("JENS Current summary extent mismatch");
    for (double value : result)
        if (!std::isfinite(value) || value <= 0.)
            throw std::runtime_error("JENS Current summary is invalid");
    return result;
}

/** Lower one host fluid state to the backend transfer view. */
backend::HostStateTransferView DriverRuntime::host_transfer_view(FluidState& state)
{
    const std::size_t cells = state.rho.size();
    const std::size_t species = static_cast<std::size_t>(
        state.GetNumSpecies());
    return arch::backend::HostStateTransferView{
        state.rho.data(), state.mom_u.data(), state.mom_v.data(),
        state.mom_w.data(), state.eng.data(), state.enuc_rate.data(),
        species == 0 ? nullptr : state.mass_fractions.data(), cells,
        species, species == 0 ? 0 : cells};
}

/** Lease one backend block and state slot with generation identity. */
backend::BackendStateAccess DriverRuntime::backend_access(std::size_t block_index, StateSlot slot) const
{
    if (!compute_backend)
        throw std::logic_error("CUDA backend is not constructed");
    if (block_index >= stage_handles.size()
        || block_index >= backend_storage.size())
        throw std::out_of_range("CUDA backend block index is invalid");
    return arch::backend::BackendStateAccess{
        stage_handles[block_index], backend_storage[block_index], slot};
}

/** Record backend transfer/launch deltas for the requested stage. */
void DriverRuntime::trace_backend_operation(backend::BackendOperation operation, StateSlot slot, const backend::BackendCounters& before)
{
    const auto after = compute_backend->counters();
    for (std::size_t index = 0; index < stage_handles.size(); ++index) {
        compute_backend->append_trace({
            static_cast<std::uint64_t>(ctrl.step_count), operation,
            stage_handles[index], backend_storage[index], slot,
            residency_ledger->inspect({stage_handles[index], slot}),
            index == 0 ? after.bytes_h2d - before.bytes_h2d : 0,
            index == 0 ? after.bytes_d2h - before.bytes_d2h : 0,
            index == 0 ? after.kernel_count - before.kernel_count : 0,
            index == 0
                ? after.stream_sync_count - before.stream_sync_count : 0});
    }
}

/** Register initial block identities and their state residency. */
void DriverRuntime::initialize_topology()
{
    if(host_hydro_transaction_)throw std::logic_error("Active Host Hydro owner excludes topology/backend mutation");
    if (geometry_semantics_ == GridMetrics::GeometrySemantics::AxisymmetricRz
        && !native_rz_eos_acceptance_)
        throw std::logic_error("Native RZ initialization requires an explicitly bound EOS");
    for (int id:amr_ctrl.tree->GetActiveBlocks())
        (void)bc_handler.logical_plan(amr_ctrl.pool->GetBlock(id).grid);
    auto initial_candidate =
        topology_registry.stage_adoption(observe_topology());
    std::unique_ptr<StateResidencyLedger> staged_initial_ledger;
    std::vector<amr::BlockHandle> staged_initial_handles;
    const auto initial_topology = topology_registry.commit_after_success(
        std::move(initial_candidate), [&](const auto& proposed) {
            auto replacement =
                std::make_unique<StateResidencyLedger>(proposed.epoch);
            const arch::scheduler::PublicationWitness initial_witness =
                scheduler_clock.next_publication();
            for (const amr::BlockHandle handle
                 : proposed.handles_in_observation_order) {
                replacement->register_block(handle, initial_witness.version,
                                            initial_witness.completion);
            }
            // A restart can deliberately skip the first regrid and initial
            // output. Establish real host ghost data and publish it here so
            // diffusion/hydro never starts from an interior-only Current slot.
            StageExecutionContext staged_context{
                ExecutionSide::Host, *replacement, scheduler_clock};
            bind_native_boundary_acceptance(
                staged_context, proposed.handles_in_observation_order);
            TimeIntegration::synchronize_domain_boundary(amr_ctrl, bc_handler,
                &amr::Block::fluid_state, proposed.handles_in_observation_order,
                geometry_semantics_,
                {config.numerics.sml_rho,config.numerics.min_eint,config.numerics.max_eint});
            (void)arch::scheduler::complete_boundary(
                staged_context, proposed.handles_in_observation_order,
                StateSlot::Current, initial_witness.version,
                [](StateSlot, arch::state::StateVersion,
                   arch::state::CompletionToken token) { return token; });
            staged_initial_handles = proposed.handles_in_observation_order;
            staged_initial_ledger = std::move(replacement);
        });
    stage_handles = std::move(staged_initial_handles);
    residency_ledger = std::move(staged_initial_ledger);
    if (initial_topology.handles_in_observation_order != stage_handles)
        throw std::logic_error("initial topology commit result mismatch");
    amr_ctrl.BindActiveHandles(stage_handles);

}

/** Build topology bindings for backend storage allocation. */
std::vector<backend::BackendTopologyBinding> DriverRuntime::prepare_backend_bindings()
{
    if(host_hydro_transaction_)throw std::logic_error("Active Host Hydro owner excludes topology/backend mutation");
    if (geometry_semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz)
        throw std::logic_error("RZ device runtime is not yet migrated");
    const auto& active = amr_ctrl.tree->GetActiveBlocks();
    if (active.empty() || stage_handles.size() != active.size()) {
        throw std::logic_error(
            "CUDA backend requires a complete active topology");
    }
    bool needs_host_ghosts = false;
    for (const auto handle : stage_handles) {
        const auto coherence = residency_ledger->inspect(
            {handle, StateSlot::Current});
        needs_host_ghosts = needs_host_ghosts
            || !arch::state::side_can_read(
                coherence.ghost.residency, ExecutionSide::Host)
            || coherence.ghost.version != coherence.interior.version
            || coherence.ghost_source_version != coherence.interior.version;
    }
    if (needs_host_ghosts)
        materialize_current_for_host();

    std::vector<backend::BackendTopologyBinding> bindings;
    bindings.reserve(active.size());
    backend_storage.clear();
    backend_storage.reserve(active.size());
    for (std::size_t index = 0; index < active.size(); ++index) {
        const auto storage = storage_generation_issuer.issue();
        backend_storage.push_back(storage);
        bindings.push_back({
            &amr_ctrl.pool->GetBlock(active[index]), stage_handles[index],
            storage, &bc_handler.logical_plan()});
    }
    return bindings;
}

/** Install a validated compute backend and its resident block views. */
void DriverRuntime::install_backend(std::unique_ptr<backend::ComputeBackend> backend)
{
    if(host_hydro_transaction_)throw std::logic_error("Active Host Hydro owner excludes topology/backend mutation");
    if (geometry_semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz)
        throw std::logic_error("RZ device runtime is not yet migrated");
    if (compute_backend || !backend) throw std::logic_error("invalid backend installation");
    compute_backend = std::move(backend);
}

/** Upload accepted case initial state before device stepping. */
void DriverRuntime::upload_initial_state()
{
    if(host_hydro_transaction_)throw std::logic_error("Active Host Hydro owner excludes topology/backend mutation");
    const auto& active = amr_ctrl.tree->GetActiveBlocks();
    for (std::size_t index = 0; index < active.size(); ++index) {
        amr::Block& block = amr_ctrl.pool->GetBlock(active[index]);
        (void)arch::backend::transfer_state_regions(
            *compute_backend, *residency_ledger, scheduler_clock,
            backend_access(index, StateSlot::Current),
            host_transfer_view(block.fluid_state),
            arch::state::PendingTransferPhase::PendingH2D, 0,
            arch::backend::BackendOperation::InitialUpload);
    }
    compute_backend->prepare_amr_flux_plan(
        amr_ctrl.RequireFluxTopologyPlan(specs.count()),
        amr_ctrl.RequireRefluxTopologyPlan(specs.count()));
}
} // namespace arch::driver
