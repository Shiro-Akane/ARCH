/**
 * @file test_rz_runtime_boundary.cpp
 * @brief Actual native Runtime EOS, boundary and RKL ownership witnesses.
 *
 * Workflow:
 * 1. Bind the real IdealGas and validate completed native boundary/exchange.
 * 2. Reject incomplete RKL ownership before slot copies or flux mutation.
 * 3. Exercise actual single/composite RKL1/2 publication and existing physical
 *    references with their original budgets. No scientific output is written.
 * 4. Run genuine nonzero angular RKL1 through real phased BC/EOS: reject an
 *    inadmissible first D/2 and verify whole-macro rollback, then accept a
 *    separate smaller half-step. Neither is a complete positive macro, full
 *    tensor, source-active, public native route or CUDA qualification.
 * 5. Add bounded warm RKL2 + external-source + selected Hydro macro witnesses,
 *    with both actual diffusion halves and original native integral budgets.
 *    Burn is disabled; this does not qualify cold/four-module/long-time use.
 */
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "host/driver/RzRuntimeWitness.h"

#include "driver/DriverUtils.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "driver/stages/DriverMacroStep.h"
#include "driver/stages/DriverStages.h"
#include "driver/stages/GravityStage.h"
#include "numerics/flux/FluxHLLC.h"
#include "numerics/integrator/HydroSolverImpl.h"
#include "numerics/integrator/TimeIntegratorEuler.h"
#include "numerics/integrator/TimeIntegratorRK2.h"
#include "numerics/integrator/TimeIntegratorRK3.h"
#include "numerics/reconstruction/Reconstruction.h"
#include "physics/boundary/UserBoundary.h"
#include "physics/eos/IdealGas.h"
#include "physics/gravity/ExternalGravity.h"
#include "physics/gravity/NativeExternalSource.h"
using rz_runtime_witness::require;
/** Instrument actual IdealGas calls; retain its physical species/Cv/gamma implementation. */
struct ObservedIdealGas : IdealGas {
    mutable std::size_t pressure_calls=0;
    ObservedIdealGas(const SpeciesManager& species):IdealGas(1.4,species){}
    double get_pressure(const FluidVector& state,const double* fractions) const {
        ++pressure_calls;return IdealGas::get_pressure(state,fractions);
    }
};

/** Exercise both real native RKL engines' rejection before any owner writes.
 * The warm valid physical fixture isolates scheduler ownership: missing EOS
 * acceptance, Device execution and truncated handles must fail at native
 * preflight, rather than at a later arithmetic/ghost/thermal error.
 */
template<class Eos>
static void native_rkl_preflight_before_write(
    arch::driver::DriverRuntime& runtime,const Eos& eos,
    arch::scheduler::StageExecutionContext& context,
    arch::scheduler::RklMethod method,double dt,double dt_fe)
{
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    auto& control=runtime.control();
    const auto& active=control.tree->GetActiveBlocks();
    const auto& handles=runtime.handles();
    require(!active.empty()&&handles.size()==active.size(),"RKL negative fixture domain mismatch");
    std::vector<std::array<FluidState,3>> before;
    for(int id:active) {
        const auto& block=control.pool->GetBlock(id);
        before.push_back({block.fluid_state,block.state_next,block.state_scratch});
    }
    const auto ledger_before=context.ledger.snapshot_host();
    const auto flux_before=control.flux_register.snapshot_host();
    const auto boundary_before=runtime.boundaries().snapshot_stage_context();
    const auto same_state=[](const FluidState& state,const FluidState& saved) {
        return state.rho==saved.rho&&state.mom_u==saved.mom_u
            &&state.mom_v==saved.mom_v&&state.mom_w==saved.mom_w
            &&state.eng==saved.eng&&state.enuc_rate==saved.enuc_rate
            &&state.mass_fractions==saved.mass_fractions
            &&state.GetNumSpecies()==saved.GetNumSpecies()
            &&state.block_total_size_==saved.block_total_size_
            &&state.stage_repairs.semantics==saved.stage_repairs.semantics
            &&state.stage_repairs.values==saved.stage_repairs.values
            &&state.stage_repairs.block_uid==saved.stage_repairs.block_uid
            &&state.stage_repairs.stage==saved.stage_repairs.stage
            &&state.stage_repairs.time==saved.stage_repairs.time
            &&std::equal(state.stage_repairs.position,state.stage_repairs.position+3,
                saved.stage_repairs.position)
            &&state.diffusion_boundary==saved.diffusion_boundary
            &&state.boundary_flux_capture==saved.boundary_flux_capture;
    };
    const auto attempt=[&](arch::scheduler::StageExecutionContext& selected,
        std::span<const amr::BlockHandle> selected_handles,const char* expected) {
        bool rejected=false;
        try {
            arch::scheduler::ScopedStageBinding binding(selected,selected_handles);
            if(active.size()==1) {
                auto& block=control.pool->GetBlock(active.front());
                Numerics::Diffusion::detail::advance_single_rkl(block,eos,block.grid,
                    runtime.configuration(),dt,dt_fe,runtime.boundaries(),method,rz);
            } else {
                Numerics::Diffusion::advance_amr_rkl(control,dt,dt_fe,
                    runtime.boundaries(),eos,runtime.configuration(),method,rz);
            }
        } catch(const std::logic_error& error) {
            rejected=std::string(error.what()).find(expected)!=std::string::npos;
        }
        require(rejected,"Native RKL owner did not reject at its pre-write preflight");
        require(context.ledger.host_snapshot_matches(ledger_before),
            "Native RKL preflight rejection changed publication ledger");
        require(control.flux_register.host_snapshot_matches(flux_before),
            "Native RKL preflight rejection changed actual flux arena/content/species");
        require(runtime.boundaries().stage_context_matches(boundary_before),
            "Native RKL preflight rejection changed real boundary context");
        for(std::size_t n=0;n<active.size();++n) {
            const auto& block=control.pool->GetBlock(active[n]);
            require(same_state(block.fluid_state,before[n][0])
                &&same_state(block.state_next,before[n][1])
                &&same_state(block.state_scratch,before[n][2]),
                "Native RKL preflight rejection changed an actual state slot/receipt");
        }
    };
    auto missing_acceptance=context;
    missing_acceptance.post_boundary_acceptance={};
    attempt(missing_acceptance,handles,"requires post-boundary acceptance");
    auto device=context;device.side=arch::state::ExecutionSide::Device;
    attempt(device,handles,"requires a Host stage binding");
    // ScopedStageBinding rejects an empty span itself. Keep this span nonempty
    // so the negative reaches the real single/composite RKL extent preflight.
    std::vector<amr::BlockHandle> wrong_extent(handles.begin(),handles.end());
    if(wrong_extent.size()==1)wrong_extent.push_back(wrong_extent.front());
    else wrong_extent.pop_back();
    attempt(context,wrong_extent,"stage domain extent mismatch");
}

/** Actual Runtime/EOS gate witnesses; no numerical advance, alternate EOS or output. */
static void real_native_eos_boundary_gate() {
    using namespace arch;
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    using Slot=state::StateSlot;
    SimConfig config;
    config.grid.dim=2;config.grid.geometry="cylindrical";
    config.grid.nblockx1=2;config.grid.nblockx2=1;config.grid.nblockx3=0;
    config.grid.x1_min=0.;config.grid.x1_max=2.;config.grid.x2_min=-1.;config.grid.x2_max=1.;
    config.grid.x1l_boundary_type="outflow";config.grid.x1r_boundary_type="outflow";
    config.grid.x2l_boundary_type="outflow";config.grid.x2r_boundary_type="outflow";
    SpeciesManager species;species.add_species("real-gas",1.,1.,1.4,3.);
    ObservedIdealGas eos(species);
    amr::AMRControl control(8,2);control.tree->InitRootGrid(config,1,rz);
    for(int id:control.tree->GetActiveBlocks()) {
        auto& block=control.pool->GetBlock(id);
        for(int cell=0;cell<block.grid.GetTotalSize();++cell) {
            block.fluid_state.set(cell,{1.,0.,0.,0.,10.});block.fluid_state.X(0,cell)=1.;
        }
        block.fluid_state.rho[block.grid.GetIndex(block.grid.Is()-1,block.grid.Js(),0)]=-77.;
    }
    RunState start{};SimulationController counters(config,start);
    BCHandler boundary(config,rz);
    driver::DriverRuntime runtime(control,boundary,config,species,counters);
    std::vector<std::vector<double>> before;
    for(int id:control.tree->GetActiveBlocks())before.push_back(control.pool->GetBlock(id).fluid_state.rho);
    bool unbound=false;
    try {runtime.initialize_topology();}
    catch(const std::logic_error& error) {
        unbound=std::string(error.what()).find("explicitly bound EOS")!=std::string::npos;
    }
    require(unbound&&runtime.handles().empty(),"Native initialization skipped its mandatory actual EOS binding");
    for(std::size_t i=0;i<before.size();++i)
        require(before[i]==control.pool->GetBlock(control.tree->GetActiveBlocks()[i]).fluid_state.rho,
            "Unbound native initialization wrote physical ghosts");
    runtime.bind_native_rz_eos(eos);runtime.initialize_topology();
    require(eos.pressure_calls>0,"Actual native initialization did not call real IdealGas");
    auto context=runtime.stage_context();
    const auto version=context.ledger.inspect({runtime.handles().front(),Slot::Current}).interior.version;
    const auto saved=context.ledger.snapshot_host();
    const auto stage_snapshot=boundary.snapshot_stage_context();
    const auto rejects_before_eos=[&](const auto& action,const char* message) {
        eos.pressure_calls=0;bool rejected=false;
        try {action();}catch(const std::logic_error&) {rejected=true;}
        require(rejected&&eos.pressure_calls==0,message);
        require(context.ledger.host_snapshot_matches(saved),"Rejected native gate changed publication/ghost ledger");
    };
    context.side=state::ExecutionSide::Device;
    rejects_before_eos([&]{context.post_boundary_acceptance(context,Slot::Current,version);},
        "Native gate accepted actual Device context or evaluated its EOS");
    context.side=state::ExecutionSide::Host;
    rejects_before_eos([&]{context.post_boundary_acceptance(context,static_cast<Slot>(77),version);},
        "Native gate accepted unknown slot or evaluated its EOS");
    state::StateResidencyLedger foreign_ledger(context.ledger.active_epoch());
    // Every foreign Current record is independently valid/readable. Its
    // rejection must be the real owner identity, rather than missing storage.
    for(const auto handle:runtime.handles()) {
        const auto current=context.ledger.inspect({handle,Slot::Current});
        foreign_ledger.register_block(handle,current.interior.version,
            current.interior.completion,state::ExecutionSide::Host);
        foreign_ledger.publish_ghost({handle,Slot::Current},state::ExecutionSide::Host,
            current.ghost.version,current.ghost.completion);
        foreign_ledger.require_readable({handle,Slot::Current},
            {state::ExecutionSide::Host,version,true,true});
    }
    const auto foreign_before=foreign_ledger.snapshot_host();
    scheduler::StageExecutionContext foreign{state::ExecutionSide::Host,foreign_ledger,context.clock};
    rejects_before_eos([&]{context.post_boundary_acceptance(foreign,Slot::Current,version);},
        "Native gate accepted foreign actual ledger");
    require(foreign_ledger.host_snapshot_matches(foreign_before),
        "Native gate rejection changed otherwise-valid foreign ledger");
    boundary.configure_stage(.125,arch::boundary::BoundaryPurpose::Diffusion);
    rejects_before_eos([&]{context.post_boundary_acceptance(context,Slot::Current,version);},
        "Native gate accepted changed real BC time/purpose/revision");
    boundary.restore_stage_context_noexcept(stage_snapshot);
    auto& last=control.pool->GetBlock(control.tree->GetActiveBlocks().back()).fluid_state;
    const double last_energy=last.eng.back();last.eng.pop_back();
    rejects_before_eos([&]{context.post_boundary_acceptance(context,Slot::Current,version);},
        "Late block layout failure was discovered after first-patch EOS calls");
    last.eng.push_back(last_energy);
    // A genuine later-block version mismatch after real BC/exchange must veto
    // every ghost publication before the first EOS call, rather than accepting
    // the earlier block. This is a lease-order witness, not physical error data.
    const std::array<amr::BlockHandle,1> late{runtime.handles().back()};
    (void)scheduler::publish_completed_interior(context,late,Slot::Current);
    const auto divergent=context.ledger.snapshot_host();
    eos.pressure_calls=0;bool version_rejected=false;
    try {runtime.ensure_fluid_ghosts();}catch(const std::logic_error&) {version_rejected=true;}
    require(version_rejected&&eos.pressure_calls==0&&context.ledger.host_snapshot_matches(divergent),
        "Whole-domain native boundary did not reject late unreadable version before EOS/publication");

    // Real IdealGas can reject a positive finite thermal state when its actual
    // Cv makes T=e/Cv underflow. The configured zero lower bound is deliberate
    // for this negative witness; no successful state is warmed or repaired.
    SimConfig cold=config;cold.numerics.sml_rho=0.;cold.numerics.min_eint=0.;
    SpeciesManager cold_species;cold_species.add_species("large-Cv",1.,1.,1.4,std::ldexp(1.,900));
    ObservedIdealGas cold_eos(cold_species);
    amr::AMRControl cold_control(8,2);cold_control.tree->InitRootGrid(cold,1,rz);
    for(int id:cold_control.tree->GetActiveBlocks()) {
        auto& block=cold_control.pool->GetBlock(id);
        for(int cell=0;cell<block.grid.GetTotalSize();++cell) {
            block.fluid_state.set(cell,{1.,0.,0.,0.,std::ldexp(1.,-900)});block.fluid_state.X(0,cell)=1.;
        }
    }
    SimulationController cold_counters(cold,start);BCHandler cold_boundary(cold,rz);
    driver::DriverRuntime cold_runtime(cold_control,cold_boundary,cold,cold_species,cold_counters);
    cold_runtime.bind_native_rz_eos(cold_eos);bool eos_rejected=false;
    try {cold_runtime.initialize_topology();}
    catch(const std::runtime_error& error) {
        eos_rejected=std::string(error.what()).find("closure/EOS rejected")!=std::string::npos;
    }
    require(eos_rejected&&cold_eos.pressure_calls>0&&cold_runtime.handles().empty(),
        "Actual EOS-domain failure published initial native topology or skipped IdealGas");
    std::cout<<"RZ_ACTUAL_EOS_BOUNDARY_GATE unbound_before_write=1 actual_side_ledger_slot_bc=1"
        <<" late_layout_before_eos=1 late_version_before_publication=1 actual_eos_domain_rejected=1\n";
}
/** Actual nonzero-angular RKL1 rejection, native macro rollback and safe half-step.
 * The antiderivatives below define physical input/reference only. The actual
 * selected diffusion producer, phased boundary service and Runtime EOS own all
 * numerical advancement/acceptance; no manually written FE update is executed.
 */
namespace angular_runtime_checks {
using namespace arch;
using state::StateSlot;
constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
constexpr double e0=1./33554432.;
using rz_runtime_witness::bits;
using rz_runtime_witness::near;
using rz_runtime_witness::fields;
using rz_runtime_witness::same_repairs;
using rz_runtime_witness::slots;
using rz_runtime_witness::FieldsWitness;

/** Independent native means: M=V, J=Omega*C, Et=V*e0+Omega^2*C/2.
 * Signed reflected annuli retain signed V/C and positive W; the actual axis
 * service subsequently constructs its own true mirrored logical ghosts.
 */
FluidVector initial_mean(double lower,double upper) {
    const long double l=lower,h=upper;
    const long double v=(h*h-l*l)/2.L,w=(h*h*h-l*l*l)/3.L;
    const long double c=(h*h*h*h-l*l*l*l)/4.L;
    const long double omega=std::max(std::abs(l),std::abs(h))<=1.L?0.L:1.L;
    return {1.,0.,0.,double(omega*c/w),double(e0+omega*omega*c/(2.L*v))};
}

/** Real root, selection, bound EOS, Runtime, scheduler and selected Hydro owner. */
struct Fixture {
    SimConfig config;
    SpeciesManager species;
    std::unique_ptr<IdealGas> eos;
    amr::AMRControl control{8,2};
    RunState start{};
    std::unique_ptr<SimulationController> controller;
    std::unique_ptr<boundary::ScopedUserBoundarySelection> selection;
    std::unique_ptr<BCHandler> bc;
    std::unique_ptr<driver::DriverRuntime> runtime;
    std::unique_ptr<Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>>> hydro;
    std::optional<scheduler::StageExecutionContext> context;
    driver::DriverStageWorkspace workspace;
    dispatch::ResolvedExecutionPlan plan{};
    int diffusion_calls=0,hydro_calls=0,burn_calls=0;
    bool candidate_seen=false;
    FluidVector observed_candidate;
    state::StateVersion candidate_version{};
    StateSlot candidate_slot=StateSlot::Current;
    double dt_fe=0.,burn_advice=.75;

    Fixture() {
        // Every chart/face/control is fixed before Tree, selection or handler
        // construction; immutable compiled BC kinds are never changed later.
        config.grid.dim=2;config.grid.geometry="cylindrical";
        config.grid.nblockx1=1;config.grid.nblockx2=1;config.grid.nblockx3=0;
        config.grid.x1_min=0.;config.grid.x1_max=16.;config.grid.x2_min=0.;config.grid.x2_max=16.;
        config.grid.amr_max_blocks=8;config.amr.lrefinemin=0;config.amr.lrefinemax=0;
        config.grid.x1l_boundary_type="outflow";config.grid.x1r_boundary_type="user";
        config.grid.x2l_boundary_type=config.grid.x2r_boundary_type="periodic";
        config.numerics.solver_name="HLLC";config.numerics.reconstruction="pcm";
        config.numerics.time_integrator="euler";
        config.physics.diffusion.use_diffusion=true;
        config.physics.diffusion.use_viscous_diffusion=true;config.physics.diffusion.nu_visc=1.;
        config.physics.diffusion.use_thermal_diffusion=false;
        config.physics.diffusion.use_species_diffusion=false;
        config.io.tmax=8.;
        require(config.physics.diffusion.diff_cfl==.8&&e0>config.numerics.min_eint,
            "angular fixture changed the frozen diffusion CFL or original thermal bound");
        species.add_species("angular-gas",1.,1.,1.4,2.);
        eos=std::make_unique<IdealGas>(1.4,species);
        control.tree->InitRootGrid(config,1,rz);control.flux_register.EnsureSpecies(1);
        require(control.tree->GetActiveBlocks().size()==1,"angular fixture lacks its true single root");
        auto& b=block();b.RequireNativeGeometryIdentity();
        require(b.level==0&&b.logical_x1==0&&b.logical_x2==0
            &&b.grid.dyadic_identity.bound&&b.grid.dyadic_identity.periodic_axial
            &&b.grid.ng==amr::MAX_NG&&b.grid.dx1==1.&&b.grid.dx2==1.,
            "angular fixture lost actual root support/spacing/provenance");
        for(auto* input:slots(b)) {
            input->stage_repairs.reset(1,state::RepairSemantics::RzVolumeAngular);
            // Initialize padding as finite distinguishable diagnostic data; it
            // is neither a logical density stencil nor a boundary source.
            for(int cell=0;cell<b.grid.GetTotalSize();++cell) {
                input->set(cell,{1.,0.,0.,0.,e0});input->enuc_rate[cell]=.25+cell/8.;input->X(0,cell)=1.;
            }
            for(int j=0;j<b.grid.GetTotalY();++j)for(int i=0;i<b.grid.GetTotalX();++i)
                input->set(b.grid.GetIndex(i,j,0),initial_mean(b.grid.GetFacePosL(i),b.grid.GetFacePosR(i)));
        }
        start.time=.375;start.step=1;start.has_timestep_state=true;start.dt_old=.375;
        start.repairs.reset(1,state::RepairSemantics::RzVolumeAngular);
        controller=std::make_unique<SimulationController>(config,start);
        boundary::ResolvedUserBoundaries callbacks;
        callbacks.identity="actual-angular-runtime-outer-analytic";
        callbacks.physical=[](const boundary::PhysicalBoundaryContext& c) {
            require(c.axis==boundary::BoundaryAxis::X1&&c.side==boundary::BoundarySide::Upper
                &&std::isfinite(c.time)&&c.ghost_point.r_cy>=16.
                &&(c.purpose==boundary::BoundaryPurpose::Hydro||c.purpose==boundary::BoundaryPurpose::Diffusion),
                "angular outer callback lost actual positive physical face/time/purpose");
            PrimitiveData point;point.rho=1.;point.u=0.;point.v=0.;point.w=c.ghost_point.r_cy;
            point.SetTemperature(e0/2.);point.mass_fractions={1.};
            boundary::PhysicalBoundaryData data;data.hydro=point;return data;
        };
        selection=std::make_unique<boundary::ScopedUserBoundarySelection>(std::move(callbacks),config,species);
        bc=std::make_unique<BCHandler>(config,rz);bc->bind(*eos,species);
        bc->configure_stage(controller->t_current,boundary::BoundaryPurpose::Hydro);
        runtime=std::make_unique<driver::DriverRuntime>(control,*bc,config,species,*controller);
        runtime->bind_native_rz_eos(*eos);runtime->initialize_topology();
        hydro=std::make_unique<Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>>>(*eos,rz);
        plan.time_integrator=dispatch::TimeIntegratorId::Euler;
        plan.diffusion_integrator=dispatch::DiffusionIntegratorId::Rkl1;
        context.emplace(runtime->stage_context());
        const auto candidate=driver::calculate_timestep_candidates(*runtime,workspace,*eos,&plan);
        dt_fe=candidate.diffusion_forward_euler;near(dt_fe,1./8.,"actual angular Runtime FE row reference failed");
        require(runtime->handles().size()==1&&context->hydro_preparation==nullptr,
            "angular fixture fabricated a source journal or lost actual topology");
    }
    amr::Block& block(){return control.pool->GetBlock(control.tree->GetActiveBlocks().front());}
    int first_cell() {const auto& g=block().grid;return g.GetIndex(g.Is(),g.Js(),0);}

    /** Keep the genuine Runtime gate and observe the actual stage before it acts.
     * No token/success is manufactured and no producer or EOS call is replaced.
     */
    void refresh_gate() {
        runtime->bind_native_boundary_acceptance(*context,runtime->handles());
        const auto science=context->post_boundary_acceptance;
        require(bool(science),"angular fixture lost its actual bound Runtime EOS gate");
        context->post_boundary_acceptance=[this,science](const scheduler::StageExecutionContext& actual,
            StateSlot slot,state::StateVersion version) {
            const auto expected=scheduler::make_rkl_plan(scheduler::RklMethod::RKL1,1).stages.front().output_slot;
            if(diffusion_calls==1&&slot==expected) {
                const auto member=TimeIntegration::hydro_boundary_state_member(slot);
                observed_candidate=(block().*member).get(first_cell());
                candidate_version=version;candidate_slot=slot;candidate_seen=true;
            }
            science(actual,slot,version);
        };
    }
    /** Same genuine boundary configure/preparation owner as Driver.h. */
    void bind_frame(double half_dt) {
        context->step_start_time=controller->t_current;context->step_dt=2.*half_dt;
        context->boundary_start_time=controller->t_current;context->boundary_step_dt=half_dt;
        context->configure_boundary_context=[this](double time,boundary::BoundaryPurpose purpose) {
            bc->configure_stage(time,purpose);refresh_gate();
        };
        context->physical_boundary_preparation=[this](StateSlot slot,double time,boundary::BoundaryPurpose purpose) {
            context->configure_boundary_context(time,purpose);runtime->ensure_fluid_ghosts(slot);
        };
        context->configure_boundary_context(controller->t_current,boundary::BoundaryPurpose::Hydro);
        runtime->bind_boundary_accounting(*context);
        // Install the real observer/storage before the rollback snapshot. It
        // remains aliased across all slots and its planes are checked deeply.
        const auto one=scheduler::make_rkl_plan(scheduler::RklMethod::RKL1,1);
        context->rkl_flux_capture_begin(one.stages.front(),one);
        runtime->ensure_fluid_ghosts();
        const auto ready=context->ledger.inspect({runtime->handles().front(),StateSlot::Current});
        context->ledger.require_readable({runtime->handles().front(),StateSlot::Current},
            {state::ExecutionSide::Host,ready.interior.version,true,true});
    }
    void diffuse(double interval) {
        ++diffusion_calls;
        require(runtime->active_host_hydro_transaction()!=nullptr,
            "angular actual diffusion did not borrow its outer owner");
        require(DiffFunction::compute_stages(DiffFunction::RKLOrder::First,interval,dt_fe,
            config.physics.diffusion.diff_cfl,config.physics.diffusion.max_stages)==1,
            "angular Runtime witness did not select genuine one-stage RKL1");
        driver::advance_diffusion(*runtime,workspace,*context,*eos,&plan,controller->step_count,interval,dt_fe);
    }
};

/** Actual first D/2 fails thermally and the complete native macro owner rolls back. */
void rejected_macro() {
    Fixture f;const double half_dt=f.config.physics.diffusion.diff_cfl*f.dt_fe;
    f.bind_frame(half_dt);
    const FieldsWitness fields_before(f.block());
    const auto owner_before=driver::HostHydroTransaction::snapshot_owner(*f.runtime,*f.context);
    const auto identity_before=f.block().grid.dyadic_identity;
    const auto old_time=f.controller->t_current,old_dt=f.controller->dt_old,old_burn=f.burn_advice;
    const int old_step=f.controller->step_count;
    bool rejected=false;
    try {
        driver::NativeMacroStepAdvice advice(*f.runtime,*f.controller,f.burn_advice);
        (void)f.controller->calculate_next_dt(f.context->step_dt,f.burn_advice);f.burn_advice=1.e99;
        scheduler::ScopedStageBinding binding(*f.context,f.runtime->handles());
        driver::execute_driver_macro_step(*f.runtime,*f.context,f.hydro.get(),false,
            [&](driver::BurnHalf,double,state::CompletionToken)->state::CompletionToken {
                ++f.burn_calls;throw std::logic_error("inactive angular Burn was invoked");
            },
            [&](double interval){f.diffuse(interval);},
            [&](double interval){++f.hydro_calls;driver::advance_hydro(*f.runtime,f.workspace,*f.context,&f.plan,
                interval,&SolverEuler::solve<BCHandler>,nullptr,f.hydro.get());},
            [](driver::CpuStage,auto&& execute){execute();});
        advice.commit();
    } catch(const driver::NativeBoundaryAcceptanceError& error) {
        // Completed ghosts are visited first; a genuine signed/periodic image
        // can be the first bad cell. The actual envelope/phase is authoritative,
        // not a guessed active index or a broad accepted exception string.
        rejected=error.pool_index==f.block().id&&error.handle==f.runtime->handles().front()
            &&error.slot==f.candidate_slot&&error.version==f.candidate_version
            &&error.diagnostic.phase==RzThermodynamics::AcceptancePhase::effective_thermal
            &&error.diagnostic.status==state::Status::unresolved_energy
            &&error.diagnostic.inertia_mapping_valid;
        if(!rejected)throw;
        std::cout<<"RZ_ANGULAR_RKL1_REJECT phase=effective_thermal i="<<error.diagnostic.i
            <<" j="<<error.diagnostic.j<<" index="<<error.diagnostic.index
            <<" actual="<<error.what()<<'\n';
    }
    require(rejected&&f.candidate_seen&&f.diffusion_calls==1&&f.hydro_calls==0&&f.burn_calls==0,
        "angular macro did not reach the actual first diffusion thermal rejection");
    near(f.observed_candidate.rho,1.,"negative angular proposal changed density");
    near(f.observed_candidate.mom_u,0.,"negative angular proposal changed radial momentum");
    near(f.observed_candidate.mom_v,0.,"negative angular proposal changed axial momentum");
    near(f.observed_candidate.mom_w,9./32.,"negative angular actual RKL1 angular mean reference failed");
    near(f.observed_candidate.eng,e0+15./512.,"negative angular actual RKL1 energy mean reference failed");
    // Independent first annulus M=1/2, W=1/3, C=1/4; no production closure is the oracle.
    const double j=f.observed_candidate.mom_w/3.;
    const double thermal=f.observed_candidate.eng-4.*j*j;
    near(thermal,e0-3./512.,"negative angular independent recovered thermal reference failed");
    require(thermal<0.,"negative angular finite-step reference lost its physical sign");
    fields_before.matches(f.block());
    require(driver::HostHydroTransaction::owner_matches(*f.runtime,*f.context,owner_before)
        &&GridMetrics::equal_identity(f.block().grid.dyadic_identity,identity_before),
        "angular failed macro did not restore ledger/register/clock/BC/budgets/handles/root owners");
    require(bits(f.controller->t_current,old_time)&&f.controller->step_count==old_step
        &&bits(f.controller->dt_old,old_dt)&&bits(f.burn_advice,old_burn)
        &&f.context->hydro_preparation==nullptr,"angular failed macro accepted time/advice or source identity");
}

/** A real accepted RKL half-step, not a no-op-Hydro or complete macro substitute. */
void accepted_half() {
    Fixture f;constexpr double interval=1./16.;f.bind_frame(interval);
    const auto& g=f.block().grid;const auto identity=g.dyadic_identity;
    const double old_time=f.controller->t_current,old_dt=f.controller->dt_old,old_burn=f.burn_advice;
    const int old_step=f.controller->step_count;
    std::array<std::array<const double*,7>,3> original{};
    const auto before_slots=slots(f.block());
    for(int s=0;s<3;++s)for(int field=0;field<7;++field)original[s][field]=(before_slots[s]->*fields[field]).data();
    scheduler::ScopedStageBinding binding(*f.context,f.runtime->handles());
    {
        driver::HostHydroTransaction transaction(*f.runtime,*f.context,*f.hydro);
        f.context->configure_boundary_context(old_time,boundary::BoundaryPurpose::Diffusion);
        f.diffuse(interval);
        f.context->configure_boundary_context(old_time+interval,boundary::BoundaryPurpose::Hydro);
        f.runtime->ensure_fluid_ghosts();
        transaction.validate_storage();transaction.commit();
    }
    require(f.diffusion_calls==1&&f.hydro_calls==0&&f.burn_calls==0&&f.candidate_seen,
        "positive angular half skipped actual nonzero RKL or invoked another module");
    const auto actual=f.block().fluid_state.get(f.first_cell());
    near(actual.rho,1.,"positive angular half changed density");
    near(actual.mom_u,0.,"positive angular half changed radial momentum");
    near(actual.mom_v,0.,"positive angular half changed axial momentum");
    near(actual.mom_w,45./256.,"positive angular actual RKL mean reference failed");
    near(actual.eng,e0+75./4096.,"positive angular actual RKL energy mean reference failed");
    const double j=actual.mom_w/3.,thermal=actual.eng-4.*j*j;
    near(thermal,e0+75./16384.,"positive angular independent thermal reference failed");
    require(thermal>f.config.numerics.min_eint,"positive angular half lost admissible physical thermal state");
    const auto ready=f.context->ledger.inspect({f.runtime->handles().front(),StateSlot::Current});
    f.context->ledger.require_readable({f.runtime->handles().front(),StateSlot::Current},
        {state::ExecutionSide::Host,ready.interior.version,true,true});
    scheduler::detail::require_settled_destination(ready);
    RzThermodynamics::validate_completed_patch_eos(f.block().fluid_state,g,1,
        {f.config.numerics.sml_rho,f.config.numerics.min_eint,f.config.numerics.max_eint},*f.eos);
    const auto after_slots=slots(f.block());std::array<bool,3> used{};
    for(const auto* input:after_slots) {
        int allocation=-1;
        for(int s=0;s<3;++s) {
            bool match=true;for(int field=0;field<7;++field)
                match=match&&((input->*fields[field]).data()==original[s][field]);
            if(match)allocation=s;
        }
        require(allocation>=0&&!used[allocation],"positive angular RKL changed original seven-array allocation groups");
        used[allocation]=true;
        require(input->stage_repairs.semantics==state::RepairSemantics::RzVolumeAngular,
            "positive angular half changed the native receipt measure");
        for(double value:input->stage_repairs.values)require(value==0.,"positive angular half added a repair/heating receipt");
    }
    for(double value:f.runtime->repair_budget().values)require(value==0.,"positive angular accepted half concealed heating/repair");
    require(GridMetrics::equal_identity(g.dyadic_identity,identity)
        &&bits(f.controller->t_current,old_time)&&f.controller->step_count==old_step
        &&bits(f.controller->dt_old,old_dt)&&bits(f.burn_advice,old_burn)
        &&!f.runtime->active_host_hydro_transaction(),"positive angular half altered root/time/advice or retained an owner");
    std::cout<<std::setprecision(17)<<"RZ_ANGULAR_RKL1_ACCEPTED_HALF dt="<<interval
        <<" actual_fe="<<f.dt_fe<<" thermal="<<thermal<<" mean_energy="<<actual.eng
        <<" real_completed_eos=1 macro_acceptance=0\n";
}

void run(){rejected_macro();accepted_half();}
} // namespace angular_runtime_checks


#ifdef ARCH_RZ_RUNTIME_CONTRACT_EMBEDDED
// The private standalone diagnostic keeps its original main and scope; only
// the existing embedded owner links/executes the external-source translation unit.
void run_native_rz_runtime_external_contract();
/** Existing gravity-stage lane: call each coherent Runtime body exactly once. */
void run_native_rz_runtime_boundary_contract() {
    angular_runtime_checks::run();
    run_native_rz_runtime_external_contract();
}
#endif

#ifndef ARCH_RZ_RUNTIME_CONTRACT_EMBEDDED
int main(int argc,char** argv) {
 try {
  if(argc==2&&std::string(argv[1])=="--angular-runtime-only") {
   angular_runtime_checks::run();
   std::cout<<"PASS: actual native Runtime angular RKL1 rejection/rollback and accepted half\n";return 0;
  }
  require(argc==1,"Unknown private RZ Runtime fixture selection");
  real_native_eos_boundary_gate();
  angular_runtime_checks::run();
  for(bool mixed:{false,true})for(int direction:{0,1})for(double inner:{0.,1.}) {
    SimConfig config{};
    config.grid.dim=2;config.grid.geometry="cylindrical";
    config.grid.nblockx1=mixed&&direction==0?2:1;
    config.grid.nblockx2=mixed&&direction==1?2:1;config.grid.nblockx3=0;
    config.grid.x1_min=inner;config.grid.x1_max=inner+2.;
    config.grid.x2_min=-1.;config.grid.x2_max=1.;
    config.grid.amr_max_blocks=32;config.amr.lrefinemin=0;config.amr.lrefinemax=1;
    amr::AMRControl control(32,2);
    if(mixed)control.tree->LoadLeafGrid(config,0,{1,1,1,1,0},
        {0,1,0,1,static_cast<std::uint32_t>(direction==0?1:0)},
        {0,0,1,1,static_cast<std::uint32_t>(direction==0?0:1)},{0,0,0,0,0});
    else control.tree->LoadLeafGrid(config,0,{0},{0},{0},{0});
    SpeciesManager species;species.add_species("a",1.,1.,1.4,3.);
    species.add_species("b",2.,1.,1.4,3.);
    IdealGas eos(1.4,species);
    const auto& active=control.tree->GetActiveBlocks();
    for(int id:active) {
        auto& b=control.pool->GetBlock(id);b.fluid_state.InitSpecies(2);
        for(int cell=0;cell<b.grid.GetTotalSize();++cell) {
            b.fluid_state.set(cell,{2.,2.,6.,4.,100.});
            b.fluid_state.X(0,cell)=.6;b.fluid_state.X(1,cell)=.4;
        }
    }
    RunState start{};
    SimulationController counters(config,start);
    BCHandler boundary(config,GridMetrics::GeometrySemantics::AxisymmetricRz);
    arch::driver::DriverRuntime runtime(control,boundary,config,species,counters);
    runtime.bind_native_rz_eos(eos);
    runtime.initialize_topology();
    require(runtime.geometry_semantics()==GridMetrics::GeometrySemantics::AxisymmetricRz,
        "Runtime chart identity");
    auto verify=[&]() {
        auto context=runtime.stage_context();
        for(std::size_t n=0;n<active.size();++n) {
            const auto& b=control.pool->GetBlock(active[n]);const auto& g=b.grid;
            const auto coherence=context.ledger.inspect({runtime.handles()[n],arch::state::StateSlot::Current});
            context.ledger.require_readable({runtime.handles()[n],arch::state::StateSlot::Current},
                {arch::state::ExecutionSide::Host,coherence.interior.version,true,true});
            if(g.x1_min==inner) {
                const int cell=g.GetIndex(g.Is()-1,g.Js()+amr::BLOCK_NY/2,0);
                const auto u=b.fluid_state.get(cell);const double sign=inner==0.?-1.:1.;
                require(u.rho==2.&&u.mom_u==sign*2.&&u.mom_v==6.&&u.mom_w==sign*4.,
                    "Runtime axis/outflow donor parity");
            }
        }
    };
    verify();
    // Force another real halo fill; retain Current interior version.
    const auto v=runtime.stage_context().ledger.inspect(
        {runtime.handles()[0],arch::state::StateSlot::Current}).interior.version;
    for(int id:active) {
        auto& b=control.pool->GetBlock(id);const auto& g=b.grid;
        if(g.x1_min==inner) b.fluid_state.set(
            g.GetIndex(g.Is()-1,g.Js()+amr::BLOCK_NY/2,0),{-123.,-123.,-123.,-123.,-123.});
    }
    runtime.ensure_fluid_ghosts();verify();
    require(runtime.stage_context().ledger.inspect(
        {runtime.handles()[0],arch::state::StateSlot::Current}).interior.version==v,
        "Runtime halo refresh changed interior version");
    // Actual Driver candidate aggregation over all native leaf cells.
    config.numerics.cfl=.4;
    arch::driver::DriverStageWorkspace workspace;
    double expected=1.e99;
    for(int id:active) {
        const auto& g=control.pool->GetBlock(id).grid;
        for(int i=g.Is();i<g.Ie();++i) {
            // Independent constant-density annular integrals, not the
            // production closure: beta=rho*W^2/(V*I). Native m_phi=J/W
            // has rotational K/V=beta*m_phi^2/(2*rho), not point K/V.
            const long double lo=g.x1_min+(i-g.ng)*g.dx1,hi=lo+g.dx1;
            const long double d2=hi*hi-lo*lo;
            const long double d3=hi*hi*hi-lo*lo*lo;
            const long double d4=hi*hi*hi*hi-lo*lo*lo*lo;
            const long double beta=(8.L/9.L)*d3*d3/(d2*d4);
            const double internal=static_cast<double>((100.L-10.L-4.L*beta)/2.L);
            const double sound=std::sqrt(1.4*.4*internal);
            expected=std::min(expected,.5*config.numerics.cfl/
                ((1.+sound)/g.dx1+(3.+sound)/g.dx2));
        }
    }
    const auto candidates=arch::driver::calculate_timestep_candidates(
        runtime,workspace,eos,nullptr);
    require(std::isfinite(candidates.hydro)
        &&std::abs(candidates.hydro-expected)<=2.e-12*std::max(1.,expected),
        "Driver RZ CFL physical dr/dz aggregation");
    config.physics.diffusion.use_diffusion=true;
    config.physics.diffusion.use_viscous_diffusion=true;
    config.physics.diffusion.nu_visc=.01;
    double diffusion_expected=1.e99;
    for(int id:active) {
        const auto& b=control.pool->GetBlock(id);
        diffusion_expected=std::min(diffusion_expected,DiffFlux::adaptive_dt_diff(
            b.fluid_state,eos,b.grid,config,1.,
            GridMetrics::GeometrySemantics::AxisymmetricRz));
    }
    for(auto integrator:{arch::dispatch::DiffusionIntegratorId::Rkl1,
                        arch::dispatch::DiffusionIntegratorId::Rkl2}) {
        arch::dispatch::ResolvedExecutionPlan plan{};
        plan.diffusion_integrator=integrator;
        const auto diff=arch::driver::calculate_timestep_candidates(runtime,workspace,eos,&plan);
        require(diff.diffusion_forward_euler==diffusion_expected,
            "Driver RZ diffusion dropped chart");
        require(diff.hydro==candidates.hydro,"Diffusion changed Hydro candidate");
        std::cout<<std::setprecision(17)<<"RZ_RUNTIME_DT direction="<<direction
            <<" inner="<<inner<<" rkl="<<(integrator==arch::dispatch::DiffusionIntegratorId::Rkl1?1:2)
            <<" hydro="<<diff.hydro<<" independent_hydro="<<expected
            <<" diffusion_fe="<<diff.diffusion_forward_euler
            <<" diffusion_sts="<<diff.diffusion_sts<<'\n';
    }
    // Real Driver -> single/composite RKL -> scheduler, with a zero
    // viscous operator witness (constant axial translation, no swirl).
    for(auto integrator:{arch::dispatch::DiffusionIntegratorId::Rkl1,
                        arch::dispatch::DiffusionIntegratorId::Rkl2}) {
        for(int id:active) {
            auto& b=control.pool->GetBlock(id);
            for(int cell=0;cell<b.grid.GetTotalSize();++cell) {
                b.fluid_state.set(cell,{2.,0.,6.,0.,21.5});
                b.fluid_state.X(0,cell)=.6;b.fluid_state.X(1,cell)=.4;
            }
        }
        auto context=runtime.stage_context();
        (void)arch::scheduler::publish_completed_interior(
            context,runtime.handles(),arch::state::StateSlot::Current);
        runtime.ensure_fluid_ghosts();
        arch::dispatch::ResolvedExecutionPlan plan{};plan.diffusion_integrator=integrator;
        const auto diff=arch::driver::calculate_timestep_candidates(runtime,workspace,eos,&plan);
        const double dt=3.*diff.diffusion_forward_euler*config.physics.diffusion.diff_cfl;
        const auto order=integrator==arch::dispatch::DiffusionIntegratorId::Rkl1
            ?DiffFunction::RKLOrder::First:DiffFunction::RKLOrder::Second;
        const int stages=DiffFunction::compute_stages(order,dt,diff.diffusion_forward_euler,
            config.physics.diffusion.diff_cfl,config.physics.diffusion.max_stages);
        require(stages>=2,"RKL fixture did not exercise recurrence");
        native_rkl_preflight_before_write(runtime,eos,context,
            integrator==arch::dispatch::DiffusionIntegratorId::Rkl1
                ?arch::scheduler::RklMethod::RKL1:arch::scheduler::RklMethod::RKL2,
            dt,diff.diffusion_forward_euler);
        const auto before=context.ledger.inspect(
            {runtime.handles()[0],arch::state::StateSlot::Current}).interior.version;
        {
            arch::scheduler::ScopedStageBinding scope(context,runtime.handles());
            arch::driver::advance_diffusion(runtime,workspace,context,eos,&plan,0,dt,
                diff.diffusion_forward_euler);
        }
        double error=0.;
        for(std::size_t n=0;n<active.size();++n) {
            const auto& b=control.pool->GetBlock(active[n]);const auto& g=b.grid;
            const auto coherence=context.ledger.inspect(
                {runtime.handles()[n],arch::state::StateSlot::Current});
            require(coherence.interior.version.value==before.value+stages,
                "RKL real stage publication count");
            context.ledger.require_readable({runtime.handles()[n],arch::state::StateSlot::Current},
                {arch::state::ExecutionSide::Host,coherence.interior.version,true,true});
            for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
                const int cell=g.GetIndex(i,j,0);const auto u=b.fluid_state.get(cell);
                for(double e:{u.rho-2.,u.mom_u,u.mom_v-6.,u.mom_w,u.eng-21.5,
                    b.fluid_state.X(0,cell)-.6,b.fluid_state.X(1,cell)-.4})
                    error=std::max(error,std::abs(e));
            }
        }
        require(std::isfinite(error)&&error<=2.e-12,"RZ actual RKL constant state drift");
        if(mixed)require(control.RequireFluxTopologyPlan(2,
            GridMetrics::GeometrySemantics::AxisymmetricRz,-1,true).semantics
            ==GridMetrics::GeometrySemantics::AxisymmetricRz,"RKL AMR chart identity");
        std::cout<<"RZ_RUNTIME_RKL mixed="<<mixed<<" direction="<<direction
            <<" inner="<<inner<<" rkl="<<(integrator==arch::dispatch::DiffusionIntegratorId::Rkl1?1:2)
            <<" stages="<<stages<<" max_error="<<error<<'\n';
    }
    if(!mixed) {
        // Independent RZ heat equation witness:
        // T=10+a*r^2+b*z^2; (1/r)d_r(r*d_r T)+d_zz T=4a+2b.
        // Constant rho/Cv/alpha -> dE/dt=rho*Cv*alpha*(4a+2b).
        config.physics.diffusion.use_viscous_diffusion=false;
        config.physics.diffusion.use_thermal_diffusion=true;
        config.physics.diffusion.alpha_therm=.01;
        auto& b=control.pool->GetBlock(active.front());const auto& g=b.grid;
        constexpr double a=.2,beta=.3,rho=2.,cv=3.,alpha=.01;
        const auto energy=[&](int i,int j) {
            const double radius=g.x1_min+(i-g.ng+.5)*g.dx1;
            const double z=g.x2_min+(j-g.ng+.5)*g.dx2;
            return rho*cv*(10.+a*radius*radius+beta*z*z);
        };
        for(auto integrator:{arch::dispatch::DiffusionIntegratorId::Rkl1,
                            arch::dispatch::DiffusionIntegratorId::Rkl2}) {
            for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i) {
                const int cell=g.GetIndex(i,j,0);
                b.fluid_state.set(cell,{rho,0.,0.,0.,energy(i,j)});
                b.fluid_state.X(0,cell)=.6;b.fluid_state.X(1,cell)=.4;
            }
            auto context=runtime.stage_context();
            (void)arch::scheduler::publish_completed_interior(
                context,runtime.handles(),arch::state::StateSlot::Current);
            runtime.ensure_fluid_ghosts();
            arch::dispatch::ResolvedExecutionPlan plan{};plan.diffusion_integrator=integrator;
            const auto candidate=arch::driver::calculate_timestep_candidates(runtime,workspace,eos,&plan);
            const double dt=3.*candidate.diffusion_forward_euler*config.physics.diffusion.diff_cfl;
            const auto order=integrator==arch::dispatch::DiffusionIntegratorId::Rkl1
                ?DiffFunction::RKLOrder::First:DiffFunction::RKLOrder::Second;
            const int stages=DiffFunction::compute_stages(order,dt,candidate.diffusion_forward_euler,
                config.physics.diffusion.diff_cfl,config.physics.diffusion.max_stages);
            {
                arch::scheduler::ScopedStageBinding scope(context,runtime.handles());
                arch::driver::advance_diffusion(runtime,workspace,context,eos,&plan,0,dt,
                    candidate.diffusion_forward_euler);
            }
            // Finite stage stencil cannot reach these cells from the outflow
            // boundaries. Axis Neumann parity is exact for r^2 when inner=0.
            double error=0.,activity=0.;int checked=0;
            for(int j=g.Js()+stages;j<g.Je()-stages;++j)
                for(int i=(inner==0.?g.Is():g.Is()+stages);i<g.Ie()-stages;++i) {
                    const int cell=g.GetIndex(i,j,0);const auto u=b.fluid_state.get(cell);
                    const double initial=energy(i,j);
                    const double expected=initial+dt*rho*cv*alpha*(4.*a+2.*beta);
                    error=std::max(error,std::abs(u.eng-expected));
                    activity=std::max(activity,std::abs(u.eng-initial));
                    require(u.rho==rho&&u.mom_u==0.&&u.mom_v==0.&&u.mom_w==0.,
                        "Thermal-only RKL changed mass/momentum");
                    ++checked;
                }
            require(checked>0&&activity>1.e-6,"Nonzero RKL witness inactive");
            require(std::isfinite(error)&&error<=2.e-12,"RZ thermal RKL quadratic reference");
            std::cout<<"RZ_RUNTIME_THERMAL_RKL direction="<<direction<<" inner="<<inner
                <<" rkl="<<(integrator==arch::dispatch::DiffusionIntegratorId::Rkl1?1:2)
                <<" stages="<<stages<<" checked="<<checked<<" dt="<<dt
                <<" expected_increment="<<dt*rho*cv*alpha*(4.*a+2.*beta)
                <<" max_error="<<error<<" activity="<<activity<<'\n';
        }
        config.physics.diffusion.use_thermal_diffusion=false;
        config.physics.diffusion.use_viscous_diffusion=true;
        config.physics.diffusion.alpha_therm=0.;
    }
    if(mixed) {
        config.physics.diffusion.use_viscous_diffusion=false;
        config.physics.diffusion.use_thermal_diffusion=true;
        config.physics.diffusion.alpha_therm=.01;
        constexpr long double pi=3.141592653589793238462643383279502884L;
        const auto integral=[&]() {
            long double total=0.;
            for(int id:active) {
                const auto& b=control.pool->GetBlock(id);const auto& g=b.grid;
                for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
                    const long double lo=(long double)g.x1_min+(i-g.ng)*(long double)g.dx1;
                    const long double hi=lo+g.dx1;
                    const long double volume=pi*(hi-lo)*(hi+lo)*g.dx2;
                    total+=(long double)b.fluid_state.eng[g.GetIndex(i,j,0)]*volume;
                }
            }
            return total;
        };
        for(auto integrator:{arch::dispatch::DiffusionIntegratorId::Rkl1,
                            arch::dispatch::DiffusionIntegratorId::Rkl2}) {
            for(int id:active) {
                auto& b=control.pool->GetBlock(id);const auto& g=b.grid;
                for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i) {
                    const int cell=g.GetIndex(i,j,0);
                    const double radius=g.x1_min+(i-g.ng+.5)*g.dx1;
                    const double z=g.x2_min+(j-g.ng+.5)*g.dx2;
                    const double temperature=10.+.2*std::cos((double)pi*(radius-inner)/2.)
                        +.1*std::cos((double)pi*z);
                    b.fluid_state.set(cell,{2.,0.,0.,0.,6.*temperature});
                    b.fluid_state.X(0,cell)=.6;b.fluid_state.X(1,cell)=.4;
                }
            }
            const long double before=integral();
            std::vector<double> initial;
            for(int id:active) {
                const auto& b=control.pool->GetBlock(id);const auto& g=b.grid;
                for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i)
                    initial.push_back(b.fluid_state.eng[g.GetIndex(i,j,0)]);
            }
            auto context=runtime.stage_context();
            (void)arch::scheduler::publish_completed_interior(
                context,runtime.handles(),arch::state::StateSlot::Current);
            runtime.ensure_fluid_ghosts();
            arch::dispatch::ResolvedExecutionPlan plan{};plan.diffusion_integrator=integrator;
            const auto candidate=arch::driver::calculate_timestep_candidates(runtime,workspace,eos,&plan);
            const double dt=3.*candidate.diffusion_forward_euler*config.physics.diffusion.diff_cfl;
            {
                arch::scheduler::ScopedStageBinding scope(context,runtime.handles());
                arch::driver::advance_diffusion(runtime,workspace,context,eos,&plan,0,dt,
                    candidate.diffusion_forward_euler);
            }
            const long double after=integral();
            const double relative=(double)(std::abs(after-before)/std::max(1.L,std::abs(before)));
            double activity=0.;std::size_t index=0;
            for(int id:active) {
                const auto& b=control.pool->GetBlock(id);const auto& g=b.grid;
                for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i)
                    activity=std::max(activity,std::abs(b.fluid_state.eng[g.GetIndex(i,j,0)]-initial[index++]));
            }
            require(activity>1.e-6,"Mixed RKL thermal transfer inactive");
            require(std::isfinite(relative)&&relative<=2.e-12,"Mixed RZ RKL energy accounting");
            std::cout<<"RZ_MIXED_THERMAL_RKL direction="<<direction<<" inner="<<inner
                <<" rkl="<<(integrator==arch::dispatch::DiffusionIntegratorId::Rkl1?1:2)
                <<" energy_before="<<(double)before<<" energy_after="<<(double)after
                <<" relative_error="<<relative<<" activity="<<activity<<'\n';
        }
        config.physics.diffusion.use_thermal_diffusion=false;
        config.physics.diffusion.use_viscous_diffusion=true;
        config.physics.diffusion.alpha_therm=0.;
    }
    config.physics.diffusion.use_diffusion=false;
    auto& bad=control.pool->GetBlock(active.front());
    const int bad_cell=bad.grid.GetIndex(bad.grid.Is(),bad.grid.Js(),0);
    const double saved=bad.fluid_state.rho[bad_cell];
    const auto density_failure_ledger=runtime.stage_context().ledger.snapshot_host();
    bad.fluid_state.rho[bad_cell]=std::numeric_limits<double>::quiet_NaN();
    bool invalid_density=false;
    try{(void)arch::driver::calculate_timestep_candidates(runtime,workspace,eos,nullptr);}
    catch(const std::exception& error){
        // Native candidates now complete real halos/EOS before reduction.
        // Accept only the actual malformed-density diagnostic at that owner,
        // never an unrelated exception or a relaxed numerical threshold.
        const std::string message=error.what();
        invalid_density=message=="Coordinate seam donor has invalid fluid state"
            ||message=="AMR prolongation requires an admissible parent fluid state."
            ||message.find("RZ native provisional state rejected")!=std::string::npos
            ||message.find("RZ native closure/EOS rejected")!=std::string::npos;
        if(!invalid_density)throw;
    }
    require(invalid_density,"Driver ignored invalid active CFL density");
    require(runtime.stage_context().ledger.host_snapshot_matches(density_failure_ledger),
        "Malformed native CFL density published a boundary/version prefix");
    bad.fluid_state.rho[bad_cell]=saved;
    const auto current_version=runtime.stage_context().ledger.inspect(
        {runtime.handles()[0],arch::state::StateSlot::Current}).interior.version;
    bool device=false,regrid=false;
    try{(void)runtime.prepare_backend_bindings();}catch(const std::logic_error&){device=true;}
    try{(void)runtime.perform_regrid(0,0.);}catch(const std::logic_error&){regrid=true;}
    require(device&&regrid,"Unmigrated RZ consumer accepted");
    require(runtime.stage_context().ledger.inspect(
        {runtime.handles()[0],arch::state::StateSlot::Current}).interior.version==current_version,
        "Rejected Runtime consumer changed version");
    require(counters.step_count==0&&counters.t_current==0.,"Runtime fixture advanced time");
    std::cout<<"RZ_RUNTIME_HALO direction="<<direction<<" inner="<<inner
        <<" leaves="<<active.size()<<" version="<<v.value<<" PASS\n";
  }
  return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
#endif // ARCH_RZ_RUNTIME_CONTRACT_EMBEDDED
