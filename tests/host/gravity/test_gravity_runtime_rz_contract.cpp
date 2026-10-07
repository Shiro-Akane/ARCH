// Actual CPU RZ Runtime ledger -> Stage -> native candidate; no timestep.
// Scientific/physical consumers and regrid/Device production gates stay held.
#include "amr/AMRControl.h"
#include "driver/DriverUtils.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "driver/stages/GravityStage.h"
#include "physics/eos/IdealGas.h"
#include "physics/gravity/self/SelfGravity.h"
#include "physics/gravity/GravityExecution.h"
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
// This standalone Runtime runner borrows production compile flags without an
// -Itests path. Reuse the sole tests-only witness through its local file path;
// do not duplicate seven-array/alias/receipt checks or any scientific math.
#include "../driver/RzRuntimeWitness.h"
#include "RzMaterializedSourceRecord.h"
#include <fstream>
#include "amr/elliptic/EllipticMeshAdapter.h"
#include "physics/constant/PhysicalConstants.h"
#include "physics/gravity/GravitySolveTypes.h"
#include "physics/gravity/GravityBoundary.h"
#include <cmath>
#include <memory>
#include <string>
#include <sstream>
static void require(bool x,const char* m){if(!x)throw std::runtime_error(m);}
template<class F> static void rejects(F f,const char* m){
    bool failed=false;try{f();}catch(const std::exception&){failed=true;}
    require(failed,m);
}
class Capture final:public Physical::Gravity::GravityExecution {
    std::shared_ptr<Physical::Gravity::GravityExecution> host=
        Physical::Gravity::make_host_gravity_execution();
public:
    int gathers=0;std::vector<double> density;
    std::vector<Physical::Gravity::GravityCell> cells;
    std::shared_ptr<arch::multigrid::CompositeExecution> numeric() const override{return host->numeric();}
    void run(const Physical::Gravity::GravityWork& work) override {
        host->run(work);
        if(const auto* input=std::get_if<Physical::Gravity::GatherDensity>(&work)){
            host->numeric()->fence();++gathers;
            density.assign(input->out,input->out+input->size);
            cells.assign(input->cells,input->cells+input->size);
        }
    }
};


namespace source_inspection_checks {
using Stage=arch::driver::GravityStage;
using Slot=arch::state::StateSlot;
using View=Physical::Gravity::NativeRzSourceInspectionView;
using rz_runtime_witness::bits;

/** Resolve the actual selected pool member; no copied state can grant a lease. */
FluidState& selected(amr::Block& block,Slot slot) {
    if(slot==Slot::Current)return block.fluid_state;
    if(slot==Slot::Scratch)return block.state_scratch;
    if(slot==Slot::Next)return block.state_next;
    throw std::logic_error("inspection fixture received an invalid slot");
}

/** Freeze real values/addresses, ledger high-water marks, BC and accepted clock.
 * Source service generations are monotonic and intentionally not rolled back.
 * These checks never claim a source inspection is a numerical transaction.
 */
struct OwnerWitness {
    arch::driver::DriverRuntime& runtime;
    SimulationController& counters;
    std::vector<rz_runtime_witness::FieldsWitness> fields;
    arch::state::StateResidencyLedger::HostSnapshot ledger;
    BCHandler::StageContextSnapshot boundary;
    const std::uint64_t token,version;
    const double time,dt;
    const int step,plt,chk;
    const arch::state::RepairBudget repairs;
    const std::vector<double> hydro,diffusion;
    OwnerWitness(arch::driver::DriverRuntime& owner,SimulationController& clock)
        :runtime(owner),counters(clock),ledger(owner.stage_context().ledger.snapshot_host()),
         boundary(owner.boundaries().snapshot_stage_context()),
         token(owner.stage_context().clock.last_token()),version(owner.stage_context().clock.last_version()),
         time(clock.t_current),dt(clock.dt_old),step(clock.step_count),
         plt(clock.plt_file_index),chk(clock.chk_file_index),repairs(clock.repairs),
         hydro(owner.hydro_boundary_budget()),diffusion(owner.diffusion_boundary_budget()) {
        const auto& active=runtime.control().tree->GetActiveBlocks();fields.reserve(active.size());
        for(int id:active)fields.emplace_back(runtime.control().pool->GetBlock(id));
    }
    void matches() const {
        const auto& active=runtime.control().tree->GetActiveBlocks();
        require(active.size()==fields.size(),"source inspection changed the actual active domain");
        for(std::size_t p=0;p<fields.size();++p)fields[p].matches(runtime.control().pool->GetBlock(active[p]));
        auto context=runtime.stage_context();
        require(context.ledger.host_snapshot_matches(ledger)
            &&context.clock.last_token()==token&&context.clock.last_version()==version,
            "source inspection changed actual fluid ledger or scheduler clock");
        require(runtime.boundaries().stage_context_matches(boundary)
            &&bits(counters.t_current,time)&&bits(counters.dt_old,dt)
            &&counters.step_count==step&&counters.plt_file_index==plt&&counters.chk_file_index==chk,
            "source inspection changed accepted BC/time/advice/output indices");
        require(rz_runtime_witness::same_repairs(counters.repairs,repairs)
            &&bits(runtime.hydro_boundary_budget(),hydro)&&bits(runtime.diffusion_boundary_budget(),diffusion),
            "source inspection changed accepted repairs or boundary budgets");
    }
};

/** Only a guarded reentry diagnostic counts: an unrelated physical gate is not
 * evidence that the source callback excluded synchronous owner replacement.
 */
template<class F> void guarded_reentry(F&& call) {
    bool refused=false;
    try {call();}
    catch(const std::logic_error& error) {
        require(std::string(error.what()).find("Native source inspection")!=std::string::npos,
            "reentry hit an unrelated rejection rather than the real inspection lease");
        refused=true;
    }
    require(refused,"materialized source callback accepted synchronous owner reentry");
}

/** Require the real existing boundary resource rejection after materialization.
 * All source-only cases intentionally stop at maximum_work=1, before solve.
 * Authenticate status and actual charged work, not an arbitrary thrown error.
 * No scientific tolerance, elapsed-time cap or new fallback is changed.
 */
template<class F> void boundary_work_limit(F&& invoke) {
    bool refused=false;
    try {invoke();}
    catch(const std::runtime_error& error) {
        const std::string message=error.what();
        const std::string prefix="Native RZ ring boundary failed: status="
            +std::to_string(int(Physical::Gravity::RingBoundaryStatus::WorkLimit))+" target=";
        require(message.starts_with(prefix),
            "source-only preparation did not reach the actual boundary WorkLimit diagnostic");
        const auto leaf=message.find(" leaf=",prefix.size());
        const auto parent=message.find(" parent=",leaf==std::string::npos?prefix.size():leaf+6);
        require(leaf!=std::string::npos&&parent!=std::string::npos&&parent>leaf,
            "actual boundary WorkLimit diagnostic lost its work accounting");
        const auto target_text=message.substr(prefix.size(),leaf-prefix.size());
        const auto leaf_text=message.substr(leaf+6,parent-leaf-6);
        const auto parent_text=message.substr(parent+8);
        std::size_t used=0;const double target=std::stod(target_text,&used);
        require(used==target_text.size()&&std::isfinite(target)&&target>=0.,
            "actual boundary WorkLimit diagnostic has an invalid target");
        used=0;const auto leaves=std::stoull(leaf_text,&used);
        require(used==leaf_text.size(),"actual boundary WorkLimit has an invalid leaf charge");
        used=0;const auto parents=std::stoull(parent_text,&used);
        require(used==parent_text.size()&&leaves<=1&&parents<=1&&leaves+parents==1,
            "actual boundary WorkLimit did not enforce the existing explicit work cap=1");
        refused=true;
    }
    require(refused,"source-only preparation unexpectedly solved/published a field through work cap=1");
}

enum class Action { Copy,Throw,DriftRho,ReplaceRho,StageSolve,CurrentHistory,
    SelfSolve,NativeBind,PublicBind,SameExecution,BeginConsumption,
    StageInvalidate,SelfInvalidate,EndConsumption,ResetHistory };

/** Owning value payload only: no op/binding/request/span/reference survives.
 * Copies describe the actual source before solve, never a certified field.
 */
struct CopiedSource {
    Physical::Gravity::GravitySolveIdentity identity;
    GravityConfig service_configuration;
    std::uint64_t source_generation=0;
    std::vector<double> density;
    std::vector<arch::elliptic::CompositeCell> cells;
    std::vector<arch::elliptic::CompositeFace> faces;
    std::vector<std::array<double,6>> bounds_and_centers;
};

struct Sink {
    Stage& stage;
    Physical::Gravity::SelfGravity& gravity;
    arch::driver::DriverRuntime& runtime;
    SimConfig& config;
    Capture& capture;
    std::shared_ptr<Physical::Gravity::GravityExecution> execution;
    Action action=Action::Copy;
    Slot expected_slot=Slot::Current;
    std::size_t calls=0;
    CopiedSource copied;
    // Replacement keeps the original allocation alive throughout the callback;
    // the test restores the injected fault before making any retry claim.
    std::vector<double> kept_allocation;
    double saved_rho=0.;
    std::size_t fault_offset=0;
    bool fault_applied=false;

    void unreadable() const {
        rejects([&]{gravity.native_rz_assessment();},"callback/failure retained a candidate assessment");
        rejects([&]{gravity.native_rz_potential();},"callback/failure retained a candidate potential");
        rejects([&]{gravity.potential();},"inspection granted a physical potential");
        rejects([&]{stage.plot_fields();},"inspection granted physical output");
        rejects([&]{stage.timestep();},"inspection granted physical timestep");
    }
    /** Authenticate against live pool/ledger/Grid inputs, not an ideal formula. */
    void capture_actual(const View& view) {
        require(!stage.native_rz_source_inspection_completed(),"inspection marker was true during callback");
        unreadable();++calls;
        auto context=runtime.stage_context();const auto& active=runtime.control().tree->GetActiveBlocks();
        require(view.source_generation>0&&view.density.size()==512
            &&view.op.size()==512&&view.binding.cells==view.op.cells()
            &&view.binding.storage.size()==view.density.size()
            &&view.binding.handles==runtime.handles()&&view.binding.grids.size()==active.size(),
            "inspection omitted real op/binding/source generation or actual patches");
        require(view.request.identity.topology==runtime.handles().front().epoch
            &&view.request.identity.inputs.size()==active.size()&&view.request.blocks.size()==active.size()
            &&bits(view.request.identity.input_time,0.)
            &&bits(view.request.identity.gravitational_constant,
                arch::constants::gravity::cgs::gravitational_constant)
            &&view.service_configuration==config.physics.gravity,
            "inspection changed Runtime input/service configuration/shared CGS G identity");
        const auto& base=view.op.base();
        require(base.native_canonical_domain&&base.dimension==2
            &&base.geometry==arch::elliptic::Geometry::Cylindrical
            &&base.semantics==GridMetrics::GeometrySemantics::AxisymmetricRz
            &&bits(base.origin[0],config.grid.x1_min)&&bits(base.root_upper[0],config.grid.x1_max)
            &&bits(base.origin[1],config.grid.x2_min)&&bits(base.root_upper[1],config.grid.x2_max),
            "inspection lost actual canonical native root bounds");
        copied={view.request.identity,view.service_configuration,view.source_generation,
            {view.density.begin(),view.density.end()},view.op.cells(),view.op.faces(),{}};
        copied.bounds_and_centers.reserve(view.density.size());
        std::size_t cell=0;
        for(std::size_t p=0;p<active.size();++p) {
            auto& block=runtime.control().pool->GetBlock(active[p]);
            auto& input=selected(block,expected_slot);const auto& grid=block.grid;
            const auto& identity=view.request.identity.inputs[p];const auto& read=view.request.blocks[p];
            const auto actual=context.ledger.inspect({runtime.handles()[p],expected_slot});
            require(identity.block==runtime.handles()[p]&&identity.slot==expected_slot
                &&identity.version==actual.interior.version&&identity.storage_generation>0
                &&read.identity==identity&&read.density.data==input.rho.data()
                &&read.density.size==input.rho.size()&&read.density.layout==amr::native_scalar_layout(grid)
                &&read.density.memory==arch::grid::FieldMemory::Host
                &&read.density.storage_generation==identity.storage_generation&&view.binding.grids[p]==&grid,
                "inspection borrowed a fake slot/storage/layout or nonfirst Runtime version");
            context.ledger.require_readable({identity.block,expected_slot},
                {arch::state::ExecutionSide::Host,identity.version,true,false});
            for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i,++cell) {
                const int offset=grid.GetIndex(i,j,0);const auto location=view.binding.storage[cell];
                const auto& key=view.op.cells()[cell];const auto center=view.op.center(int(cell));
                require(location.block==p&&location.offset==offset&&key.level==block.level
                    &&key.index==std::array<int,3>{int(block.logical_x1)*amr::BLOCK_NX+i-grid.Is(),
                        int(block.logical_x2)*amr::BLOCK_NY+j-grid.Js(),0}
                    &&bits(view.density[cell],input.rho[offset])
                    &&bits(capture.density[cell],view.density[cell])
                    &&bits(view.op.lower(int(cell),0),grid.GetFacePosL(i))
                    &&bits(view.op.upper(int(cell),0),grid.GetFacePosR(i))
                    &&bits(view.op.lower(int(cell),1),grid.GetAxialFacePosL(j))
                    &&bits(view.op.upper(int(cell),1),grid.GetAxialFacePosR(j))
                    &&bits(center[0],grid.GetCellCenterX(i))&&bits(center[1],grid.GetCellCenterY(j)),
                    "inspection differs from true downloaded rho/cell bounds/center/source ordering");
                copied.bounds_and_centers.push_back({view.op.lower(int(cell),0),view.op.upper(int(cell),0),
                    view.op.lower(int(cell),1),view.op.upper(int(cell),1),center[0],center[1]});
            }
        }
        require(cell==view.density.size()&&!copied.faces.empty(),"inspection source or face extent incomplete");
        for(const auto& face:view.op.faces()) {
            require(face.native_bounds&&(face.axis==0||face.axis==1)
                &&std::isfinite(face.area)&&face.area>=0.,"inspection face lacks real native geometry");
            const int anchor=face.left>=0?face.left:face.right;
            require(anchor>=0&&anchor<view.op.size(),"inspection face has no actual source owner");
            const bool lower=face.left<0;
            const double normal=lower?view.op.lower(anchor,face.axis):view.op.upper(anchor,face.axis);
            require(bits(face.center[face.axis],normal)
                &&bits(face.fragment_lower[face.axis],normal)&&bits(face.fragment_upper[face.axis],normal),
                "inspection face is not its actual oriented source endpoint");
            if(face.left>=0&&face.right>=0)
                require(bits(view.op.upper(face.left,face.axis),view.op.lower(face.right,face.axis)),
                    "inspection internal face joins different actual cell endpoints");
            const int tangent=1-face.axis;
            require(bits(face.fragment_lower[tangent],view.op.lower(anchor,tangent))
                &&bits(face.fragment_upper[tangent],view.op.upper(anchor,tangent))
                &&std::isfinite(face.center[tangent])
                &&face.center[tangent]>=face.fragment_lower[tangent]
                &&face.center[tangent]<=face.fragment_upper[tangent],
                "inspection face fragment/center differs from the real uniform leaf source");
        }
    }
    static void call(void* payload,const View& view) {
        auto& self=*static_cast<Sink*>(payload);self.capture_actual(view);
        auto context=self.runtime.stage_context();
        const auto plan=arch::scheduler::make_hydro_plan(arch::scheduler::HydroMethod::RK3);
        auto& last=self.runtime.control().pool->GetBlock(self.runtime.control().tree->GetActiveBlocks().back());
        auto& input=selected(last,self.expected_slot);
        switch(self.action) {
        case Action::Copy:break;
        case Action::Throw:throw std::runtime_error("actual materialized-source callback fault");
        case Action::DriftRho:
            self.fault_offset=std::size_t(last.grid.GetIndex(last.grid.Is(),last.grid.Js(),0));
            self.saved_rho=input.rho[self.fault_offset];
            input.rho[self.fault_offset]=std::nextafter(self.saved_rho,std::numeric_limits<double>::infinity());self.fault_applied=true;break;
        case Action::ReplaceRho:
            require(self.kept_allocation.size()==input.rho.size()&&self.kept_allocation.data()!=input.rho.data(),
                "replacement fixture did not prepare a distinct real allocation");
            input.rho.swap(self.kept_allocation);self.fault_applied=true;break;
        case Action::StageSolve:
            guarded_reentry([&]{self.stage.prepare({plan.method,plan.stages.front(),self.runtime.handles(),
                arch::state::ExecutionSide::Host,context.ledger,0.,0.});});break;
        case Action::CurrentHistory:guarded_reentry([&]{self.stage.prepare_current(0.,true);});break;
        case Action::SelfSolve:guarded_reentry([&]{self.gravity.prepare(view.request);});break;
        case Action::NativeBind:guarded_reentry([&]{self.gravity.bind_native_rz_candidate(view.binding,65536,0);});break;
        case Action::PublicBind:guarded_reentry([&]{self.gravity.bind(view.binding,0.);});break;
        case Action::SameExecution:guarded_reentry([&]{self.gravity.set_execution(self.execution);});break;
        case Action::BeginConsumption:guarded_reentry([&]{self.gravity.begin_host_stage_consumption(1.e-4);});break;
        case Action::StageInvalidate:self.stage.invalidate();break;
        case Action::SelfInvalidate:self.gravity.invalidate();break;
        case Action::EndConsumption:self.gravity.end_host_stage_consumption();break;
        case Action::ResetHistory:self.gravity.clear_solver_initial_guess();break;
        }
        require(!self.stage.native_rz_source_inspection_completed(),"callback published its own completion marker");
    }
};

/** Exercise genuine source pre/callback/post leases; there is no model step,
 * checkpoint output, fake prepared token or source-to-field qualification.
 */
void run(SimConfig& config,amr::AMRControl& control,SimulationController& counters,
    arch::driver::DriverRuntime& runtime,Physical::Gravity::SelfGravity& gravity,
    Stage& stage,const std::shared_ptr<Capture>& capture) {
    // The original NaN/unpublished/nonfirst gates already ran unchanged. Give
    // each real slot a distinct uniform density for the new source-only phase.
    auto context=runtime.stage_context();
    for(int id:control.tree->GetActiveBlocks()) {
        auto& b=control.pool->GetBlock(id);
        for(const auto slot:{Slot::Current,Slot::Scratch,Slot::Next}) {
            auto& input=selected(b,slot);const double rho=slot==Slot::Current?1.125:slot==Slot::Scratch?1.25:1.5;
            for(double& value:input.rho)value=rho;
        }
    }
    for(const auto slot:{Slot::Current,Slot::Scratch,Slot::Next})
        arch::scheduler::publish_completed_interior(context,runtime.handles(),slot);
    // Original main already completed three full candidate field solves. The
    // separate optional-hook phase needs only real source preparation: rebind
    // the actual Runtime geometry/handles through the existing explicit work
    // cap=1. This resets the real ring source counter BEFORE this phase; no
    // cross-binding/global generation monotonicity is claimed.
    stage.invalidate();
    gravity.bind_native_rz_candidate(amr::bind_elliptic_mesh(control,config.grid,runtime.handles()),65536,1);
    const auto plan=arch::scheduler::make_hydro_plan(arch::scheduler::HydroMethod::RK3);
    Sink sink{stage,gravity,runtime,config,*capture,capture};
    stage.set_native_rz_source_inspection(&Sink::call,&sink);
    require(!stage.native_rz_source_inspection_completed(),"sink installation claimed completed materialization");
    auto invoke=[&] {
        const int index=sink.expected_slot==Slot::Current?0:sink.expected_slot==Slot::Scratch?1:2;
        stage.prepare({plan.method,plan.stages[index],runtime.handles(),arch::state::ExecutionSide::Host,
            context.ledger,0.,0.});
    };
    auto success=[&] {
        const OwnerWitness before(runtime,counters);const auto calls=sink.calls;
        const auto previous_generation=sink.copied.source_generation;
        boundary_work_limit(invoke);before.matches();
        require(sink.calls==calls+1&&stage.native_rz_source_inspection_completed(),
            "real pre/callback/post did not complete exactly once");
        require(sink.copied.source_generation>previous_generation
            &&sink.copied.identity.inputs.size()==runtime.handles().size()
            &&sink.copied.identity.topology==runtime.handles().front().epoch
            &&bits(sink.copied.identity.input_time,0.)
            &&bits(sink.copied.identity.gravitational_constant,
                arch::constants::gravity::cgs::gravitational_constant)
            &&bits(sink.copied.density,capture->density)&&sink.copied.service_configuration==config.physics.gravity,
            "copied actual source identity/generation/configuration differs from checked materialization");
        // A true marker authenticates source pre/callback/post only. Expected
        // later WorkLimit leaves all candidate/physical field readers blocked.
        sink.unreadable();
        rejects([&]{gravity.potential();},"source-only marker granted a physical potential");
        rejects([&]{stage.plot_fields();},"source-only marker granted physical output");
        rejects([&]{stage.timestep();},"source-only marker granted a physical timestep");
        require(sink.copied.bounds_and_centers.size()==512&&!sink.copied.faces.empty(),
            "source payload retained no owning geometry copies");
    };
    for(const auto slot:{Slot::Current,Slot::Scratch,Slot::Next}) {
        sink.expected_slot=slot;success();
        std::cout<<"ACTUAL_RZ_SOURCE_INSPECTION_SLOT_PASS slot="<<int(slot)
            <<" source_generation="<<sink.copied.source_generation<<" copied_cells=512 marker_after=1 boundary_work_limit=1 field_not_solved=1"<<std::endl;
    }
    sink.expected_slot=Slot::Current;
    stage.invalidate();
    require(!stage.native_rz_source_inspection_completed(),"explicit stage invalidation retained source completion");
    sink.unreadable();

    // Immutable service vs Runtime mismatch must fail BEFORE the user sink.
    {
        const OwnerWitness before(runtime,counters);const auto calls=sink.calls;
        const double original=config.physics.gravity.relative_tolerance;
        config.physics.gravity.relative_tolerance=std::nextafter(original,std::numeric_limits<double>::infinity());
        try {rejects(invoke,"one-ULP Runtime/service configuration mismatch accepted");}
        catch(...) {config.physics.gravity.relative_tolerance=original;throw;}
        config.physics.gravity.relative_tolerance=original;
        require(sink.calls==calls&&!stage.native_rz_source_inspection_completed(),
            "service configuration mismatch reached callback or a true marker");
        before.matches();sink.unreadable();success();
    }
    const std::array rejected{Action::Throw,Action::DriftRho,Action::ReplaceRho,
        Action::StageInvalidate,Action::SelfInvalidate,Action::EndConsumption,Action::ResetHistory};
    for(const auto action:rejected) {
        sink.action=action;sink.fault_applied=false;const OwnerWitness before(runtime,counters);const auto calls=sink.calls;
        auto& last=control.pool->GetBlock(control.tree->GetActiveBlocks().back());
        auto& input=selected(last,sink.expected_slot);
        if(action==Action::ReplaceRho)sink.kept_allocation=input.rho;
        bool refused=false;std::string actual_error;
        try {invoke();}catch(const std::exception& error){refused=true;actual_error=error.what();}
        // Deliberate corruption is NOT a transactional rollback contract.
        // Restore the true fault before comparing all leases/values and retry.
        if(action==Action::DriftRho&&sink.fault_applied)input.rho[sink.fault_offset]=sink.saved_rho;
        if(action==Action::ReplaceRho&&sink.fault_applied)input.rho.swap(sink.kept_allocation);
        require(refused&&sink.calls==calls+1&&!stage.native_rz_source_inspection_completed(),
            "fault/poisoned source callback published completion or did not run exactly once");
        if(action==Action::Throw)require(actual_error=="actual materialized-source callback fault",
            "throwing sink was masked by an earlier unrelated rejection");
        before.matches();sink.unreadable();sink.action=Action::Copy;success();
        std::cout<<"ACTUAL_RZ_SOURCE_INSPECTION_NEGATIVE_PASS action="<<int(action)
            <<" marker_false=1 real_input_restored=1 source_only_clean_retry=1 field_not_solved=1"<<std::endl;
    }
    // Guarded synchronous reentries throw before they alter a real owner.
    // The sink may catch that refusal and finish its unchanged source copy.
    for(const auto action:{Action::StageSolve,Action::CurrentHistory,Action::SelfSolve,
            Action::NativeBind,Action::PublicBind,Action::SameExecution,Action::BeginConsumption}) {
        sink.action=action;success();
        std::cout<<"ACTUAL_RZ_SOURCE_INSPECTION_REENTRY_PASS action="<<int(action)
            <<" caught_real_lease_guard=1 unchanged_source_completed=1 field_not_solved=1"<<std::endl;
    }
    sink.action=Action::Copy;stage.set_native_rz_source_inspection(nullptr,nullptr);
    require(!stage.native_rz_source_inspection_completed(),"clearing sink retained a completion marker");
    // Optional hook removed: actual source preparation reaches the same
    // explicit WorkLimit without running the old sink or inheriting its marker.
    // Normal no-hook field success was already proven by the original main.
    const OwnerWitness before(runtime,counters);const auto calls=sink.calls;
    boundary_work_limit(invoke);before.matches();
    require(sink.calls==calls&&!stage.native_rz_source_inspection_completed(),
        "disabled optional hook ran its old payload or inherited its marker");
    rejects([&]{gravity.potential();},"cleared hook promoted physical field scope");
    // Retire the source-only work-cap fixture outside every callback and restore
    // the same actual Runtime binding with its original full-resource policy.
    // This performs no field solve. A later independent export preparation may
    // now use the original settings; its new ring generation restarts locally,
    // so no cross-binding/global generation monotonicity is asserted.
    stage.invalidate();
    gravity.bind_native_rz_candidate(amr::bind_elliptic_mesh(control,config.grid,runtime.handles()),65536,0);
    before.matches();
    require(!stage.native_rz_source_inspection_completed(),"restoring original binding fabricated source completion");
    sink.unreadable();
    std::cout<<"ACTUAL_RZ_SOURCE_INSPECTION_CONTRACT_PASS slots=3 config_preflight=1"
        <<" failures=7 guarded_reentries=7 copies_only=1 bounded_source_only=1 field_not_solved=1 time=0 steps=0"<<std::endl;
}
/** Export one real Current materialization and its existing candidate cell
 * arrays after the independent lifecycle owner checks. No model step, ideal
 * ring geometry, face-gradient accessor or scientific qualification is added.
 * expected_cells is the checked actual-root layout extent; its default512
 * preserves the original lifecycle/source-only fixture without another model.
 */
void export_json(const std::filesystem::path& out,SimConfig& config,
    amr::AMRControl& control,SimulationController& counters,
    arch::driver::DriverRuntime& runtime,Physical::Gravity::SelfGravity& gravity,
    Stage& stage,const std::shared_ptr<Capture>& capture,std::size_t expected_cells=512) {
    arch::test::RzMaterializedSourceRecord record;
    require(!stage.native_rz_source_inspection_attached(),"export inherited a live source inspection sink");
    // The real existing source observer owns this attachment. capture_call must
    // refuse it without invoking it, replacing it or starting another solve.
    {
        const OwnerWitness before(runtime,counters);const int gathers=capture->gathers;
        Sink other{stage,gravity,runtime,config,*capture,capture};
        stage.set_native_rz_source_inspection(&Sink::call,&other);
        bool invoked=false;
        try {
            rejects([&]{record.capture_call(stage,[&]{invoked=true;});},
                "JSON record replaced another actual Stage sink");
            require(!invoked&&stage.native_rz_source_inspection_attached()
                &&other.calls==0&&capture->gathers==gathers&&!record.source_only_checked(),
                "existing-sink refusal mutated its owner or fabricated source completion");
        } catch(...) {stage.set_native_rz_source_inspection(nullptr,nullptr);throw;}
        stage.set_native_rz_source_inspection(nullptr,nullptr);before.matches();
    }
    // An actual non-standard invocation exception must be rethrown unchanged,
    // with no callback, field solve, retained sink or serializable source grant.
    {
        const OwnerWitness before(runtime,counters);const int gathers=capture->gathers;
        bool propagated=false;
        try {record.capture_call(stage,[]{throw std::uint32_t{0x13579u};});}
        catch(std::uint32_t code){require(code==0x13579u,"JSON helper changed non-standard invocation error");propagated=true;}
        require(propagated&&record.callback_count()==0&&!record.source_only_checked()
            &&!record.cleanup_failed()&&!stage.native_rz_source_inspection_attached()
            &&!stage.native_rz_source_inspection_completed()&&capture->gathers==gathers,
            "non-standard invocation failure retained a sink or false source completion");
        rejects([&]{(void)record.json();},"incomplete JSON record exported a checked source");
        before.matches();
    }
    const OwnerWitness before(runtime,counters);const int gathers=capture->gathers;
    record.capture_call(stage,[&]{stage.prepare_current(counters.t_current,true);});
    require(record.callback_count()==1&&record.source_only_checked()&&!record.cleanup_failed()
        &&!stage.native_rz_source_inspection_attached()&&!stage.native_rz_source_inspection_completed()
        &&capture->gathers==gathers+1,"JSON export missed actual one-call checked materialization/cleanup");
    // These are genuine scoped candidate getters under the captured matching
    // source stamp. They do not authorize continuous Phi/g or physical readers.
    record.capture_native_field(gravity);before.matches();
    require(gravity.native_rz_potential().size()==expected_cells&&capture->density.size()==expected_cells,
        "JSON export lost the expected dense actual field/source extent");
    for(const auto& input:gravity.native_rz_assessment().source.inputs)
        require(input.slot==Slot::Current,"JSON source export selected another Runtime slot");
    rejects([&]{gravity.potential();},"JSON capture granted physical potential");
    rejects([&]{stage.plot_fields();},"JSON capture granted physical output");
    rejects([&]{stage.timestep();},"JSON capture granted physical timestep");
    const std::string json=record.json();
    const auto path=out/"materialized-native-source.json";
    require(!std::filesystem::exists(path),"JSON source export requires a new local file");
    std::ofstream writer(path,std::ios::out|std::ios::binary);
    require(writer.is_open(),"cannot create actual local source JSON record");
    writer<<json<<'\n';writer.flush();require(bool(writer),"cannot write actual source JSON record");
    writer.close();require(bool(writer),"cannot close actual source JSON record");
    std::cout<<"ACTUAL_RZ_SOURCE_JSON_EXPORT_PASS cells="<<expected_cells<<" actual_source_checked=1"
        <<" actual_candidate_cell_arrays=1 physical_qualified=0 existing_sink_refused=1"
        <<" nonstd_propagated=1 time=0 steps=0"<<std::endl;
}
} // namespace source_inspection_checks

int main(int argc,char** argv){
 try {
    const bool default_lane=argc==2;
    const bool source_lane=(argc==3||argc==5)&&std::string(argv[2])=="--materialized-source-only";
    require(default_lane||source_lane,
        "new persistent output directory and optional exact source-only/matched-resolution flags required");
    int matched_resolution=0;
    if(argc==5) {
        const std::string level(argv[4]);
        require(std::string(argv[3])=="--matched-resolution"&&(level=="0"||level=="1"||level=="2"),
            "matched-resolution requires the source-only lane and exact level 0, 1 or 2");
        matched_resolution=level[0]-'0';
    }
    const bool materialized_source_only=source_lane;
    const std::filesystem::path out(argv[1]);require(!std::filesystem::exists(out),"new directory required");
    SimConfig config;config.grid.geometry="cylindrical";config.grid.dim=2;
    // Workflow: a bounded maintainer ordinal changes only actual level-zero
    // root block counts. The physical domain/source and all solver settings
    // remain fixed. k=0 keeps the original 2x1 roots/512-cell layout.
    const int scale=1<<matched_resolution;
    config.grid.nblockx1=2*scale;config.grid.nblockx2=scale;config.grid.nblockx3=0;
    const std::size_t root_count=static_cast<std::size_t>(config.grid.nblockx1)*config.grid.nblockx2;
    require(amr::BLOCK_NX==16&&amr::BLOCK_NY==16,"matched source fixture requires the original 16x16 native blocks");
    require(root_count<=static_cast<std::size_t>(std::numeric_limits<int>::max())/4,
        "matched root capacity exceeds the real controller owner range");
    const int block_capacity=static_cast<int>(4*root_count);
    const std::size_t expected_cells=root_count*amr::BLOCK_NX*amr::BLOCK_NY;
    require(expected_cells==static_cast<std::size_t>(512)*scale*scale,
        "matched root count differs from its fixed source-cell extent");
    config.grid.x1_min=0.;config.grid.x1_max=1.;config.grid.x2_min=-.5;config.grid.x2_max=.5;
    config.grid.amr_max_blocks=block_capacity;config.amr.lrefinemax=1;
    config.physics.gravity.boundary="isolated";
    config.physics.gravity.relative_tolerance=1.e-10;config.physics.gravity.absolute_tolerance=0.;
    config.physics.gravity.max_cycles=200;config.io.out_dir=out.string();
    SpeciesManager species;species.add_species("fixture",1.,1.,1.4,1.);
    IdealGas eos(1.4,species);
    amr::AMRControl control(block_capacity,2);
    control.tree->InitRootGrid(config,1,GridMetrics::GeometrySemantics::AxisymmetricRz);
    require(control.tree->GetActiveBlocks().size()==root_count,"matched initialization lost actual root owners");
    for(int id:control.tree->GetActiveBlocks()){
        auto& b=control.pool->GetBlock(id);
        for(auto* state:{&b.fluid_state,&b.state_next,&b.state_scratch}){
            state->InitSpecies(1);
            for(int c=0;c<b.grid.GetTotalSize();++c){state->set(c,{1.,0.,0.,0.,10.});state->X(0,c)=1.;}
        }
    }
    RunState start{};SimulationController counters(config,start);
    BCHandler boundary(config,GridMetrics::GeometrySemantics::AxisymmetricRz);
    arch::driver::DriverRuntime runtime(control,boundary,config,species,counters);
    runtime.bind_native_rz_eos(eos);
    runtime.initialize_topology();
    Physical::Gravity::SelfGravity gravity(config.physics.gravity);
    auto capture=std::make_shared<Capture>();gravity.set_execution(capture);
    using Stage=arch::driver::GravityStage;using Slot=arch::state::StateSlot;
    Stage production(runtime,&gravity);
    rejects([&]{production.prepare_current(0.,true);},"default stage enabled RZ");
    require(capture->gathers==0,"default RZ gate delegated work");
    Stage stage(runtime,&gravity,Stage::Qualification::NativeRzCandidate);
    if(materialized_source_only) {
        // One fresh actual Current source/field observation. The
        // lifecycle/fault/reentry matrix uses a separate default lane.
        source_inspection_checks::export_json(out,config,control,counters,runtime,gravity,stage,capture,expected_cells);
        std::cout<<"ACTUAL_RZ_MATCHED_RESOLUTION_SOURCE_LAYOUT level="<<matched_resolution
            <<" root_blocks="<<config.grid.nblockx1<<','<<config.grid.nblockx2
            <<" expected_cells="<<expected_cells<<" physical_qualified=0 time=0 steps=0"<<std::endl;
        std::cout<<"ACTUAL_RZ_MATERIALIZED_SOURCE_ONLY_PASS actual_runtime_initialized=1"
            <<" selected_current=1 source_and_candidate_cell_arrays_only=1"
            <<" physical_qualified=0 time=0 steps=0"<<std::endl;
        return 0;
    }
    auto verify=[&](Slot slot){
        const auto& result=gravity.native_rz_assessment();
        require(result.source.inputs.size()==runtime.handles().size(),"Runtime inputs incomplete");
        require(result.source.topology==runtime.handles().front().epoch,"Runtime topology identity lost");
        require(result.source.input_time==0.,"t=0 Runtime request changed");
        auto context=runtime.stage_context();
        std::vector<bool> seen(runtime.handles().size(),false);
        for(std::size_t n=0;n<runtime.handles().size();++n){
            const auto& input=result.source.inputs[n];
            const auto interior=context.ledger.inspect({runtime.handles()[n],slot}).interior;
            require(input.block==runtime.handles()[n]&&input.slot==slot
                &&input.version==interior.version,"actual Runtime lease/version mismatch");
            require(input.storage_generation>0,"Runtime lease generation missing");
        }
        for(std::size_t n=0;n<capture->cells.size();++n){
            const auto cell=capture->cells[n];
            require(cell.block>=0&&std::size_t(cell.block)<seen.size(),"native block map mismatch");
            const auto& b=control.pool->GetBlock(control.tree->GetActiveBlocks()[cell.block]);
            const auto& state=slot==Slot::Current?b.fluid_state:slot==Slot::Scratch?b.state_scratch:b.state_next;
            require(capture->density[n]==state.rho[cell.offset],"Runtime selected buffer not gathered");
            seen[cell.block]=true;
        }
        for(bool found:seen)require(found,"nonfirst native block omitted");
        require(gravity.native_rz_potential().size()==512,"Runtime native extent changed");
        rejects([&]{gravity.potential();},"Runtime candidate escaped output scope");
        rejects([&]{stage.plot_fields();},"candidate allowed physical plot output");
        rejects([&]{stage.timestep();},"candidate allowed physical CFL");
        rejects([&]{gravity.patch_view(1);},"candidate allowed nonfirst Hydro patch");
        std::cout<<"ACTUAL_RZ_RUNTIME_SLOT_PASS slot="<<static_cast<int>(slot)
            <<" lease="<<result.source.inputs.front().storage_generation
            <<" source_generation="<<result.source_generation<<" cells=512 time=0 steps=0"<<std::endl;
    };
    stage.prepare_current(0.,true);verify(Slot::Current);
    const auto plan=arch::scheduler::make_hydro_plan(arch::scheduler::HydroMethod::RK3);
    auto context=runtime.stage_context();
    auto invoke=[&](const arch::scheduler::StageDescriptor& descriptor){
        return stage.prepare({plan.method,descriptor,runtime.handles(),arch::state::ExecutionSide::Host,
            context.ledger,0.,0.});
    };
    rejects([&]{invoke(plan.stages[1]);},"unpublished Scratch accepted");
    require(capture->gathers==1,"unpublished Scratch reached gather");
    rejects([&]{gravity.native_rz_assessment();},"invalid lease retained field");
    const auto prefix=std::span<const amr::BlockHandle>(runtime.handles()).first(runtime.handles().size()-1);
    arch::scheduler::publish_completed_interior(context,prefix,Slot::Scratch);
    rejects([&]{invoke(plan.stages[1]);},"unpublished nonfirst Scratch accepted");
    require(capture->gathers==1,"unpublished nonfirst reached gather");
    for(auto slot:{Slot::Scratch,Slot::Next}){
        for(int id:control.tree->GetActiveBlocks()){
            auto& b=control.pool->GetBlock(id);
            for(auto* state:{&b.fluid_state,&b.state_scratch,&b.state_next})
                for(double& rho:state->rho)rho=std::numeric_limits<double>::quiet_NaN();
            auto& selected=slot==Slot::Scratch?b.state_scratch:b.state_next;
            for(double& rho:selected.rho)rho=1.;
        }
        arch::scheduler::publish_completed_interior(context,runtime.handles(),slot);
        invoke(plan.stages[slot==Slot::Scratch?1:2]);verify(slot);
    }
    require(capture->gathers==3,"actual RZ Runtime gather count");
    stage.invalidate();rejects([&]{gravity.native_rz_potential();},"invalidation retained candidate");
    rejects([&]{runtime.perform_regrid(0,0.);},"RZ production regrid gate lifted");
    require(capture->gathers==3,"regrid gate executed gravity");
    std::cout<<"ACTUAL_RZ_RUNTIME_CONTRACT_PASS blocks=2 gathers=3 slots=3"
        <<" actual_ledger=1 nonfirst_unpublished=1 invalidation=1"
        <<" physical_consumers_rejected=1 regrid_gate_held=1 time=0 steps=0"<<std::endl;
    source_inspection_checks::run(config,control,counters,runtime,gravity,stage,capture);
    source_inspection_checks::export_json(out,config,control,counters,runtime,gravity,stage,capture);
    return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
