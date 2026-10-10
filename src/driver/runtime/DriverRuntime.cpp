/**
 * @file DriverRuntime.cpp
 * @brief Own live mesh, state residency and backend resources for the integration loop.
 *
 * Workflow:
 * 1. Borrow resolved configuration, species and the actual run EOS.
 * 2. Stage topology/ledger identity before real physical BC and halo exchange.
 * 3. For native RZ, preflight the actual domain and validate the shared
 *    thermodynamic closure through its selected Host/Device EOS before ghosts.
 * 4. Bootstrap the actual resident AMR flux/reflux chart, including native
 *    angular transport, before publishing it to the next numerical stage.
 */

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "driver/runtime/DriverRuntime.h"

#include "amr/AMRControl.h"
#include "driver/DriverUtils.h"
#include "driver/runtime/RuntimeStateTransaction.h"
#include "driver/schedule/DriverControl.h"
#include "numerics/diffusion/DiffFunction.h"
#include "numerics/diffusion/DiffusionTypes.h"
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
    std::span<const amr::BlockHandle> handles,
    backend::BackendTopologyStoreTransaction* staged,
    std::span<const backend::StorageGeneration> staged_storage)
{
    if (!staged && !staged_storage.empty())
        throw std::logic_error("Native boundary storage requires its explicit staged store");
    if (geometry_semantics_ != GridMetrics::GeometrySemantics::AxisymmetricRz) {
        if (staged)
            throw std::logic_error("Staged boundary acceptance requires Native RZ geometry");
        return;
    }
    if (!native_rz_eos_acceptance_)
        throw std::logic_error("Native RZ boundary acceptance requires an explicitly bound EOS");
    const auto side = context.side;
    auto* const backend_owner = compute_backend.get();
    if (&context.clock != &scheduler_clock
        || (side == ExecutionSide::Host && backend_owner)
        || (side == ExecutionSide::Device
            && (!backend_owner || backend_owner->side() != ExecutionSide::Device))
        || (side != ExecutionSide::Host && side != ExecutionSide::Device))
        throw std::logic_error("Native RZ EOS boundary acceptance requires its real clock/side");
    const auto& active = amr_ctrl.tree->GetActiveBlocks();
    if (handles.empty() || handles.size() != active.size())
        throw std::logic_error("Native RZ EOS boundary domain extent mismatch");
    auto* const ledger = &context.ledger;
    const bool committed_domain = ledger == residency_ledger.get();
    if (committed_domain)
        topology_registry.validate_committed_snapshot(observe_topology());
    // A staged Device gate retains its replacement ledger and exact store.
    // Initial/regrid Host candidates retain their original full Host gate.
    if (staged && (side != ExecutionSide::Device || committed_domain
        || staged_storage.size() != handles.size()))
        throw std::logic_error("Native staged EOS boundary requires its replacement Device domain");
    if (!staged && side == ExecutionSide::Device
        && (!committed_domain || handles.data() != stage_handles.data()
            || handles.size() != stage_handles.size()
            || backend_storage.size() != handles.size()))
        throw std::logic_error("Native Device EOS boundary requires the complete committed domain");
    const std::vector<backend::StorageGeneration> frozen_storage = staged
        ? std::vector<backend::StorageGeneration>(staged_storage.begin(), staged_storage.end())
        : backend_owner ? backend_storage : std::vector<backend::StorageGeneration>{};
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
        if (backend_owner) {
            (void)GridMetrics::make_geometry_view(block.grid, geometry_semantics_);
            const backend::BackendStateAccess access{
                handles[index], frozen_storage[index], StateSlot::Current};
            if (!(staged ? backend_owner->contains(*staged, access)
                         : backend_owner->contains(access)))
                throw std::logic_error("Native Device EOS boundary actual storage is unavailable");
        }
    }
    auto* const pool = amr_ctrl.pool.get();
    auto* const tree = amr_ctrl.tree.get();
    const auto boundary_snapshot = bc_handler.snapshot_stage_context();
    const auto eos_acceptance = native_rz_eos_acceptance_;
    const auto eos_binding = native_rz_eos_binding_;
    if (!eos_binding || !native_rz_eos_binding_matches(*eos_binding))
        throw std::logic_error("Native RZ EOS boundary binding is stale");
    context.post_boundary_acceptance = [this, ledger, committed_domain, epoch, species, pool, tree,
        side, backend_owner, staged, staged_storage, frozen_storage,
        handles, frozen_handles = std::move(frozen_handles), patches = std::move(patches),
        boundary_snapshot, eos_acceptance, eos_binding](const StageExecutionContext& actual,
            StateSlot slot, state::StateVersion version) {
        if (staged && slot != StateSlot::Current)
            throw std::logic_error("Native staged EOS boundary accepts Current only");
        const auto member = TimeIntegration::hydro_boundary_state_member(slot);
        std::vector<backend::BackendStateAccess> accesses;
        if (backend_owner) {
            accesses.reserve(frozen_handles.size());
            for (std::size_t index = 0; index < frozen_handles.size(); ++index)
                accesses.push_back({frozen_handles[index], frozen_storage[index], slot});
        }
        const auto require_frame = [&] {
            if (actual.side != side || compute_backend.get() != backend_owner
                || (backend_owner && (backend_owner->side() != ExecutionSide::Device
                    || (!staged && backend_storage != frozen_storage)))
                || (staged && (residency_ledger.get() == ledger
                    || staged_storage.size() != frozen_storage.size()
                    || !std::equal(staged_storage.begin(), staged_storage.end(), frozen_storage.begin())))
                || &actual.ledger != ledger || &actual.clock != &scheduler_clock
                || (committed_domain && (residency_ledger.get() != ledger
                    || stage_handles.data() != handles.data()
                    || stage_handles.size() != handles.size()))
                || ledger->active_epoch() != epoch || specs.count() != species
                || amr_ctrl.pool.get() != pool || amr_ctrl.tree.get() != tree
                || !native_rz_eos_binding_matches(*eos_binding)
                || !bc_handler.stage_context_matches(boundary_snapshot))
                throw std::logic_error("Native RZ EOS boundary context/owner changed");
            // Gate before ghost publication: require only exact-version actual
            // INTERIORS. Completed ghost data is read by the real EOS owner;
            // neither side may have a pending transfer at this acceptance gate.
            for (const auto handle : frozen_handles) {
                ledger->require_readable({handle, slot},
                    {side, version, true, false});
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
                if (backend_owner) {
                    (void)GridMetrics::make_geometry_view(block.grid, geometry_semantics_);
                    if (!(staged ? backend_owner->contains(*staged, accesses[index])
                                 : backend_owner->contains(accesses[index])))
                        throw std::logic_error("Native Device EOS boundary storage lease changed");
                } else {
                    require_native_boundary_layout(block.*member, block.grid, species);
                }
            }
        };
        require_frame();
        if (backend_owner) {
            // Read complete resident logical patches through the actual selected
            // backend EOS. An eligible refusal is classified in the same
            // resident namespace before rollback; no Host U is materialized.
            const auto rejected = staged
                ? backend_owner->validate_completed_native_eos_batch(*staged,
                    accesses, eos_binding->bounds)
                : backend_owner->validate_completed_native_eos_batch(
                    accesses, eos_binding->bounds);
            require_frame();
            if (rejected) {
                std::size_t index = 0;
                for (; index < accesses.size(); ++index) {
                    const auto& access = accesses[index];
                    if (access.block == rejected->access.block
                        && access.storage == rejected->access.storage
                        && access.slot == rejected->access.slot) break;
                }
                if (index == accesses.size())
                    throw std::logic_error("Native Device EOS refusal belongs to another domain");
                const NativeBoundaryAcceptanceError failure(
                    "Native Device completed boundary EOS rejected at cell "
                        + std::to_string(rejected->diagnostic.index),
                    patches[index].pool_index, frozen_handles[index], slot, version,
                    rejected->diagnostic);
                if (!staged && committed_domain && native_macro_retry_attempt_
                    && native_macro_retry_attempt_->half_ != 0)
                    qualify_native_thermal_rejection(actual, failure, &*rejected);
                throw failure;
            }
        } else for (std::size_t index=0;index<patches.size();++index) {
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
      eos_binding_(runtime.native_rz_eos_binding_),handles_(runtime.stage_handles),side_(context.side),
      backend_(runtime.compute_backend.get()),storage_(backend_?runtime.backend_storage:std::vector<backend::StorageGeneration>{}) {
    if(runtime.geometry_semantics_!=GridMetrics::GeometrySemantics::AxisymmetricRz)return;
    const auto& g=runtime.config.grid;
    if(runtime.bc_handler.has_user()||retry_user_word(runtime.config.physics.gravity.boundary)
        ||retry_user_word(g.x1l_boundary_type)||retry_user_word(g.x1r_boundary_type)
        ||retry_user_word(g.x2l_boundary_type)||retry_user_word(g.x2r_boundary_type)
        ||retry_user_word(g.x3l_boundary_type)||retry_user_word(g.x3r_boundary_type))return;
    const auto& binding=scheduler::current_stage_binding();
    if(runtime.native_macro_retry_attempt_||runtime.runtime_state_transaction_||!attempt
        ||&binding.context!=&context||binding.handles.data()!=handles_.data()||binding.handles.size()!=handles_.size()
        ||&context.ledger!=runtime.residency_ledger.get()||&context.clock!=&runtime.scheduler_clock
        ||(side_==ExecutionSide::Host&&backend_)
        ||(side_==ExecutionSide::Device&&(!backend_||backend_->side()!=ExecutionSide::Device
            ||storage_.size()!=handles_.size()))
        ||(side_!=ExecutionSide::Host&&side_!=ExecutionSide::Device)
        ||!std::isfinite(start_)||!std::isfinite(dt_)||dt_<=0.
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
            ||actual_binding.handles.size()!=handles_.size()||runtime.runtime_state_transaction_
            ||runtime.native_macro_retry_attempt_||runtime.compute_backend.get()!=backend_||context.side!=side_
            ||(backend_&&(backend_->side()!=ExecutionSide::Device||runtime.backend_storage!=storage_))
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
            context.ledger.require_readable(key,{side_,entry_versions[i],true,true});
            const auto coherence=context.ledger.inspect(key);
            if(coherence.interior.pending_transfer!=state::PendingTransferPhase::None
                ||coherence.ghost.pending_transfer!=state::PendingTransferPhase::None)
                throw std::logic_error("Native retry entry has a pending state transfer");
            if(backend_) {
                if(!backend_->contains({handles_[i],storage_[i],StateSlot::Current}))
                    throw std::logic_error("Native retry entry resident storage changed");
            } else require_native_boundary_layout(block.fluid_state,block.grid,runtime.specs.count());
        }
    };
    require_entry();
    if(backend_) {
        std::vector<backend::BackendStateAccess> accesses;
        accesses.reserve(handles_.size());
        for(std::size_t i=0;i<handles_.size();++i)
            accesses.push_back({handles_[i],storage_[i],StateSlot::Current});
        const auto rejected=backend_->validate_completed_native_eos_batch(accesses,eos_binding_->bounds);
        require_entry();
        if(rejected) {
            std::size_t i=0;
            for(;i<accesses.size();++i) {
                const auto& access=accesses[i];
                if(access.block==rejected->access.block&&access.storage==rejected->access.storage
                    &&access.slot==rejected->access.slot)break;
            }
            if(i==accesses.size())
                throw std::logic_error("Native retry entry EOS refusal belongs to another domain");
            throw NativeBoundaryAcceptanceError("Native Device retry entry EOS rejected at cell "
                +std::to_string(rejected->diagnostic.index),active[i],handles_[i],StateSlot::Current,
                entry_versions[i],rejected->diagnostic);
        }
    } else for(const int id:active) {
        const auto& block=runtime.amr_ctrl.pool->GetBlock(id);
        runtime.native_rz_eos_acceptance_(block.fluid_state,block.grid);
    }
    require_entry();
    runtime.native_macro_retry_attempt_=this;enabled_=true;
}
/** Release metadata only after the real outer transaction finished/unwound. */
NativeMacroRetryAttempt::~NativeMacroRetryAttempt() noexcept {
    if(!enabled_)return;
    if(runtime_.native_macro_retry_attempt_!=this||runtime_.runtime_state_transaction_)std::terminate();
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
    const NativeBoundaryAcceptanceError& error,const backend::NativeEosFailure* resident_refusal) {
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
        ||!runtime_state_transaction_||!attempt.enabled_||!attempt.method_||attempt.half_==0
        ||!attempt.eos_binding_||!native_rz_eos_binding_matches(*attempt.eos_binding_)
        ||actual.side!=attempt.side_||compute_backend.get()!=attempt.backend_
        ||(attempt.backend_&&(attempt.backend_->side()!=ExecutionSide::Device
            ||backend_storage!=attempt.storage_||!resident_refusal))
        ||(!attempt.backend_&&resident_refusal)||&actual.ledger!=residency_ledger.get()
        ||&actual.clock!=&scheduler_clock||frame->handles.data()!=stage_handles.data()
        ||frame->handles.size()!=stage_handles.size()||attempt.handles_.data()!=stage_handles.data()
        ||attempt.handles_.size()!=stage_handles.size()||frame->slot!=error.slot||frame->version!=error.version
        ||frame->completion.value!=actual.clock.last_token()||!state::is_complete(frame->completion)
        ||frame->version.value!=actual.clock.last_version()
        ||!retry_same(actual.step_start_time,attempt.start_)||!retry_same(actual.step_dt,attempt.dt_)
        ||!retry_config_matches(config.numerics,attempt.numerics_,config.physics.diffusion,attempt.diffusion_))
        throw std::logic_error("Native thermal refusal lost its live macro/RKL owner");
    runtime_state_transaction_->validate_storage();
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
    runtime_state_transaction_->validate_storage();
    topology_registry.validate_committed_snapshot(observe_topology());
    const auto input_member=TimeIntegration::hydro_boundary_state_member(error.slot);
    for(std::size_t b=0;b<active_before.size();++b) {
        const auto& block=amr_ctrl.pool->GetBlock(active_before[b]);
        if(!native_boundary_patch_matches(patches_before[b],active_before[b],block)
            ||stage_handles[b]!=topology_registry.handle_for_pool(active_before[b]))
            throw std::logic_error("Native thermal refusal changed actual grid/UID correspondence");
        if(attempt.backend_) {
            if(!attempt.backend_->contains({stage_handles[b],attempt.storage_[b],error.slot}))
                throw std::logic_error("Native thermal refusal resident storage changed");
        } else require_native_boundary_layout(block.*input_member,block.grid,specs.count());
        const auto key=state::StateKey{stage_handles[b],error.slot};
        actual.ledger.require_readable(key,{attempt.side_,error.version,true,false});
        const auto coherence=actual.ledger.inspect(key);
        if(coherence.interior.pending_transfer!=state::PendingTransferPhase::None
            ||coherence.ghost.pending_transfer!=state::PendingTransferPhase::None)
            throw std::logic_error("Native thermal refusal has a pending state transfer");
    }
    };
    require_live_failure();
    const auto member=TimeIntegration::hydro_boundary_state_member(error.slot);
    const auto& active=amr_ctrl.tree->GetActiveBlocks();bool requested=false;
    if(attempt.backend_) {
        std::vector<backend::BackendStateAccess> accesses;
        accesses.reserve(active.size());
        std::size_t target=active.size();
        for(std::size_t b=0;b<active.size();++b) {
            accesses.push_back({stage_handles[b],attempt.storage_[b],error.slot});
            if(active[b]==error.pool_index&&stage_handles[b]==error.handle)target=b;
        }
        if(target==active.size()||resident_refusal->access.block!=accesses[target].block
            ||resident_refusal->access.storage!=accesses[target].storage
            ||resident_refusal->access.slot!=accesses[target].slot)
            throw std::logic_error("Native thermal refusal lost its original resident access");
        const auto& original=resident_refusal->diagnostic;
        if(original.phase!=d.phase||original.status!=d.status||original.index!=d.index
            ||original.i!=d.i||original.j!=d.j||original.node!=d.node
            ||original.inertia_mapping_valid!=d.inertia_mapping_valid)
            throw std::logic_error("Native thermal refusal changed its original resident diagnostic");
        const auto& grid=amr_ctrl.pool->GetBlock(active[target]).grid;
        if(d.i<grid.Is()||d.i>=grid.Ie()||d.j<grid.Js()||d.j>=grid.Je())return;
        if(d.index!=grid.GetIndex(d.i,d.j,0))
            throw std::logic_error("Native thermal refusal index does not match its actual active cell");
        const auto classification=attempt.backend_->classify_completed_native_active_thermal(
            accesses,attempt.eos_binding_->bounds,*resident_refusal);
        require_live_failure();
        // A later nonthermal active failure is fatal even when the original
        // requested cell still has the same effective-thermal refusal.
        if(classification.nonthermal_failure) {
            const auto& fatal=*classification.nonthermal_failure;
            std::size_t b=0;
            for(;b<accesses.size();++b) {
                const auto& access=accesses[b];
                if(access.block==fatal.access.block&&access.storage==fatal.access.storage
                    &&access.slot==fatal.access.slot)break;
            }
            if(b==accesses.size())
                throw std::logic_error("Native active EOS classification belongs to another domain");
            throw NativeBoundaryAcceptanceError("Native Device active EOS classification rejected at cell "
                +std::to_string(fatal.diagnostic.index),active[b],stage_handles[b],error.slot,
                error.version,fatal.diagnostic);
        }
        requested=classification.requested_failure;
    } else for(std::size_t b=0;b<active.size();++b) {
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
    if(const char* value=std::getenv("ARCH_TRACE_DIFFUSION_ACTIVITY")) {
        const std::string_view option(value);
        if(option!="0"&&option!="1")
            throw std::invalid_argument("ARCH_TRACE_DIFFUSION_ACTIVITY must be exactly 0 or 1");
        diffusion_activity_enabled_=option=="1";
    }
    diffusion_activity_totals_.process_start_time=ctrl.t_current;
    diffusion_activity_totals_.process_start_step=ctrl.step_count;
    ctrl.repairs.bind_semantics(geometry_semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz
        ? state::RepairSemantics::RzVolumeAngular : state::RepairSemantics::ExistingVolume);
}
DriverRuntime::~DriverRuntime() = default;

/** Read two existing energy planes after the real final BC/EOS gate returned.
 * This records endpoint redistribution, not time-integrated heat or intermediate
 * RKL excursions. Existing repairs/viscous work remain part of accepted E.
 */
void DriverRuntime::observe_completed_diffusion_activity(const scheduler::RklPlan& plan)
{
    if(!diffusion_activity_enabled_)return;
    if(plan.stages.empty())throw std::logic_error("Diffusion activity lacks its actual RKL plan");
    const auto& rotation=plan.final_rotation;
    std::optional<StateSlot> retained_seed;
    for(const auto mapping:{std::pair{StateSlot::Current,rotation.current_from},
            std::pair{StateSlot::Next,rotation.next_from},
            std::pair{StateSlot::Scratch,rotation.scratch_from}}) {
        if(mapping.second==StateSlot::Current) {
            if(retained_seed)throw std::logic_error("RKL rotation duplicated its seed");
            retained_seed=mapping.first;
        }
    }
    if(!retained_seed||*retained_seed==StateSlot::Current)
        throw std::logic_error("RKL rotation did not retain a distinct input Current");
    topology_registry.validate_committed_snapshot(observe_topology());
    const auto& active=amr_ctrl.tree->GetActiveBlocks();
    if(!residency_ledger||active.empty()||active.size()!=stage_handles.size())
        throw std::logic_error("Diffusion activity lost actual leaf/ledger ownership");
    const auto side=compute_backend?ExecutionSide::Device:ExecutionSide::Host;
    for(const auto handle:stage_handles) {
        for(const auto slot:{StateSlot::Current,*retained_seed}) {
            const state::StateKey key{handle,slot};
            const auto coherence=residency_ledger->inspect(key);
            scheduler::detail::require_settled_destination(coherence);
            residency_ledger->require_readable(key,
                {side,coherence.interior.version,true,slot==StateSlot::Current});
        }
    }
    backend::DiffusionActivityReceipt receipt{};
    if(compute_backend) {
        std::vector<backend::BackendStateAccess> accesses;
        accesses.reserve(stage_handles.size());
        for(std::size_t b=0;b<stage_handles.size();++b)
            accesses.push_back(backend_access(b,StateSlot::Current));
        const auto before=compute_backend->counters();
        receipt=compute_backend->reduce_diffusion_energy_activity_batch(accesses,*retained_seed);
        const auto after=compute_backend->counters();
        diffusion_activity_operations_.bytes_h2d+=after.bytes_h2d-before.bytes_h2d;
        diffusion_activity_operations_.bytes_d2h+=after.bytes_d2h-before.bytes_d2h;
        diffusion_activity_operations_.kernel_count+=after.kernel_count-before.kernel_count;
        diffusion_activity_operations_.stream_sync_count+=after.stream_sync_count-before.stream_sync_count;
    } else {
        const auto seed_member=TimeIntegration::hydro_boundary_state_member(*retained_seed);
        long double signed_change=0.,absolute_change=0.;
        for(const int id:active) {
            const auto& block=amr_ctrl.pool->GetBlock(id);
            const auto& grid=block.grid;
            const auto& after=block.fluid_state.eng;
            const auto& before=(block.*seed_member).eng;
            const auto extent=static_cast<std::size_t>(grid.GetTotalSize());
            if(!block.active||after.size()!=extent||before.size()!=extent)
                throw std::logic_error("Diffusion activity energy plane/leaf mismatch");
            const auto geometry=GridMetrics::make_geometry_view(grid,geometry_semantics_);
            for(int k=grid.Ks();k<grid.Ke();++k)
                for(int j=grid.Js();j<grid.Je();++j)
                    for(int i=grid.Is();i<grid.Ie();++i) {
                        const int cell=grid.GetIndex(i,j,k);
                        const double volume=GridMetrics::CellVolume(geometry,i,j,k);
                        const auto term=DiffFlux::diffusion_energy_activity_term(volume,after[cell],before[cell]);
                        if(!term.valid)
                            throw std::runtime_error("Diffusion activity requires a valid shared endpoint energy term");
                        signed_change+=term.signed_energy_change;
                        absolute_change+=term.absolute_energy_change;
                        if(receipt.cells==std::numeric_limits<std::uint64_t>::max())
                            throw std::overflow_error("Diffusion activity cell count exhausted");
                        ++receipt.cells;
                    }
        }
        const auto maximum=static_cast<long double>(std::numeric_limits<double>::max());
        if(!std::isfinite(signed_change)||!std::isfinite(absolute_change)
            ||std::abs(signed_change)>maximum||absolute_change>maximum)
            throw std::overflow_error("Diffusion activity half receipt is not representable");
        receipt.signed_energy_change=static_cast<double>(signed_change);
        receipt.absolute_energy_change=static_cast<double>(absolute_change);
    }
    if(!std::isfinite(receipt.signed_energy_change)||!std::isfinite(receipt.absolute_energy_change)
        ||receipt.absolute_energy_change<0.||receipt.cells==0)
        throw std::runtime_error("Diffusion activity returned an invalid half receipt");
    diffusion_activity_half_=receipt;
}

DiffusionActivityTotals DriverRuntime::prepare_diffusion_activity_promotion(
    std::span<const std::optional<backend::DiffusionActivityReceipt>> halves) const
{
    auto next=diffusion_activity_totals_;
    if(!diffusion_activity_enabled_)return next;
    if(halves.size()!=2)throw std::logic_error("Diffusion activity requires the original two macro halves");
    if(!halves[0]&&!halves[1])return next; // Disabled diffusion contributes nothing.
    if(!halves[0]||!halves[1])throw std::logic_error("Diffusion activity macro lost a half receipt");
    if(next.accepted_macros==std::numeric_limits<std::uint64_t>::max()
        ||next.accepted_halves>std::numeric_limits<std::uint64_t>::max()-2)
        throw std::overflow_error("Diffusion activity accepted count exhausted");
    for(const auto& half:halves) {
        if(!std::isfinite(half->signed_energy_change)||!std::isfinite(half->absolute_energy_change)
            ||half->absolute_energy_change<0.||half->cells==0
            ||next.cells>std::numeric_limits<std::uint64_t>::max()-half->cells)
            throw std::overflow_error("Diffusion activity promotion has an invalid receipt/count");
        next.signed_energy_change+=half->signed_energy_change;
        next.absolute_energy_change+=half->absolute_energy_change;
        next.cells+=half->cells;
    }
    if(!std::isfinite(next.signed_energy_change)||!std::isfinite(next.absolute_energy_change))
        throw std::overflow_error("Diffusion activity process total exhausted");
    next.accepted_halves+=2;++next.accepted_macros;return next;
}
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
    if(runtime_state_transaction_)throw std::logic_error("Active Host Hydro owner excludes topology/backend mutation");
    if (geometry_semantics_ == GridMetrics::GeometrySemantics::AxisymmetricRz
        && !native_rz_eos_acceptance_)
        throw std::logic_error("Native RZ initialization requires an explicitly bound EOS");
    // The native macro snapshots flux storage before the first split operator.
    // Freeze its actual composition extent here, even with AMR disabled; the
    // integrator's later EnsureSpecies cannot resize an already leased arena.
    if(geometry_semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz)
        amr_ctrl.flux_register.EnsureSpecies(specs.count());
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
    if(runtime_state_transaction_)throw std::logic_error("Active Host Hydro owner excludes topology/backend mutation");
    const bool native = geometry_semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz;
    if (native && (compute_backend || !residency_ledger || !native_rz_eos_binding_
        || !native_rz_eos_binding_matches(*native_rz_eos_binding_)))
        throw std::logic_error("Native backend preparation requires its bound EOS and uninstalled committed owner");
    if (native) topology_registry.validate_committed_snapshot(observe_topology());
    const auto& active = amr_ctrl.tree->GetActiveBlocks();
    if (active.empty() || stage_handles.size() != active.size()) {
        throw std::logic_error(
            "CUDA backend requires a complete active topology");
    }
    if (native) {
        // The original per-grid logical plan supplies both seed and signed-axis
        // factory caches; no independent boundary policy is introduced here.
        for (std::size_t index = 0; index < active.size(); ++index) {
            const auto& block = amr_ctrl.pool->GetBlock(active[index]);
            if (stage_handles[index] != topology_registry.handle_for_pool(active[index]))
                throw std::logic_error("Native backend preparation handle/pool order changed");
            (void)GridMetrics::make_geometry_view(block.grid, geometry_semantics_);
            (void)bc_handler.logical_plan(block.grid);
        }
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
            storage, &bc_handler.logical_plan(amr_ctrl.pool->GetBlock(active[index]).grid)});
    }
    return bindings;
}

/** Install a validated compute backend and its resident block views. */
void DriverRuntime::install_backend(std::unique_ptr<backend::ComputeBackend> backend)
{
    if(runtime_state_transaction_)throw std::logic_error("Active Host Hydro owner excludes topology/backend mutation");
    if (compute_backend || !backend) throw std::logic_error("invalid backend installation");
    if (geometry_semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz) {
        if (backend->side()!=ExecutionSide::Device || !residency_ledger
            || !native_rz_eos_binding_
            || !native_rz_eos_binding_matches(*native_rz_eos_binding_))
            throw std::logic_error("Native backend installation requires its actual Device owner and bound EOS");
        topology_registry.validate_committed_snapshot(observe_topology());
        const auto& active=amr_ctrl.tree->GetActiveBlocks();
        if (active.empty() || stage_handles.size()!=active.size()
            || backend_storage.size()!=active.size())
            throw std::logic_error("Native backend installation requires complete committed storage");
        // Factory binding authenticates actual geometry/EOS/allocation; this
        // Runtime preflight proves coverage before publishing the new owner.
        for (std::size_t index=0;index<active.size();++index) {
            const auto& block=amr_ctrl.pool->GetBlock(active[index]);
            if (stage_handles[index]!=topology_registry.handle_for_pool(active[index]))
                throw std::logic_error("Native backend installation handle/pool order changed");
            (void)GridMetrics::make_geometry_view(block.grid,geometry_semantics_);
            (void)bc_handler.logical_plan(block.grid);
            for (const auto slot : {StateSlot::Current,StateSlot::Next,StateSlot::Scratch})
                if (!backend->contains({stage_handles[index],backend_storage[index],slot}))
                    throw std::logic_error("Native backend installation lacks an actual state slot");
        }
    }
    compute_backend = std::move(backend);
}

/** Upload only accepted Current and bind the original immutable transport plans.
 * Native RZ selects the existing angular route even when its route list is empty;
 * this bootstrap does not qualify Device gravity, retries or dynamic regridding.
 */
void DriverRuntime::upload_initial_state()
{
    if(runtime_state_transaction_)throw std::logic_error("Active Host Hydro owner excludes topology/backend mutation");
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
    // Use the actual Runtime chart and the original cache owner. Native
    // Hydro consumes W-weighted torque routes; Existing keeps its false mode.
    const bool angular_transport=geometry_semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz;
    compute_backend->prepare_amr_flux_plan(
        amr_ctrl.RequireFluxTopologyPlan(specs.count(), geometry_semantics_,-1,angular_transport),
        amr_ctrl.RequireRefluxTopologyPlan(specs.count(), geometry_semantics_,angular_transport));
}
} // namespace arch::driver
