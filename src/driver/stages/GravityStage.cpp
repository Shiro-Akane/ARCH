/**
 * @file GravityStage.cpp
 * @brief Lease the correct density generation, solve domain gravity and publish fields.
 *
 * Workflow:
 * 1. Receive a resolved configuration, stage request and current state identity.
 * 2. Lease the correct density generation, solve domain gravity and publish fields.
 * 3. Hand completed state and diagnostics to the next scheduled stage.
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <utility>

#include "driver/stages/GravityStage.h"

#include "amr/AMRControl.h"
#include "amr/elliptic/EllipticMeshAdapter.h"
#include "driver/dispatch/PolicyDescriptor.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/runtime/HostHydroTransaction.h"
#include "numerics/multigrid/CompositeMultigrid.h"
#include "physics/gravity/GravityExecution.h"
#include "physics/gravity/GravitySolveTypes.h"
#include "physics/gravity/NativeExternalStage.h"
#include "physics/gravity/NativeSelfStage.h"
#include "physics/gravity/self/SelfGravity.h"

namespace arch::driver {
/** One owned metadata lease shared by Current/Hydro preparation and optional
 * source inspection. Workflow: freeze the real Runtime/EOS/BC/slot leases;
 * authenticate the actual bound producer before/after its unchanged solve;
 * seal one purpose capability; retire before supported source mutation.
 * Optional inspection additionally checks dense rho and actual operator sites.
 * No fluid array or downloaded density is duplicated. Supported mutation is
 * synchronous and owner-excluded; these checks do not diagnose data races or
 * promise that all non-density conserved values remain bitwise unchanged.
 */
struct GravityStage::RuntimeSourceLease {
    using Field=std::vector<double> FluidState::*;
    static constexpr std::array<Field,7> fields{&FluidState::rho,&FluidState::mom_u,
        &FluidState::mom_v,&FluidState::mom_w,&FluidState::eng,
        &FluidState::enuc_rate,&FluidState::mass_fractions};
    struct Patch {
        int id;const amr::Block* block;const Grid* grid;const FluidState* input;
        amr::BlockHandle handle;state::SlotCoherence coherence;
        GridMetrics::GeometryView geometry;
        arch::grid::ScalarFieldLayout layout;
        std::array<const double*,7> addresses;
        std::array<std::size_t,7> sizes;
        int species,extent;
    };
    struct Observer {
        int left,right,axis,boundary_side;
        bool native_bounds;
        double area;
        std::array<double,3> center,lower,upper;
    };
    GravityStage& owner;
    Physical::Gravity::GravitySolveIdentity identity;
    std::vector<Physical::Gravity::GravityDensityView> owned_views;
    Physical::Gravity::RuntimeGravitySourceLease token;
    const Physical::Gravity::GravitySolveRequest request;
    const state::StateResidencyLedger& ledger;
    const Physical::Gravity::GravityFieldPurpose purpose;
    std::optional<DriverRuntime::NativeRzEosBindingWitness> eos_binding;
    const scheduler::StageBinding* stage_binding=nullptr;
    const scheduler::StageExecutionContext* stage_context=nullptr;
    const amr::BlockHandle* bound_handles=nullptr;
    std::size_t bound_handle_count=0;
    std::optional<scheduler::StageDescriptor> descriptor;
    std::optional<scheduler::HydroMethod> method;
    double step_start=0.,step_dt=0.;
    HostHydroTransaction* transaction_owner=nullptr;
    const amr::MemoryPool* pool;const amr::AmrTree* tree;
    const Physical::Gravity::GravityDensityView* request_blocks_address;
    const Physical::Gravity::GravityInputIdentity* request_identity_address;
    const int* active_address;const amr::BlockHandle* handles_address;
    const amr::BlockHandle* control_handles_address;
    std::vector<int> active;
    std::vector<Patch> patches;
    std::vector<Observer> observers;
    GridConfig root_config;GravityConfig gravity_config;NumericsConfig numerics_config;
    std::array<double,3> physical_bounds;
    BCHandler::StageContextSnapshot boundary_context;
    std::uint64_t clock_token,clock_version;
    const arch::elliptic::CompositePoisson* op=nullptr;
    const amr::EllipticMeshBinding* binding=nullptr;
    const double* density_address=nullptr;
    const void *binding_cells_address=nullptr,*storage_address=nullptr,*grids_address=nullptr,
        *binding_handles_address=nullptr,*operator_cells_address=nullptr,*faces_address=nullptr;
    std::uint64_t source_generation=0;
    bool called=false;

    /** Exact IEEE754 comparison, including signed zero, with no epsilon. */
    static bool same(double a,double b) noexcept {
        return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);
    }
    /** Region identity includes completion/pending state, not just its version. */
    static bool region(const state::RegionCoherence& a,const state::RegionCoherence& b) noexcept {
        return a.residency==b.residency&&a.version==b.version
            &&a.completion==b.completion&&a.pending_transfer==b.pending_transfer;
    }
    /** Preserve the selected slot's exact interior/ghost publication witness. */
    static bool coherence(const state::SlotCoherence& a,const state::SlotCoherence& b) noexcept {
        return region(a.interior,b.interior)&&region(a.ghost,b.ghost)
            &&a.ghost_source_version==b.ghost_source_version;
    }
    /** Match every value field of the actual geometry view, never its padding. */
    static bool geometry(const GridMetrics::GeometryView& a,const GridMetrics::GeometryView& b) noexcept {
        if(a.geometry!=b.geometry||a.dim!=b.dim||a.ng!=b.ng||a.stride_y!=b.stride_y
            ||a.stride_z!=b.stride_z||a.total_size!=b.total_size||a.semantics!=b.semantics
            ||!same(a.dx1,b.dx1)||!same(a.dx2,b.dx2)||!same(a.dx3,b.dx3)
            ||!same(a.x1_min,b.x1_min)||!same(a.x2_min,b.x2_min)||!same(a.x3_min,b.x3_min)
            ||!GridMetrics::equal_identity(a.dyadic_identity,b.dyadic_identity))return false;
        for(int aaxis=0;aaxis<2;++aaxis)
            if(!same(a.actual_block_upper[aaxis],b.actual_block_upper[aaxis]))return false;
        return true;
    }
    /** Preserve all actual service settings, including IEEE signed-zero bits. */
    static bool gravity_configuration(const GravityConfig& a,const GravityConfig& b) noexcept {
        return a==b&&same(a.g_x,b.g_x)&&same(a.g_y,b.g_y)&&same(a.g_z,b.g_z)
            &&same(a.relative_tolerance,b.relative_tolerance)&&same(a.absolute_tolerance,b.absolute_tolerance);
    }
    /** Match actual operator/binding mesh bits, not an ideal reconstructed mesh. */
    static bool mesh_identity(const elliptic::EllipticMesh& a,const elliptic::EllipticMesh& b) noexcept {
        if(a.dimension!=b.dimension||a.cells!=b.cells||a.geometry!=b.geometry||a.semantics!=b.semantics
            ||a.native_canonical_domain!=b.native_canonical_domain)return false;
        for(int axis=0;axis<3;++axis)
            if(!same(a.origin[axis],b.origin[axis])||!same(a.spacing[axis],b.spacing[axis])
                ||!same(a.root_upper[axis],b.root_upper[axis]))return false;
        return true;
    }
    /** Adapt only a private issued capability back to its actual owner. */
    static void check_owner(const void* value,bool preparing) {
        static_cast<const RuntimeSourceLease*>(value)->require_owner(preparing);
    }
    /** Compare the real bound producer geometry on both sides of preparation. */
    static void check_binding(const void* value,const amr::EllipticMeshBinding& binding) {
        static_cast<const RuntimeSourceLease*>(value)->require_binding(binding);
    }
    /** Move source metadata into a nonmoving lifetime covering its field.
     * Workflow: freeze actual EOS/BC/context -> snapshot each original lease;
     * caller attaches this unique owner -> full preflight -> unchanged solve.
     */
    RuntimeSourceLease(GravityStage& stage,Physical::Gravity::GravitySolveIdentity actual,
        std::vector<Physical::Gravity::GravityDensityView> views,
        const state::StateResidencyLedger& actual_ledger,Physical::Gravity::GravityFieldPurpose use)
        :owner(stage),identity(std::move(actual)),owned_views(std::move(views)),
          token(this,stage.generation_,use,&check_owner,&check_binding),
          request{identity,owned_views,use,&token},ledger(actual_ledger),purpose(use),
          eos_binding(stage.runtime_.native_rz_eos_binding_),
          pool(stage.runtime_.control().pool.get()),tree(stage.runtime_.control().tree.get()),
          request_blocks_address(request.blocks.data()),request_identity_address(request.identity.inputs.data()),
          active_address(tree->GetActiveBlocks().data()),handles_address(stage.runtime_.handles().data()),
          control_handles_address(stage.runtime_.control().ActiveHandles().data()),
          active(tree->GetActiveBlocks()),root_config(stage.runtime_.configuration().grid),
          gravity_config(stage.runtime_.configuration().physics.gravity),
          numerics_config(stage.runtime_.configuration().numerics),
          physical_bounds{stage.runtime_.configuration().numerics.sml_rho,
              stage.runtime_.configuration().numerics.min_eint,stage.runtime_.configuration().numerics.max_eint},
          boundary_context(stage.runtime_.boundaries().snapshot_stage_context()),
          clock_token(stage.runtime_.scheduler_clock.last_token()),clock_version(stage.runtime_.scheduler_clock.last_version())
    {
        if(!pool||!tree||active.empty()||active.size()!=identity.inputs.size()
            ||active.size()!=owned_views.size()||active.size()!=stage.runtime_.handles().size())
            throw std::logic_error("Runtime gravity source lease has incomplete actual domain");
        if(purpose==Physical::Gravity::GravityFieldPurpose::HydroStage) {
            const auto& binding=scheduler::current_stage_binding();
            if(!stage.prepared_)throw std::logic_error("Runtime Hydro source lease has no prepared descriptor");
            stage_binding=&binding;stage_context=&binding.context;
            transaction_owner=stage.runtime_.active_host_hydro_transaction();
            bound_handles=binding.handles.data();bound_handle_count=binding.handles.size();
            descriptor=stage.prepared_->descriptor;method=stage.prepared_->method;
            const auto selected=dispatch::parse_registered_policy<dispatch::TimeIntegratorPolicies>(
                stage.runtime_.configuration().numerics.time_integrator);
            const bool exact_method=selected.ok&&((*method==scheduler::HydroMethod::Euler
                &&selected.value==dispatch::TimeIntegratorId::Euler)||(*method==scheduler::HydroMethod::RK2
                &&selected.value==dispatch::TimeIntegratorId::Rk2)||(*method==scheduler::HydroMethod::RK3
                &&selected.value==dispatch::TimeIntegratorId::Rk3));
            if(!exact_method)throw std::logic_error("Runtime gravity Hydro method differs from its selected configuration");
            step_start=binding.context.step_start_time;step_dt=binding.context.step_dt;
        }
        patches.reserve(active.size());
        for(std::size_t p=0;p<active.size();++p) {
            const auto& block=pool->GetBlock(active[p]);const auto& input=selected(block,identity.inputs[p].slot);
            const auto& grid=block.grid;
            std::array<const double*,7> addresses{};std::array<std::size_t,7> sizes{};
            for(std::size_t f=0;f<fields.size();++f) {
                addresses[f]=(input.*fields[f]).data();sizes[f]=(input.*fields[f]).size();
            }
            patches.push_back({active[p],&block,&grid,&input,identity.inputs[p].block,
                ledger.inspect({identity.inputs[p].block,identity.inputs[p].slot}),
                GridMetrics::make_geometry_view(grid,GridMetrics::GeometrySemantics::AxisymmetricRz),
                amr::native_scalar_layout(grid),addresses,sizes,input.GetNumSpecies(),input.block_total_size_});
        }
    }
    RuntimeSourceLease(const RuntimeSourceLease&)=delete;
    RuntimeSourceLease(RuntimeSourceLease&&)=delete;
    /** Constant-cost phase fence; workers retain their existing own-patch gates.
     * The Scheduler legitimately issues an output token after Hydro prepare;
     * only prepare-side fences compare the original clock counter exactly.
     */
    void require_owner(bool preparing) const {
        // Retirement precedes the borrowed Scheduler binding's lifetime end.
        // Reject before inspecting any formerly live context/descriptor.
        if(!token.live_)throw std::logic_error("Runtime gravity source lease was permanently retired");
        const auto& r=owner.runtime_;const auto& config=r.configuration();
        if(!owner.native_candidate()||!owner.gravity_||r.backend()
            ||owner.runtime_source_lease_.get()!=this
            ||r.geometry_semantics()!=GridMetrics::GeometrySemantics::AxisymmetricRz
            ||&ledger!=r.residency_ledger.get()||ledger.active_epoch()!=identity.topology
            ||r.topology_registry.epoch()!=identity.topology||r.control().pool.get()!=pool||r.control().tree.get()!=tree
            ||owner.generation_!=token.generation()||config.grid!=root_config||config.numerics!=numerics_config
            ||!gravity_configuration(config.physics.gravity,gravity_config)
            ||!gravity_configuration(owner.gravity_->config_,gravity_config)
            ||!eos_binding||!r.native_rz_eos_binding_matches(*eos_binding)
            ||!r.boundaries().stage_context_matches(boundary_context)
            ||(preparing&&!owner.source_prepare_running_))
            throw std::logic_error("Runtime gravity source lease lost its actual owner/EOS/configuration");
        const double actual_root[]{config.grid.x1_min,config.grid.x1_max,config.grid.x2_min,
            config.grid.x2_max,config.grid.x3_min,config.grid.x3_max};
        const double frozen_root[]{root_config.x1_min,root_config.x1_max,root_config.x2_min,
            root_config.x2_max,root_config.x3_min,root_config.x3_max};
        for(int a=0;a<6;++a)if(!same(actual_root[a],frozen_root[a]))
            throw std::logic_error("Runtime gravity source lease changed actual root endpoint bits");
        if(!same(config.numerics.sml_rho,physical_bounds[0])||!same(config.numerics.min_eint,physical_bounds[1])
            ||!same(config.numerics.max_eint,physical_bounds[2]))
            throw std::logic_error("Runtime gravity source lease changed physical-bound bits");
        if(purpose==Physical::Gravity::GravityFieldPurpose::AcceptedCurrent) {
            if(r.active_host_hydro_transaction()||owner.journal_active_||owner.prepared_||owner.committed_count_
                ||owner.policy_->prepared_native_self()||owner.policy_->prepared_native_external()
                ||!same(r.ctrl.t_current,identity.input_time)||(!preparing&&owner.source_prepare_running_))
                throw std::logic_error("AcceptedCurrent gravity source lease is not actually quiescent/current");
        } else {
            // OMP consumers borrow the real main-thread binding explicitly;
            // they do not have their own Scheduler TLS binding. Preparation
            // still requires actual TLS identity before any source work.
            if(!stage_binding)throw std::logic_error("Hydro gravity lost its borrowed stage binding");
            const auto& binding=*stage_binding;
            auto* transaction=r.active_host_hydro_transaction();
            if(!owner.native_self()||!transaction||transaction!=transaction_owner
                ||!owner.journal_active_||!owner.prepared_
                ||(preparing && (&scheduler::current_stage_binding()!=stage_binding))
                ||&binding.context!=stage_context
                ||binding.context.side!=state::ExecutionSide::Host
                ||&binding.context.ledger!=&ledger||&binding.context.clock!=&r.scheduler_clock
                ||binding.context.hydro_preparation!=&owner
                ||binding.handles.data()!=bound_handles||binding.handles.size()!=bound_handle_count
                ||!same(binding.context.step_start_time,step_start)||!same(binding.context.step_dt,step_dt)
                ||!descriptor||!method||owner.prepared_->method!=*method
                ||!scheduler::same_stage_descriptor(owner.prepared_->descriptor,*descriptor)
                ||!same(owner.prepared_->input_time,identity.input_time)
                ||!same(owner.prepared_->step_dt,step_dt))
                throw std::logic_error("HydroStage gravity source lease changed its actual journal/context/descriptor");
        }
        if((preparing||purpose==Physical::Gravity::GravityFieldPurpose::AcceptedCurrent)
            &&(r.scheduler_clock.last_token()!=clock_token||r.scheduler_clock.last_version()!=clock_version))
            throw std::logic_error("Runtime gravity preparation/Current clock changed");
    }
    /** Reject unknown slots before dereferencing a fluid member. */
    static const FluidState& selected(const amr::Block& b,state::StateSlot slot) {
        if(slot==state::StateSlot::Current)return b.fluid_state;
        if(slot==state::StateSlot::Next)return b.state_next;
        if(slot==state::StateSlot::Scratch)return b.state_scratch;
        throw std::logic_error("Native source inspection received an unknown input slot");
    }
    /** Reconcile registry and exact borrowed spans against actual live Runtime. */
    void require_domain(bool preparing=false) const {
        require_owner(preparing);
        const auto& r=owner.runtime_;const auto& config=r.configuration();
        if(!owner.native_candidate()
            ||r.backend()||r.geometry_semantics()!=GridMetrics::GeometrySemantics::AxisymmetricRz
            ||&ledger!=r.residency_ledger.get()||ledger.active_epoch()!=identity.topology
            ||r.topology_registry.epoch()!=identity.topology||r.control().pool.get()!=pool||r.control().tree.get()!=tree
            ||tree->GetActiveBlocks().data()!=active_address||tree->GetActiveBlocks()!=active
            ||r.handles().data()!=handles_address||r.handles().size()!=active.size()
            ||r.control().ActiveHandles().data()!=control_handles_address
            ||r.control().ActiveHandles().size()!=active.size()
            ||identity.inputs.size()!=active.size()||request.blocks.size()!=active.size()||request.blocks.data()!=request_blocks_address
            ||request.identity.inputs.data()!=request_identity_address||request.identity!=identity
            ||!same(request.identity.input_time,identity.input_time)
            ||!same(request.identity.gravitational_constant,identity.gravitational_constant)
            ||identity.gravitational_constant!=constants::gravity::cgs::gravitational_constant
            ||config.grid!=root_config||!gravity_configuration(config.physics.gravity,gravity_config)
            ||!gravity_configuration(owner.gravity_->config_,config.physics.gravity)
            ||!r.boundaries().stage_context_matches(boundary_context))
            throw std::logic_error("Native source inspection lost its actual Runtime domain/configuration");
        const double actual_root[]{config.grid.x1_min,config.grid.x1_max,config.grid.x2_min,
            config.grid.x2_max,config.grid.x3_min,config.grid.x3_max};
        const double frozen_root[]{root_config.x1_min,root_config.x1_max,root_config.x2_min,
            root_config.x2_max,root_config.x3_min,root_config.x3_max};
        for(int a=0;a<6;++a)if(!same(actual_root[a],frozen_root[a]))
            throw std::logic_error("Native source inspection changed actual root endpoint bits");
        if(!same(config.numerics.sml_rho,physical_bounds[0])||!same(config.numerics.min_eint,physical_bounds[1])
            ||!same(config.numerics.max_eint,physical_bounds[2])
            ||!same(config.physics.gravity.relative_tolerance,gravity_config.relative_tolerance)
            ||!same(config.physics.gravity.absolute_tolerance,gravity_config.absolute_tolerance)
            ||!same(config.physics.gravity.g_x,gravity_config.g_x)
            ||!same(config.physics.gravity.g_y,gravity_config.g_y)
            ||!same(config.physics.gravity.g_z,gravity_config.g_z))
            throw std::logic_error("Native source inspection changed frozen numeric identity");
        r.topology_registry.validate_committed_snapshot(r.observe_topology());
        for(std::size_t p=0;p<active.size();++p) {
            const auto& input=identity.inputs[p];const auto handle=r.topology_registry.handle_for_pool(active[p]);
            if(handle!=input.block||r.handles()[p]!=handle||r.control().ActiveHandles()[p]!=handle
                ||input.storage_generation!=owner.generation_)
                throw std::logic_error("Native source inspection changed handle/slot/density lease");
            ledger.require_readable({handle,input.slot},{state::ExecutionSide::Host,input.version,true,false});
            const auto current=ledger.inspect({handle,input.slot});
            if(current.interior.pending_transfer!=state::PendingTransferPhase::None
                ||current.ghost.pending_transfer!=state::PendingTransferPhase::None)
                throw std::logic_error("Native source inspection has a pending input transfer");
            if(patches.size()!=active.size())throw std::logic_error("Runtime source lease patch extent changed");
            const auto& expected=patches[p];const auto& block=pool->GetBlock(active[p]);
            const auto& grid=block.grid;const auto& input_state=selected(block,input.slot);
            block.RequireNativeGeometryIdentity();
            if(&block!=expected.block||!block.active||block.id!=expected.id||block.active_index!=int(p)
                ||&grid!=expected.grid||&input_state!=expected.input
                ||!geometry(GridMetrics::make_geometry_view(grid,r.geometry_semantics()),expected.geometry)
                ||amr::native_scalar_layout(grid)!=expected.layout||input_state.GetNumSpecies()!=expected.species
                ||input_state.block_total_size_!=expected.extent||input_state.block_total_size_!=grid.GetTotalSize()
                ||!coherence(current,expected.coherence)
                ||!GridMetrics::matches_identity(grid.dyadic_identity,{grid.x1_min,grid.x2_min},
                    {grid.x1_max,grid.x2_max},{grid.dx1,grid.dx2}))
                throw std::logic_error("Runtime gravity source lease changed patch/Grid/layout/publication");
            const auto& view=request.blocks[p];
            if(view.identity!=input||view.density.data!=input_state.rho.data()||view.density.size!=input_state.rho.size()
                ||view.density.layout!=expected.layout||view.density.memory!=arch::grid::FieldMemory::Host
                ||view.density.storage_generation!=input.storage_generation)
                throw std::logic_error("Runtime gravity source lease changed its actual density view");
            for(std::size_t f=0;f<fields.size();++f)
                if((input_state.*fields[f]).data()!=expected.addresses[f]||(input_state.*fields[f]).size()!=expected.sizes[f])
                    throw std::logic_error("Runtime gravity source lease changed one of seven storage allocations");
            if(purpose==Physical::Gravity::GravityFieldPurpose::AcceptedCurrent&&input.slot!=state::StateSlot::Current)
                throw std::logic_error("AcceptedCurrent gravity lease borrowed another slot");
            if(purpose==Physical::Gravity::GravityFieldPurpose::HydroStage&&input.slot!=descriptor->input_slot)
                throw std::logic_error("HydroStage gravity lease borrowed another input slot");
        }
    }
    /** Compare the bound producer against the same live Runtime source.
     * This is metadata traversal only. It rejects same-epoch drift instead of
     * recomputing another mesh or pretending epoch alone proves geometry.
     */
    void require_binding(const amr::EllipticMeshBinding& bound) const {
        require_domain(true);
        const auto& base=bound.base;
        if(!base.native_canonical_domain||base.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
            ||base.dimension!=2||base.geometry!=elliptic::Geometry::Cylindrical
            ||base.cells[0]!=root_config.nblockx1*amr::BLOCK_NX
            ||base.cells[1]!=root_config.nblockx2*amr::BLOCK_NY
            ||bound.handles.size()!=patches.size()||bound.grids.size()!=patches.size()
            ||bound.periodic!=std::array<bool,3>{root_config.x1l_boundary_type=="periodic",
                root_config.x2l_boundary_type=="periodic",false})
            throw std::logic_error("Runtime gravity binding differs from its actual Native source");
        const double lower[]{root_config.x1_min,root_config.x2_min,root_config.x3_min};
        const double upper[]{root_config.x1_max,root_config.x2_max,root_config.x3_max};
        for(int a=0;a<3;++a)if(!same(base.origin[a],lower[a])||!same(base.root_upper[a],upper[a]))
            throw std::logic_error("Runtime gravity binding changed actual root bounds at the same epoch");
        for(int a=0;a<2;++a)if(!same(base.spacing[a],(upper[a]-lower[a])/base.cells[a]))
            throw std::logic_error("Runtime gravity binding changed actual root spacing at the same epoch");
        std::size_t c=0;
        for(std::size_t p=0;p<patches.size();++p) {
            const auto& patch=patches[p];const auto& block=pool->GetBlock(active[p]);const auto& grid=*patch.grid;
            if(bound.grids[p]!=patch.grid||bound.handles[p]!=patch.handle)
                throw std::logic_error("Runtime gravity binding changed actual Grid/handle owner");
            for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i,++c) {
                if(c>=bound.cells.size()||c>=bound.storage.size()
                    ||bound.storage[c].block!=p||bound.storage[c].offset!=grid.GetIndex(i,j,0)
                    ||bound.cells[c].level!=block.level
                    ||bound.cells[c].index!=std::array<int,3>{int(block.logical_x1)*amr::BLOCK_NX+i-grid.Is(),
                        int(block.logical_x2)*amr::BLOCK_NY+j-grid.Js(),0})
                    throw std::logic_error("Runtime gravity binding changed the real active-cell source mapping");
            }
        }
        if(c!=bound.cells.size()||c!=bound.storage.size())
            throw std::logic_error("Runtime gravity binding has omitted/extra source cells");
    }
    /** Authenticate dense source order and true operator/native Grid edge bits.
     * Observer snapshots retain actual rounded producer sites and fragments.
     * No ideal coordinate arithmetic, volume inversion or rho-from-RHS appears.
     */
    void require_view(const Physical::Gravity::NativeRzSourceInspectionView& view) const {
        if(owner.qualification_!=Qualification::NativeRzCandidate||!owner.source_inspection_active_
            ||purpose!=Physical::Gravity::GravityFieldPurpose::AcceptedCurrent)
            throw std::logic_error("Native source callback has no actual Current inspection scope");
        require_domain(true);owner.gravity_->require_native_source_inspection(view);
        const auto& mesh=view.op.base();
        if(&view.request!=&request||&view.service_configuration!=&owner.gravity_->config_
            ||!mesh_identity(view.binding.base,mesh)
            ||view.binding.periodic!=std::array<bool,3>{root_config.x1l_boundary_type=="periodic",
                root_config.x2l_boundary_type=="periodic",false}
            ||view.binding.handles.size()!=patches.size()
            ||view.binding.grids.size()!=patches.size()||view.binding.storage.size()!=view.density.size()
            ||view.binding.cells!=view.op.cells()||view.density.size()!=static_cast<std::size_t>(view.op.size())
            ||!mesh.native_canonical_domain||mesh.dimension!=2||mesh.geometry!=elliptic::Geometry::Cylindrical
            ||mesh.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
            ||!same(mesh.origin[0],root_config.x1_min)||!same(mesh.origin[1],root_config.x2_min)
            ||!same(mesh.root_upper[0],root_config.x1_max)||!same(mesh.root_upper[1],root_config.x2_max)
            ||mesh.cells[0]!=root_config.nblockx1*amr::BLOCK_NX||mesh.cells[1]!=root_config.nblockx2*amr::BLOCK_NY)
            throw std::logic_error("Native source inspection changed actual materialized geometry/source extent");
        if(called&&(&view.op!=op||&view.binding!=binding||view.density.data()!=density_address
            ||view.source_generation!=source_generation||view.op.faces().size()!=observers.size()
            ||view.binding.cells.data()!=binding_cells_address||view.binding.storage.data()!=storage_address
            ||view.binding.grids.data()!=grids_address||view.binding.handles.data()!=binding_handles_address
            ||view.op.cells().data()!=operator_cells_address||view.op.faces().data()!=faces_address))
            throw std::logic_error("Native source inspection changed producer view/source generation");
        std::size_t cell=0;
        for(std::size_t p=0;p<patches.size();++p) {
            const auto& expected=patches[p];const auto& block=pool->GetBlock(active[p]);const auto& grid=block.grid;
            const auto& input=selected(block,identity.inputs[p].slot);const auto& read=request.blocks[p];
            block.RequireNativeGeometryIdentity();
            if(&block!=expected.block||!block.active||block.id!=expected.id||block.active_index!=int(p)
                ||&grid!=expected.grid||&input!=expected.input||view.binding.grids[p]!=&grid
                ||view.binding.handles[p]!=expected.handle||!grid.dyadic_identity.bound
                ||grid.dyadic_identity.root_blocks!=std::array<int,2>{root_config.nblockx1,root_config.nblockx2}
                ||!same(grid.dyadic_identity.root_lower[0],root_config.x1_min)
                ||!same(grid.dyadic_identity.root_upper[0],root_config.x1_max)
                ||!same(grid.dyadic_identity.root_lower[1],root_config.x2_min)
                ||!same(grid.dyadic_identity.root_upper[1],root_config.x2_max)
                ||grid.dyadic_identity.periodic_axial!=view.binding.periodic[1]
                ||!geometry(GridMetrics::make_geometry_view(grid,mesh.semantics),expected.geometry)
                ||amr::native_scalar_layout(grid)!=expected.layout||input.GetNumSpecies()!=expected.species
                ||input.block_total_size_!=expected.extent||input.block_total_size_!=grid.GetTotalSize()
                ||!coherence(ledger.inspect({expected.handle,identity.inputs[p].slot}),expected.coherence)
                ||read.identity!=identity.inputs[p]||read.density.memory!=arch::grid::FieldMemory::Host
                ||read.density.layout!=expected.layout||read.density.data!=input.rho.data()
                ||read.density.size!=input.rho.size()||read.density.storage_generation!=identity.inputs[p].storage_generation)
                throw std::logic_error("Native source inspection changed actual patch/layout/publication");
            for(std::size_t f=0;f<fields.size();++f)
                if((input.*fields[f]).data()!=expected.addresses[f]||(input.*fields[f]).size()!=expected.sizes[f])
                    throw std::logic_error("Native source inspection changed one of seven input storage leases");
            if(!GridMetrics::matches_identity(grid.dyadic_identity,{grid.x1_min,grid.x2_min},
                    {grid.x1_max,grid.x2_max},{grid.dx1,grid.dx2}))
                throw std::logic_error("Native source inspection has incoherent bound Grid endpoints/spacing");
            for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i,++cell) {
                if(cell>=view.density.size())throw std::logic_error("Native source inspection omitted a real active cell");
                const auto location=view.binding.storage[cell];const auto& key=view.op.cells()[cell];
                const int offset=grid.GetIndex(i,j,0);
                if(location.block!=p||location.offset!=offset||key.level!=block.level
                    ||key.index!=std::array<int,3>{int(block.logical_x1)*amr::BLOCK_NX+i-grid.Is(),
                        int(block.logical_x2)*amr::BLOCK_NY+j-grid.Js(),0}
                    ||!same(view.density[cell],input.rho[offset])||!std::isfinite(view.density[cell])||!(view.density[cell]>0.)
                    ||!same(view.op.lower(int(cell),0),grid.GetFacePosL(i))
                    ||!same(view.op.upper(int(cell),0),grid.GetFacePosR(i))
                    ||!same(view.op.lower(int(cell),1),grid.GetAxialFacePosL(j))
                    ||!same(view.op.upper(int(cell),1),grid.GetAxialFacePosR(j))
                    ||!same(view.op.center(int(cell))[0],grid.GetCellCenterX(i))
                    ||!same(view.op.center(int(cell))[1],grid.GetCellCenterY(j)))
                    throw std::logic_error("Native source inspection differs from actual dense rho/Grid source edges");
            }
        }
        if(cell!=view.density.size())throw std::logic_error("Native source inspection contains non-native extra source cells");
        if(called)for(std::size_t f=0;f<observers.size();++f) {
            const auto& actual=view.op.faces()[f];const auto& expected=observers[f];
            if(actual.left!=expected.left||actual.right!=expected.right||actual.axis!=expected.axis
                ||actual.boundary_side!=expected.boundary_side||actual.native_bounds!=expected.native_bounds
                ||!same(actual.area,expected.area))
                throw std::logic_error("Native source inspection changed actual observer/fragment identity");
            for(int a=0;a<3;++a)if(!same(actual.center[a],expected.center[a])
                ||!same(actual.fragment_lower[a],expected.lower[a])||!same(actual.fragment_upper[a],expected.upper[a]))
                throw std::logic_error("Native source inspection changed actual rounded observer site/bounds");
        }
        require_domain(true);
    }
    /** Freeze exactly the already-built actual producer observer metadata. */
    void freeze_observers(const Physical::Gravity::NativeRzSourceInspectionView& view) {
        observers.reserve(view.op.faces().size());
        for(const auto& f:view.op.faces())observers.push_back({f.left,f.right,f.axis,f.boundary_side,
            f.native_bounds,f.area,f.center,f.fragment_lower,f.fragment_upper});
        op=&view.op;binding=&view.binding;density_address=view.density.data();source_generation=view.source_generation;
        binding_cells_address=view.binding.cells.data();storage_address=view.binding.storage.data();
        grids_address=view.binding.grids.data();binding_handles_address=view.binding.handles.data();
        operator_cells_address=view.op.cells().data();faces_address=view.op.faces().data();
        called=true;
    }
};

/** Configure one optional internal source-only sink while no work is prepared.
 * Clearing or replacing resets the source completion marker but never source
 * lease/counters, field readiness, numerical tolerances or public capability.
 */
void GravityStage::set_native_rz_source_inspection(NativeSourceInspectionSink sink,void* payload) {
    if(source_prepare_running_)throw std::logic_error("Active source lease forbids replacing its inspection sink");
    source_inspection_completed_=false;
    if(qualification_!=Qualification::NativeRzCandidate||!gravity_||runtime_.backend()
        ||journal_active_||prepared_||source_inspection_active_||runtime_.active_host_hydro_transaction())
        throw std::logic_error("Native source inspection requires a quiescent actual Native candidate stage");
    if(!sink&&payload)throw std::invalid_argument("Native source inspection payload requires its sink");
    source_inspection_sink_=sink;source_inspection_payload_=payload;
}
/** Attach or clear SAME-flux/work diagnostics only before a macro journal.
 * Neither live-frame payload replacement, unflushed accepted rows nor concurrent
 * source inspection is permitted. Replacing a nonempty pair requires explicit
 * clear; the same pair is idempotent. Each later actual Frame freezes the pair; no
 * configuration parameter, field grant or numerical producer is introduced.
 */
void GravityStage::set_native_self_flux_observation(NativeSelfFluxObservationSink sink,void* payload) {
    if(source_prepare_running_)throw std::logic_error("Active source lease forbids replacing its flux observer");
    if(!native_self()||!gravity_||runtime_.backend()||journal_active_||prepared_||committed_count_
        ||runtime_.active_host_hydro_transaction()||source_inspection_active_
        ||source_inspection_sink_||source_inspection_payload_
        ||gravity_->native_source_inspection_sink_||gravity_->native_source_inspection_payload_
        ||gravity_->native_source_inspection_running_||policy_->prepared_native_self()
        ||policy_->prepared_native_external()
        ||(self_frame_&&self_frame_->live_.load(std::memory_order_acquire)))
        throw std::logic_error("Native self flux observation requires a quiescent actual private service");
    if(!sink&&payload)throw std::invalid_argument("Native self observation payload requires its sink");
    if(native_flux_observation_sink_&&sink
        &&(sink!=native_flux_observation_sink_||payload!=native_flux_observation_payload_))
        throw std::logic_error("Native self observation owner must be explicitly cleared before replacement");
    native_flux_observation_sink_=sink;native_flux_observation_payload_=payload;
}
/** Runtime authentication surrounds the internal callback; publish no marker
 * on callback/lease failure. Only copied pending local records are permitted
 * inside the callback; the formatter waits for completed() after this returns.
 */
void GravityStage::inspect_native_source(void* payload,const Physical::Gravity::NativeRzSourceInspectionView& view) {
    auto& inspection=*static_cast<NativeSourceInspection*>(payload);
    auto& owner=inspection.owner;
    owner.source_inspection_completed_=false;
    try {
        if(inspection.called||!owner.source_inspection_sink_)
            throw std::logic_error("Native source inspection callback was repeated or detached");
        inspection.require_view(view);inspection.freeze_observers(view);
        owner.source_inspection_sink_(owner.source_inspection_payload_,view);
        inspection.require_view(view);owner.gravity_->require_native_source_inspection(view);
        owner.source_inspection_completed_=true;
    } catch(...) {
        owner.source_inspection_completed_=false;owner.invalidate();throw;
    }
}
/** Open diagnostics for a configured self-gravity stage. */
GravityStage::GravityStage(DriverRuntime& runtime,const Physical::Gravity::IGravityPolicy* policy,
    Qualification qualification)
    :qualification_(qualification),runtime_(runtime),policy_(policy),gravity_(dynamic_cast<const Physical::Gravity::SelfGravity*>(policy)) {
    if(qualification_!=Qualification::Production&&qualification_!=Qualification::NativeRzCandidate
        &&qualification_!=Qualification::NativeRzExternalCandidate&&!native_self())
        throw std::invalid_argument("Unknown gravity stage qualification");
    if(native_candidate()
        &&(!gravity_||runtime_.backend()
            ||runtime_.geometry_semantics()!=GridMetrics::GeometrySemantics::AxisymmetricRz))
        throw std::invalid_argument("Native RZ stage verification requires a CPU RZ Runtime");
    if(native_self()&&(runtime_.configuration().physics.gravity.type!="self"
        ||policy_->prepared_native_self()||policy_->prepared_native_external()))
        throw std::invalid_argument("Native Self Hydro requires an unowned actual self service");
    if(native_external()) {
        if(!policy_||gravity_||runtime_.backend()
            ||runtime_.geometry_semantics()!=GridMetrics::GeometrySemantics::AxisymmetricRz
            ||runtime_.configuration().physics.gravity.type!="external")
            throw std::invalid_argument("Native external stage requires its actual CPU RZ Runtime/configuration");
        if(policy_->prepared_native_external())
            throw std::logic_error("Native external policy already has another actual frame owner");
        const auto source=policy_->source_descriptor();
        const auto& input=runtime_.configuration().physics.gravity;
        const auto same=[](double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);};
        if(source.origin!=Physical::Gravity::GravitySourceOrigin::NativeExternalOrthonormal
            ||!source.external.enabled||!std::isfinite(input.g_x)||!std::isfinite(input.g_y)
            ||!std::isfinite(input.g_z)||!same(source.external.g_x,input.g_x)
            ||!same(source.external.g_y,input.g_y)||!same(source.external.g_z,input.g_z)
            ||(runtime_.configuration().grid.x1_min==0.&&(input.g_x!=0.||input.g_z!=0.)))
            throw std::invalid_argument("Native external acceleration is mismatched or nonregular at the axis");
        return;
    }
    if (!gravity_) return;
    const auto& config=runtime.configuration();
    std::filesystem::create_directories(config.io.out_dir);
    diagnostics_.open(config.io.out_dir+(native_candidate()
        ?"/native_rz_candidates.tsv":"/gravity_solves.tsv"));
    if (!diagnostics_) throw std::runtime_error("Cannot open gravity solve diagnostics");
    if(native_candidate()) {
        diagnostics_<<"time\tstage\tepoch\tlease\tcells\tsource_generation\tresidual_upper\ttolerance_safe\tphysical_qualified\n"
            <<std::setprecision(17);
        // Publish the schema even when the first genuine macro is rejected.
        // No tentative stage rows become visible before numerical commit.
        diagnostics_.flush();
        if(!diagnostics_)throw std::runtime_error("Cannot publish native gravity diagnostic schema");
        return;
    }
    if(config.physics.gravity.boundary=="user") {
        boundary_diagnostics_.open(config.io.out_dir+"/gravity_boundary_exchange.tsv");
        if(!boundary_diagnostics_) throw std::runtime_error("Cannot open gravity boundary diagnostics");
        boundary_diagnostics_ << "# scope=since-process-start; fields=successive-publications; "
            "energy=half-integral-rho-Phi; exchange=Green-boundary-term; gauge=solver-policy; "
            "time-may-follow-RK-stage-order; units=CGS-with-GridMetrics-measure\n"
            "time\tprevious_time\tstage\tpotential_energy\tdelta_potential_energy\tboundary_exchange\tcumulative_exchange\tfaces\tobserver_seconds\tkernels\tbytes_h2d\tbytes_d2h\tsynchronizations\n"
            <<std::setprecision(17);
    }
    diagnostics_<<"time\tstage\tepoch\tgeneration\tcells\titerations\trhs_rms\tresidual\ttarget\trho_mean\tdevice\tsetup_seconds\tsolve_seconds\tkernels\tbytes_h2d\tbytes_d2h\tsynchronizations\tsource_boundary_seconds\tpoisson_seconds\tforce_seconds\n"<<std::setprecision(17);
    // Publish the schema once, including runs rejected before their first step.
    diagnostics_.flush();
    if(boundary_diagnostics_.is_open())boundary_diagnostics_.flush();
    if(!diagnostics_||(boundary_diagnostics_.is_open()&&!boundary_diagnostics_))
        throw std::runtime_error("Cannot publish gravity diagnostic schema");
}
/** Retire a borrowed native source before its nonmoving metadata owner dies. */
GravityStage::~GravityStage() {
    // A borrowed numerical workspace cannot outlive its issued metadata owner.
    if(runtime_source_lease_||native_external()||native_self())invalidate();
}

/** Prepare a body source from the actual live Runtime transaction, not a field.
 * All seven input leases and complete boundary frames are captured before
 * attaching; there is no fake Poisson identity, potential or source solve.
 */
state::CompletionToken GravityStage::prepare_native_external(
    const scheduler::HydroStagePreparationRequest& request) {
    const auto& binding=scheduler::current_stage_binding();
    auto* transaction=runtime_.active_host_hydro_transaction();
    if(!journal_active_||!prepared_||!transaction||runtime_.backend()
        ||request.side!=state::ExecutionSide::Host||&request.ledger!=&binding.context.ledger
        ||&binding.context.ledger!=runtime_.residency_ledger.get()
        ||&binding.context.clock!=&runtime_.scheduler_clock
        ||request.input_time!=binding.context.step_start_time
            +request.descriptor.input_time_fraction*binding.context.step_dt
        ||request.step_dt!=binding.context.step_dt
        ||runtime_.geometry_semantics()!=GridMetrics::GeometrySemantics::AxisymmetricRz
        ||runtime_.boundaries().geometry_semantics()!=runtime_.geometry_semantics()
        ||runtime_.configuration().physics.gravity.type!="external")
        throw std::logic_error("Native external preparation is outside its actual Runtime stage");
    transaction->require_source_preparation_owner(*this,binding.context,request.handles);
    if(policy_->prepared_native_external()
        &&policy_->prepared_native_external()!=external_frame_.get())
        throw std::logic_error("Native external policy already has another actual frame owner");
    runtime_.topology_registry.validate_committed_snapshot(runtime_.observe_topology());
    const auto description=policy_->source_descriptor();
    const auto& config=runtime_.configuration();const auto& g=config.physics.gravity;
    const auto same=[](double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);};
    if(description.origin!=Physical::Gravity::GravitySourceOrigin::NativeExternalOrthonormal
        ||!description.external.enabled||!std::isfinite(g.g_x)||!std::isfinite(g.g_y)||!std::isfinite(g.g_z)
        ||!same(description.external.g_x,g.g_x)||!same(description.external.g_y,g.g_y)
        ||!same(description.external.g_z,g.g_z))
        throw std::logic_error("Native external policy disagrees with its frozen Runtime configuration");
    for(int id:runtime_.control().tree->GetActiveBlocks()) {
        const auto& block=runtime_.control().pool->GetBlock(id);block.RequireNativeGeometryIdentity();
        const auto& root=block.grid.dyadic_identity;
        if(!same(root.root_lower[0],config.grid.x1_min)||!same(root.root_upper[0],config.grid.x1_max)
            ||!same(root.root_lower[1],config.grid.x2_min)||!same(root.root_upper[1],config.grid.x2_max)
            ||root.root_blocks[0]!=config.grid.nblockx1||root.root_blocks[1]!=config.grid.nblockx2
            ||(root.root_lower[0]==0.&&(g.g_x!=0.||g.g_z!=0.)))
            throw std::logic_error("Native external actual root differs from its configured regular domain");
    }
    invalidate();
    if(generation_==std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("Native external stage generation exhausted");
    ++generation_; // Attempted generations are monotonic, including rollback.
    std::unique_ptr<Physical::Gravity::NativeExternalStageFrame> next{
        new Physical::Gravity::NativeExternalStageFrame(*policy_,runtime_.boundaries(),
            runtime_.control(),binding,request.descriptor,description.external,config,request.step_dt,generation_)};
    external_frame_=std::move(next);policy_->native_external_frame_=external_frame_.get();
    return {generation_,state::CompletionState::Complete};
}
/** Authenticate the actual Runtime macro and root before any Self solve.
 * A configuration enum or source callback is insufficient: the transaction,
 * binding, ledger, clock and committed native root must be the original owners.
 */
void GravityStage::require_native_self_preparation(
    const scheduler::HydroStagePreparationRequest& request) const {
    const auto& binding=scheduler::current_stage_binding();
    auto* transaction=runtime_.active_host_hydro_transaction();
    if(!native_self()||!journal_active_||!prepared_||!gravity_||!transaction||runtime_.backend()
        ||request.side!=state::ExecutionSide::Host||&request.ledger!=&binding.context.ledger
        ||&binding.context.ledger!=runtime_.residency_ledger.get()
        ||&binding.context.clock!=&runtime_.scheduler_clock
        ||request.input_time!=binding.context.step_start_time
            +request.descriptor.input_time_fraction*binding.context.step_dt
        ||request.step_dt!=binding.context.step_dt
        ||runtime_.geometry_semantics()!=GridMetrics::GeometrySemantics::AxisymmetricRz
        ||runtime_.boundaries().geometry_semantics()!=runtime_.geometry_semantics()
        ||runtime_.configuration().physics.gravity.type!="self"
        ||source_inspection_sink_||source_inspection_active_
        ||policy_->prepared_native_external()
        ||(policy_->prepared_native_self()&&policy_->prepared_native_self()!=self_frame_.get()))
        throw std::logic_error("Native Self preparation is outside its actual Runtime stage");
    transaction->require_source_preparation_owner(*this,binding.context,request.handles);
    runtime_.topology_registry.validate_committed_snapshot(runtime_.observe_topology());
    const auto& config=runtime_.configuration();
    const auto same=[](double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);};
    for(int id:runtime_.control().tree->GetActiveBlocks()) {
        const auto& block=runtime_.control().pool->GetBlock(id);block.RequireNativeGeometryIdentity();
        const auto& root=block.grid.dyadic_identity;
        if(!same(root.root_lower[0],config.grid.x1_min)||!same(root.root_upper[0],config.grid.x1_max)
            ||!same(root.root_lower[1],config.grid.x2_min)||!same(root.root_upper[1],config.grid.x2_max)
            ||root.root_blocks[0]!=config.grid.nblockx1||root.root_blocks[1]!=config.grid.nblockx2)
            throw std::logic_error("Native Self actual root differs from its configured domain");
    }
}
/** Lease the exact RK input density generation and publish its solved field. */
state::CompletionToken GravityStage::solve(state::StateSlot slot,const state::StateResidencyLedger& ledger,
    double time,int stage,Physical::Gravity::GravityFieldPurpose purpose) {
    if(source_inspection_active_||source_prepare_running_)
        throw std::logic_error("Gravity source lease forbids synchronous stage solve reentry");
    if(!Physical::Gravity::valid_gravity_field_purpose(purpose)
        ||(slot!=state::StateSlot::Current&&slot!=state::StateSlot::Next&&slot!=state::StateSlot::Scratch))
        throw std::invalid_argument("Gravity source request has an unknown purpose/slot");
    source_inspection_completed_=false;
    const auto start=std::chrono::steady_clock::now();
    invalidate();
    auto* backend=runtime_.backend();
    if(native_candidate()&&backend)
        throw std::logic_error("Native RZ stage verification cannot execute on Device");
    if(backend)gravity_->set_execution(backend->gravity_execution());
    const auto& handles=runtime_.handles(); const auto& config=runtime_.configuration();
    if (handles.empty()) throw std::logic_error("Gravity requires active topology");
    if(native_candidate()&&(!runtime_.control().pool||!runtime_.control().tree))
        throw std::logic_error("Native gravity source lease has no actual pool/tree owner");
    if (epoch_!=handles.front().epoch) {
        auto binding=amr::bind_elliptic_mesh(runtime_.control(),config.grid,handles);
        if(native_candidate())
            gravity_->bind_native_rz_candidate(std::move(binding),65536,0);
        else gravity_->bind(std::move(binding),time);
        epoch_=handles.front().epoch;
    }
    // A new borrowed storage lease is issued for every solve, even if slots or
    // pool addresses are reused. No publication can survive an expired lease.
    if (++generation_==0) throw std::overflow_error("Gravity density lease exhausted");
    Physical::Gravity::GravitySolveIdentity identity;
    identity.topology=epoch_; identity.input_time=time; identity.gravitational_constant=arch::constants::gravity::cgs::gravitational_constant;
    identity.operator_revision=1;identity.boundary_revision=1;identity.accuracy_revision=1;
    std::vector<Physical::Gravity::GravityDensityView> views;
    const auto& active=runtime_.control().tree->GetActiveBlocks();
    for (std::size_t b=0;b<handles.size();++b) {
        const auto version=ledger.inspect({handles[b],slot}).interior.version;
        ledger.require_readable({handles[b],slot},{backend?state::ExecutionSide::Device:state::ExecutionSide::Host,version,true,false});
        const auto& block=runtime_.control().pool->GetBlock(active[b]);
        const auto& fluid=slot==state::StateSlot::Current?block.fluid_state:slot==state::StateSlot::Next?block.state_next:block.state_scratch;
        const auto& grid=block.grid;
        Physical::Gravity::GravityInputIdentity input{handles[b],slot,version,generation_};
        identity.inputs.push_back(input);
        const auto layout=amr::native_scalar_layout(grid);
        views.push_back({input,{backend?backend->gravity_density(runtime_.backend_access(b,slot)):fluid.rho.data(),
            fluid.rho.size(),layout,backend?arch::grid::FieldMemory::Device:arch::grid::FieldMemory::Host,generation_}});
    }
    const auto prepared=std::chrono::steady_clock::now();
    auto execution=backend?backend->gravity_execution():nullptr;
    const auto before=execution?execution->numeric()->counters():arch::multigrid::ExecutionCounters{};
    if(native_candidate()) {
        auto next=std::make_unique<RuntimeSourceLease>(*this,identity,std::move(views),ledger,purpose);
        runtime_source_lease_=std::move(next);
    }
    const Physical::Gravity::GravitySolveRequest ordinary_request{identity,views,purpose,nullptr};
    const auto& request=runtime_source_lease_?runtime_source_lease_->request:ordinary_request;
    struct SourcePrepareScope {
        GravityStage& owner;bool completed=false;
        explicit SourcePrepareScope(GravityStage& value):owner(value){owner.source_prepare_running_=true;}
        ~SourcePrepareScope() {
            owner.source_prepare_running_=false;
            if(!completed&&owner.runtime_source_lease_) {
                owner.runtime_source_lease_->token.retire();owner.gravity_->invalidate();
            }
        }
    } source_scope(*this);
    if(runtime_source_lease_)runtime_source_lease_->require_domain(true);
    // Stack-only attachment covers every prepare exit. The source view and
    // payload cannot outlive this solve, including exceptions before callback.
    struct RestoreSourceInspection {
        Physical::Gravity::NativeRzSourceInspectionSink& sink;void*& payload;bool& active;
        Physical::Gravity::NativeRzSourceInspectionSink original_sink;void* original_payload;
        RestoreSourceInspection(Physical::Gravity::NativeRzSourceInspectionSink& actual_sink,
            void*& actual_payload,bool& actual_active,Physical::Gravity::NativeRzSourceInspectionSink old_sink,
            void* old_payload) noexcept:sink(actual_sink),payload(actual_payload),active(actual_active),
            original_sink(old_sink),original_payload(old_payload){}
        RestoreSourceInspection(const RestoreSourceInspection&)=delete;
        RestoreSourceInspection(RestoreSourceInspection&&)=delete;
        ~RestoreSourceInspection(){sink=original_sink;payload=original_payload;active=false;}
    };
    NativeSourceInspection* inspection=runtime_source_lease_.get();
    std::optional<RestoreSourceInspection> inspection_attachment; // Restored before witness destruction.
    if(source_inspection_sink_) {
        if(qualification_!=Qualification::NativeRzCandidate||backend||journal_active_||prepared_
            ||gravity_->native_source_inspection_sink_||gravity_->native_source_inspection_payload_
            ||gravity_->native_source_inspection_running_)
            throw std::logic_error("Native source inspection overlaps a live or foreign owner");
        source_inspection_active_=true;
        inspection_attachment.emplace(gravity_->native_source_inspection_sink_,
            gravity_->native_source_inspection_payload_,source_inspection_active_,
            gravity_->native_source_inspection_sink_,gravity_->native_source_inspection_payload_);
        if(!inspection)throw std::logic_error("Native source inspection has no actual Runtime source lease");
        gravity_->native_source_inspection_payload_=inspection;
        gravity_->native_source_inspection_sink_=&GravityStage::inspect_native_source;
    }
    const auto token=gravity_->prepare(request);
    if(runtime_source_lease_) {
        runtime_source_lease_->require_domain(true);
        runtime_source_lease_->token.seal();
    }
    if(native_candidate()) {
        // Use the same Runtime lease and full original request. No physical
        // report/patch/CFL/output consumer is promoted by this diagnostic path.
        const auto& candidate=gravity_->native_rz_assessment();
        if(candidate.source!=identity
            ||candidate.conditional.status!=arch::elliptic::BoundaryResidualStatus::Accepted
            ||candidate.physical_status!=arch::elliptic::BoundaryResidualStatus::UncertifiedInput)
            throw std::logic_error("Native RZ stage candidate identity/qualification mismatch");
        std::ostringstream row;
        row<<std::setprecision(17)<<time<<'\t'<<stage<<'\t'<<epoch_.value<<'\t'<<generation_<<'\t'
            <<gravity_->cell_count()<<'\t'<<candidate.source_generation<<'\t'
            <<candidate.conditional.total_residual_upper<<'\t'
            <<candidate.conditional.tolerance_safe<<"\t0\n";
        if(native_self()&&purpose==Physical::Gravity::GravityFieldPurpose::HydroStage) {
            if(!journal_active_||!prepared_||pending_count_>=pending_rows_.size())
                throw std::logic_error("Native Self solved field lost its actual bounded journal");
            prepared_->source=identity;
            const auto& binding=scheduler::current_stage_binding();
            // Field scope remains NativeRzCandidate. Only this typed frame may
            // borrow it for the exact real stage; ordinary consumers still fail.
            std::unique_ptr<Physical::Gravity::NativeSelfStageFrame> next{
                new Physical::Gravity::NativeSelfStageFrame(*gravity_,runtime_.boundaries(),
                    runtime_.control(),binding,prepared_->descriptor,config,
                    prepared_->step_dt,generation_,identity,
                    native_flux_observation_sink_,native_flux_observation_payload_)};
            self_frame_=std::move(next);policy_->native_self_frame_=self_frame_.get();
            pending_rows_[pending_count_].solve=row.str();
        } else {
            diagnostics_<<row.str();diagnostics_.flush();
            if(!diagnostics_)throw std::runtime_error("Cannot write native RZ candidate diagnostics");
        }
        source_scope.completed=true;return token;
    }
    const auto& report=gravity_->report();
    // Transaction rows stay private until the complete split macro-step accepts.
    std::ostringstream solve_row,boundary_row;
    solve_row<<std::setprecision(17);boundary_row<<std::setprecision(17);
    std::ostream& solve_output=journal_active_?static_cast<std::ostream&>(solve_row):diagnostics_;
    const auto finished=std::chrono::steady_clock::now();
    const auto after=execution?execution->numeric()->counters():arch::multigrid::ExecutionCounters{};
    solve_output<<time<<'\t'<<stage<<'\t'<<epoch_.value<<'\t'<<generation_<<'\t'<<gravity_->cell_count()<<'\t'
        <<report.cycles<<'\t'<<report.rhs_rms<<'\t'<<report.residual<<'\t'<<report.target<<'\t'<<gravity_->density_mean()<<'\t'
        <<(backend?1:0)<<'\t'<<std::chrono::duration<double>(prepared-start).count()<<'\t'
        <<std::chrono::duration<double>(finished-prepared).count()<<'\t'<<after.kernels-before.kernels<<'\t'
        <<after.bytes_h2d-before.bytes_h2d<<'\t'<<after.bytes_d2h-before.bytes_d2h<<'\t'
        <<after.synchronizations-before.synchronizations<<'\t'<<gravity_->timings().source_boundary<<'\t'
        <<gravity_->timings().poisson<<'\t'<<gravity_->timings().force<<'\n';
    if (!solve_output) throw std::runtime_error("Cannot write gravity diagnostics");
    if(boundary_diagnostics_.is_open()) {
        auto next=gravity_->boundary_snapshot();
        const auto observed=std::chrono::steady_clock::now();
        const auto observer_counters=execution?execution->numeric()->counters():arch::multigrid::ExecutionCounters{};
        auto& previous=journal_active_?pending_boundary_snapshot_:boundary_snapshot_;
        auto& cumulative=journal_active_?pending_boundary_exchange_:boundary_exchange_;
        std::ostream& boundary_output=journal_active_?static_cast<std::ostream&>(boundary_row):boundary_diagnostics_;
        const double exchange=previous ? Physical::Gravity::gravity_boundary_exchange(*previous,next) : 0.;
        const double change=previous ? next.potential_energy-previous->potential_energy : 0.;
        cumulative+=exchange;
        boundary_output << time << '\t' << (previous?previous->time:time)
            << '\t' << stage << '\t' << next.potential_energy << '\t' << change << '\t' << exchange
            << '\t' << cumulative << '\t' << next.faces.size()
            << '\t' << std::chrono::duration<double>(observed-finished).count()
            << '\t' << observer_counters.kernels-after.kernels
            << '\t' << observer_counters.bytes_h2d-after.bytes_h2d
            << '\t' << observer_counters.bytes_d2h-after.bytes_d2h
            << '\t' << observer_counters.synchronizations-after.synchronizations << '\n';
        if(!boundary_output) throw std::runtime_error("Cannot write gravity boundary diagnostics");
        previous=std::move(next);
    }
    if(journal_active_) {
        if(!prepared_||pending_count_>=pending_rows_.size())
            throw std::logic_error("Gravity journal has no bounded prepared row");
        prepared_->source=identity;
        pending_rows_[pending_count_]={solve_row.str(),boundary_row.str()};
    }
    if(backend)for(std::size_t b=0;b<handles.size();++b)backend->publish_gravity(runtime_.backend_access(b,slot),gravity_->patch_view(b));
    source_scope.completed=true;return token;
}
/** Prepare gravity for the requested hydro stage input. */
state::CompletionToken GravityStage::prepare(const scheduler::HydroStagePreparationRequest& request) {
    if(source_prepare_running_)throw std::logic_error("Gravity source preparation cannot reenter another Hydro request");
    if (!gravity_&&!native_external()) throw std::logic_error("No gravity stage service");
    if(journal_active_) {
        if(prepared_||request.side!=state::ExecutionSide::Host||runtime_.backend()
            ||&request.ledger!=&runtime_.stage_context().ledger
            ||request.handles.size()!=runtime_.handles().size()
            ||!std::equal(request.handles.begin(),request.handles.end(),runtime_.handles().begin()))
            throw std::logic_error("Gravity journal request is outside its actual Runtime frame");
        (void)scheduler::hydro_output_time_fraction(request.method,request.descriptor);
        scheduler::detail::require_boundary_interval(request.input_time,request.step_dt);
        if(request.descriptor.stage!=static_cast<int>(pending_count_+1)
            ||(journal_method_&&(*journal_method_!=request.method
                ||journal_dt_!=request.step_dt||request.input_time!=journal_start_
                    +request.descriptor.input_time_fraction*journal_dt_)))
            throw std::logic_error("Gravity journal stage sequence/time changed inside a macro-step");
        if(!journal_method_) {
            journal_method_=request.method;journal_start_=request.input_time;journal_dt_=request.step_dt;
            expected_count_=scheduler::supported_hydro_time_plan(request.method).stages.size();
        }
        prepared_.emplace(PreparedFrame{request.method,request.descriptor,{},request.input_time,request.step_dt});
        if(gravity_&&!native_self())gravity_->begin_host_stage_consumption(request.step_dt);
    }
    if(native_external())return prepare_native_external(request);
    if(native_self())require_native_self_preparation(request);
    return solve(request.descriptor.input_slot,request.ledger,request.input_time,request.descriptor.stage,
        Physical::Gravity::GravityFieldPurpose::HydroStage);
}
/** Prepare gravity on the accepted current state for output and timestep use. */
void GravityStage::prepare_current(double time, bool reset_solver_history) {
    if(source_prepare_running_)throw std::logic_error("Gravity source preparation cannot reenter Current/history");
    if(native_candidate()&&(runtime_.active_host_hydro_transaction()||prepared_
        ||policy_->prepared_native_self()||policy_->prepared_native_external()
        ||!RuntimeSourceLease::same(time,runtime_.ctrl.t_current)))
        throw std::logic_error("Native Current preparation requires the actual accepted/quiescent Runtime time");
    if(source_inspection_active_)
        throw std::logic_error("Native source inspection forbids synchronous current/history preparation");
    if(journal_active_||committed_count_)
        throw std::logic_error("Current gravity/output preparation requires a closed, flushed macro-step");
    if (gravity_) {
        if(native_candidate()) {
            // Retire old source borrows before real BC/exchange changes ghost
            // data. Obtain the new Current lease only after actual EOS accepts.
            // Invalid immutable science configuration fails before BC/EOS
            // can publish a legitimate new Current ghost/clock completion.
            if(!RuntimeSourceLease::gravity_configuration(
                runtime_.configuration().physics.gravity,gravity_->config_))
                throw std::logic_error("Native Current immutable gravity configuration differs from Runtime");
            invalidate();runtime_.ensure_fluid_ghosts(state::StateSlot::Current);
            if(!runtime_.native_rz_eos_binding_
                ||!runtime_.native_rz_eos_binding_matches(*runtime_.native_rz_eos_binding_))
                throw std::logic_error("Native Current preparation lost its actual bound EOS");
        }
        // A checkpoint stores accepted fluid fields but no iterative Poisson
        // history. The Driver resets only at durable restart boundaries so
        // direct and resumed paths begin from the same accepted state.
        if (reset_solver_history) gravity_->clear_solver_initial_guess();
        auto context=runtime_.stage_context();
        solve(state::StateSlot::Current,context.ledger,time,0,
            Physical::Gravity::GravityFieldPurpose::AcceptedCurrent);
    }
}
/** A real Host production service can journal without granting any new physical scope. */
bool GravityStage::supports_host_macro_step_journal() const noexcept {
    return !runtime_.backend()&&((gravity_&&qualification_==Qualification::Production)||native_external()||native_self());
}
/** Copy accepted observer state before acquiring a fluid transaction. */
void GravityStage::begin_macro_step() {
    if(source_prepare_running_)throw std::logic_error("Gravity source preparation cannot start a macro journal");
    if(!supports_host_macro_step_journal()||journal_active_||committed_count_)
        throw std::logic_error("Gravity macro-step journal is unavailable or already live/unflushed");
    auto next=boundary_snapshot_; // All fallible allocation precedes owner mutation.
    pending_boundary_snapshot_=std::move(next);
    pending_boundary_exchange_=boundary_exchange_;pending_count_=0;expected_count_=0;
    journal_method_.reset();prepared_.reset();journal_active_=true;
    if(runtime_source_lease_)invalidate();
}
/** Validate the exact source lease, all Runtime inputs and actual force/work consumption. */
void GravityStage::accept(const scheduler::StageDescriptor& descriptor) {
    if(!journal_active_||!prepared_
        ||!scheduler::same_stage_descriptor(descriptor,prepared_->descriptor)
        ||descriptor.stage!=static_cast<int>(pending_count_+1))
        throw std::logic_error("Gravity journal acceptance does not match its prepared descriptor");
    if(native_external()) {
        if(!external_frame_)throw std::logic_error("Native external source frame was not prepared");
        const auto budget=external_frame_->require_complete_consumption();
        auto* transaction=runtime_.active_host_hydro_transaction();
        if(!transaction)throw std::logic_error("Native external acceptance lost its macro owner");
        const auto& binding=scheduler::current_stage_binding();
        transaction->require_source_preparation_owner(*this,binding.context,binding.handles);
        const long double weight=descriptor.flux_register_weight;
        // Full dt is already in the source. The actual RK weight occurs once.
        external_pending_[pending_count_]={weight*budget.radial_momentum,weight*budget.axial_momentum,
            weight*budget.torque,weight*budget.work};
        for(const auto value:external_pending_[pending_count_])if(!std::isfinite(value))
            throw std::runtime_error("Native external weighted body budget overflowed");
        for(std::size_t field=0;field<external_accepted_.size();++field) {
            long double total=external_accepted_[field];
            for(std::size_t stage=0;stage<=pending_count_;++stage)total+=external_pending_[stage][field];
            if(!std::isfinite(total))throw std::runtime_error("Native external accepted body budget overflowed");
        }
        ++pending_count_;prepared_.reset();invalidate();return;
    }
    const auto& source=prepared_->source;
    if(source.topology!=epoch_||source.input_time!=prepared_->input_time
        ||source.inputs.size()!=runtime_.handles().size())
        throw std::logic_error("Gravity prepared field identity/time changed before acceptance");
    const auto context=runtime_.stage_context();
    for(std::size_t b=0;b<source.inputs.size();++b) {
        const auto& input=source.inputs[b];
        if(input.block!=runtime_.handles()[b]||input.slot!=descriptor.input_slot
            ||input.storage_generation!=generation_)
            throw std::logic_error("Gravity source patch/slot/storage frame changed before acceptance");
        context.ledger.require_readable({input.block,input.slot},
            {state::ExecutionSide::Host,input.version,true,false});
    }
    if(native_self()) {
        if(!runtime_source_lease_)throw std::logic_error("Native Self acceptance lost its issued source lease");
        runtime_source_lease_->require_domain(); // One full joined owner check, never per worker/patch.
        if(!self_frame_)throw std::logic_error("Native Self acceptance lacks its actual solved-field frame");
        auto* transaction=runtime_.active_host_hydro_transaction();
        if(!transaction)throw std::logic_error("Native Self acceptance lost its macro owner");
        const auto& binding=scheduler::current_stage_binding();
        transaction->require_source_preparation_owner(*this,binding.context,binding.handles);
        self_frame_->require_complete_consumption();
    } else {
        gravity_->require_host_stage_consumption(source);
        gravity_->end_host_stage_consumption();
    }
    ++pending_count_;prepared_.reset();invalidate();
}
/** Move only bounded accepted observer records; physical publication was checked before this tail. */
void GravityStage::commit_macro_step() noexcept {
    // The transaction calls this only after all fallible endpoint checks. A
    // programmer contract error cannot silently publish an incomplete prefix.
    if(!journal_active_||prepared_||!journal_method_
        ||!expected_count_||pending_count_!=expected_count_
        ||committed_count_)std::terminate();
    if(native_external()) {
        for(std::size_t stage=0;stage<pending_count_;++stage)
            for(std::size_t field=0;field<external_accepted_.size();++field)
                external_accepted_[field]+=external_pending_[stage][field];
        pending_count_=0;expected_count_=0;journal_method_.reset();journal_active_=false;
        invalidate();return;
    }
    static_assert(std::is_nothrow_swappable_v<decltype(boundary_snapshot_)>);
    committed_rows_.swap(pending_rows_);committed_count_=pending_count_;
    boundary_snapshot_.swap(pending_boundary_snapshot_);
    boundary_exchange_=pending_boundary_exchange_;
    pending_count_=0;journal_method_.reset();journal_active_=false;
}
/** A failed attempt keeps accepted physical boundary history and emits no prefix.
 * Native RZ numerical guesses and interval memoization are unsaved precision
 * history. Retire the failed source first, then clear that history so a retry
 * cannot depend on work performed by the abandoned macro attempt.
 */
void GravityStage::discard_macro_step() noexcept {
    if(!journal_active_)return;
    if(gravity_)gravity_->end_host_stage_consumption();
    prepared_.reset();journal_method_.reset();pending_boundary_snapshot_.reset();
    pending_count_=0;journal_active_=false;invalidate();
    if(native_self()&&gravity_)gravity_->clear_solver_initial_guess();
}
/** Report already accepted records; durable I/O is outside numerical commit/rollback. */
void GravityStage::flush_committed_diagnostics() {
    if(journal_active_)throw std::logic_error("Cannot flush a tentative gravity macro-step");
    if(!committed_count_)return; // Ordinary runs retain their existing buffered reporting.
    for(std::size_t i=0;i<committed_count_;++i) {
        diagnostics_<<committed_rows_[i].solve;
        if(boundary_diagnostics_.is_open())boundary_diagnostics_<<committed_rows_[i].boundary;
    }
    diagnostics_.flush();
    if(boundary_diagnostics_.is_open())boundary_diagnostics_.flush();
    if(committed_count_&&(!diagnostics_||(boundary_diagnostics_.is_open()&&!boundary_diagnostics_)))
        throw std::runtime_error("Cannot publish accepted gravity macro-step diagnostics");
    committed_count_=0;
}
/** Retire both host and device gravity views before changing state. */
void GravityStage::invalidate() const {
    // Explicit owner invalidation retires its last source-inspection marker.
    // A later SelfGravity solve failure remains separately observable.
    source_inspection_completed_=false;
    if(runtime_source_lease_)runtime_source_lease_->token.retire();
    if(native_external()) {
        // Retiring an unused/older service must not detach another service's
        // actual frame. Only the owner that attached this exact borrow clears it.
        if(policy_&&external_frame_&&policy_->prepared_native_external()==external_frame_.get())
            policy_->native_external_frame_=nullptr;
        if(external_frame_)external_frame_->invalidate();
    }
    if(native_self()) {
        if(policy_&&self_frame_&&policy_->prepared_native_self()==self_frame_.get())
            policy_->native_self_frame_=nullptr;
        if(self_frame_)self_frame_->invalidate();
    }
    if(gravity_)gravity_->invalidate();if(runtime_.backend())runtime_.backend()->invalidate_gravity();
}
/** Read the actual Current candidate field without a second solve or grant.
 * The copied inspection payload retains its existing numerical-only contract;
 * purpose projection belongs to this issued reader, not that separately owned
 * scientific/error scalar schema.
 */
Physical::Gravity::NativeRzFieldInspection GravityStage::native_current_field() const {
    if(!native_candidate()||!runtime_source_lease_)throw std::logic_error("No issued Native Current field");
    runtime_source_lease_->require_domain();
    gravity_->require_runtime_purpose(Physical::Gravity::GravityFieldPurpose::AcceptedCurrent);
    return gravity_->native_rz_field_inspection();
}
/** Copy one genuinely issued Current source and its SAME completed field.
 * Workflow: require the actual quiescent Runtime source domain and sealed
 * AcceptedCurrent lease -> copy exact owning producer data -> repeat domain
 * and purpose checks. The source hook remains pre-solve/quiescent-only, and
 * this diagnostic grants no continuous accuracy or public Native capability.
 */
Physical::Gravity::NativeRzSolutionInspection GravityStage::native_current_source_and_field() const {
    if(!native_candidate()||!gravity_||!runtime_source_lease_)
        throw std::logic_error("No issued Native Current source and field");
    const auto* const lease=runtime_source_lease_.get();
    lease->require_domain();
    gravity_->require_runtime_purpose(Physical::Gravity::GravityFieldPurpose::AcceptedCurrent);
    const auto& w=gravity_->workspace();
    const auto field_generation=w.generation;
    const auto source_generation=w.ring_assessment.source_generation;
    auto result=gravity_->copy_native_rz_solution(Physical::Gravity::GravityFieldPurpose::AcceptedCurrent,
        lease->identity,field_generation,source_generation);
    if(runtime_source_lease_.get()!=lease)
        throw std::logic_error("Native Current source issuer changed during inspection");
    lease->require_domain();
    gravity_->require_runtime_purpose(Physical::Gravity::GravityFieldPurpose::AcceptedCurrent);
    return result;
}
/** Internal numerical stability cap, never ordinary public Native CFL support. */
double GravityStage::native_current_timestep() const {
    if(!native_candidate()||!runtime_source_lease_)throw std::logic_error("No issued Native Current field");
    runtime_source_lease_->require_domain();
    gravity_->require_runtime_purpose(Physical::Gravity::GravityFieldPurpose::AcceptedCurrent);
    return gravity_->field_timestep(runtime_.configuration().numerics.cfl,
        Physical::Gravity::GravityFieldScope::NativeRzCandidate);
}
/** Report the gravity stability cap to the Driver scheduler. */
double GravityStage::timestep() const { return gravity_?gravity_->timestep(runtime_.configuration().numerics.cfl):std::numeric_limits<double>::infinity(); }
/** Materialize accepted potential and acceleration for plot output. */
std::vector<io::PlotScalarField> GravityStage::plot_fields() const {
    if (!gravity_) return {};
    std::vector<io::PlotScalarField> fields{{"GPOT",gravity_->potential()}};
    const char* names[]{"GACX","GACY","GACZ"};
    for(int a=0;a<runtime_.configuration().grid.dim;++a) fields.push_back({names[a],gravity_->acceleration()[a]});
    return fields;
}
}
