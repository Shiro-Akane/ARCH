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
static void require(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
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
using Field=std::vector<double> FluidState::*;
constexpr std::array<Field,7> fields{&FluidState::rho,&FluidState::mom_u,
    &FluidState::mom_v,&FluidState::mom_w,&FluidState::eng,
    &FluidState::enuc_rate,&FluidState::mass_fractions};
constexpr double scalar_budget=2.e-12; // Existing owner's unchanged scalar budget.

bool bits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
bool bits(const std::vector<double>& a,const std::vector<double>& b) {
    return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),
        [](double x,double y){return bits(x,y);});
}
void near(double actual,double expected,const char* message) {
    require(std::isfinite(actual)&&std::abs(actual-expected)
        <=scalar_budget*std::max(1.,std::abs(expected)),message);
}
bool same_repairs(const state::RepairBudget& a,const state::RepairBudget& b) {
    return a.semantics==b.semantics&&bits(a.values,b.values)
        &&a.block_uid==b.block_uid&&a.stage==b.stage&&bits(a.time,b.time)
        &&std::equal(std::begin(a.position),std::end(a.position),std::begin(b.position),
            [](double x,double y){return bits(x,y);});
}
std::array<FluidState*,3> slots(amr::Block& b){return {&b.fluid_state,&b.state_next,&b.state_scratch};}

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

/** Snapshot values and original allocation/alias ownership independently of rollback. */
struct FieldsWitness {
    std::array<FluidState,3> values;
    std::array<std::array<const double*,7>,3> leases{};
    std::array<std::optional<boundary::BoundaryFluxCaptureStorage>,3> captures;
    explicit FieldsWitness(amr::Block& b):values{b.fluid_state,b.state_next,b.state_scratch} {
        const auto actual=slots(b);
        for(int s=0;s<3;++s) {
            for(int f=0;f<7;++f)leases[s][f]=(actual[s]->*fields[f]).data();
            if(actual[s]->boundary_flux_capture)captures[s]=*actual[s]->boundary_flux_capture;
        }
    }
    void matches(amr::Block& b) const {
        const auto actual=slots(b);
        for(int s=0;s<3;++s) {
            const auto& a=*actual[s];const auto& saved=values[s];
            require(a.n_species_==saved.n_species_&&a.block_total_size_==saved.block_total_size_,
                "angular macro rollback changed an actual slot layout");
            for(int f=0;f<7;++f)require((a.*fields[f]).data()==leases[s][f]
                &&bits(a.*fields[f],saved.*fields[f]),
                "angular macro rollback changed source/output/ghost/padding bits or a seven-array lease");
            require(same_repairs(a.stage_repairs,saved.stage_repairs)
                &&a.diffusion_boundary==saved.diffusion_boundary
                &&a.boundary_flux_capture==saved.boundary_flux_capture,
                "angular macro rollback changed native receipts or control/capture alias ownership");
            require(bool(a.boundary_flux_capture)==bool(captures[s]),
                "angular macro rollback changed capture presence");
            if(captures[s]) {
                const auto& now=*a.boundary_flux_capture;const auto& old=*captures[s];
                for(int face=0;face<6;++face)require(bits(now.stage[face],old.stage[face])
                    &&bits(now.initial[face],old.initial[face]),"angular macro rollback changed shared capture planes");
                require(bits(now.weight,old.weight)&&bits(now.initial_weight,old.initial_weight)
                    &&now.save_initial==old.save_initial,"angular macro rollback changed capture weights");
            }
        }
    }
};

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

/** Actual Runtime external-source contracts; no public/native/device grant.
 * Independent V/W antiderivatives and a three-observation density fit audit
 * actual source work. Evolution, source receipts, RK and EOS stay production.
 */
namespace external_runtime_checks {
using namespace arch;
using state::StateSlot;
constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
constexpr double dt=1.e-4;
constexpr long double budget=1.e-12L;
using Integrator=driver::IntegratorSolve;
using Hydro=Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>>;
using angular_runtime_checks::bits;
using angular_runtime_checks::near;
using angular_runtime_checks::fields;
const long double pi=std::acos(-1.L);

long double power_integral(long double l,long double h,int p) {
    return (std::pow(h,p+1)-std::pow(l,p+1))/(p+1);
}
void close_budget(long double a,long double b,long double scale,const char* message) {
    require(std::isfinite(a)&&std::isfinite(b)&&scale>0.
        &&std::abs(a-b)<=budget*scale,message);
}
/** Independent residual-coordinate polynomial fitted to actual native V means.
 * Solve the 3x3 moment equations in long double; no production density/closure
 * or quadrature function supplies the reference. These smooth fixtures require
 * the uncontracted polynomial to be positive, so a positivity contraction is
 * outside this oracle and fails explicitly rather than borrowing its output.
 */
struct MomentReference {
    long double v,w,m,b,c,j,omega;
};
MomentReference moment_reference(const FluidState& s,const Grid& g,int i,int j) {
    const int begin=std::clamp(i-1,0,g.GetTotalX()-3);
    const long double l=g.GetFacePosL(i),h=g.GetFacePosR(i);
    const long double mid=(l+h)/2.,dx=h-l;
    require(l>=0.&&h>l,"external reference requires a positive real annulus");
    long double a[3][4]{};
    for(int n=0;n<3;++n) {
        const long double lo=(g.GetFacePosL(begin+n)-mid)/dx;
        const long double hi=(g.GetFacePosR(begin+n)-mid)/dx;
        const long double rr=mid/dx;
        const long double volume=rr*power_integral(lo,hi,0)+power_integral(lo,hi,1);
        require(volume>0.,"external independent density observation lacks real V");
        for(int q=0;q<3;++q)
            a[n][q]=(rr*power_integral(lo,hi,q)+power_integral(lo,hi,q+1))/volume;
        a[n][3]=s.rho[g.GetIndex(begin+n,j,0)];
    }
    for(int q=0;q<3;++q) {
        int row=q;for(int n=q+1;n<3;++n)if(std::abs(a[n][q])>std::abs(a[row][q]))row=n;
        require(a[row][q]!=0.&&std::isfinite(a[row][q]),"external reference singular moment observations");
        for(int k=q;k<4;++k)std::swap(a[q][k],a[row][k]);
        const long double divisor=a[q][q];for(int k=q;k<4;++k)a[q][k]/=divisor;
        for(int n=0;n<3;++n)if(n!=q) {
            const long double factor=a[n][q];for(int k=q;k<4;++k)a[n][k]-=factor*a[q][k];
        }
    }
    const long double p[3]{a[0][3],a[1][3],a[2][3]};
    const auto density=[&](long double t){return p[0]+t*(p[1]+t*p[2]);};
    long double minimum=std::min(density(-.5L),density(.5L));
    if(p[2]!=0.) {
        const long double vertex=-p[1]/(2.*p[2]);
        if(vertex>-.5L&&vertex<.5L)minimum=std::min(minimum,density(vertex));
    }
    require(minimum>0.&&std::isfinite(minimum),"external smooth reference requires uncontracted positive density");
    const auto integral=[&](int radial_power) {
        long double total=0.;
        // Exact binomial expansion through degree five in the independent t chart.
        for(int q=0;q<3;++q)for(int k=0;k<=radial_power;++k) {
            long double choose=1.;
            for(int n=0;n<k;++n)choose*=static_cast<long double>(radial_power-n)/(n+1);
            total+=dx*p[q]*choose*std::pow(mid,radial_power-k)*std::pow(dx,k)
                *power_integral(-.5L,.5L,q+k);
        }
        return total;
    };
    MomentReference result;
    result.v=power_integral(l,h,1);result.w=power_integral(l,h,2);
    result.m=integral(1);result.b=integral(2);result.c=integral(3);
    result.j=s.mom_w[g.GetIndex(i,j,0)]*result.w;result.omega=result.j/result.c;
    require(result.m>0.&&result.c>0.&&std::isfinite(result.omega),"external reference inertia is invalid");
    close_budget(result.m,s.rho[g.GetIndex(i,j,0)]*result.v,result.m,
        "external independent polynomial did not preserve actual native density mean");
    return result;
}

struct Observer final:Numerics::IHydroSolver {
    const Hydro& actual;
    const IdealGas& eos;
    const amr::AMRControl& control;
    const Physical::Gravity::ExternalGravity& force;
    mutable std::mutex mutex;
    mutable std::array<long double,4> body{},unweighted_body{};
    mutable std::array<long double,5> outward{},unweighted_outward{};
    mutable int calls=0;
    scheduler::StageDescriptor descriptor{};
    double input_time=0.;
    bool abort_after_source=false;
    mutable bool source_reference_seen=false;
    // A test-only pre-producer defense probe; empty for every original case.
    // One actual prepared receipt is exercised, never a synthetic source frame.
    std::function<void(amr::AMRControl*,int,const FluidState&,const Grid&,double,
        const Physical::Gravity::IGravityPolicy*,const NumericsConfig&)> application_probe;
    Observer(const Hydro& h,const IdealGas& e,const amr::AMRControl& c,
        const Physical::Gravity::ExternalGravity& f):actual(h),eos(e),control(c),force(f){}
    GridMetrics::GeometrySemantics geometry_semantics() const noexcept override {return actual.geometry_semantics();}
    Numerics::HostHydroStorageContract host_storage_contract() const noexcept override {
        return actual.host_storage_contract();
    }
    /** Only observe immutable actual stage input and actual published capture;
     * one real selected producer performs every field/source/face operation.
     */
    void evaluate_patch(amr::AMRControl* ctrl,int id,const FluidState& input,const Grid& grid,double interval,
        std::vector<FluidVector>& du,std::vector<double>& ds,const Physical::Gravity::IGravityPolicy* gravity,
        const NumericsConfig& cfg,double weight=1.,void* stream=nullptr,
        const boundary::HostHydroBoundaryAuthority* walls=nullptr) const override
    {
        require(ctrl==&control&&gravity==&force&&interval==dt
            &&weight==descriptor.flux_register_weight&&walls,
            "external observer lost actual policy/domain/interval/descriptor/wall authority");
        const auto& block=ctrl->pool->GetBlock(id);
        const auto member=TimeIntegration::hydro_boundary_state_member(descriptor.input_slot);
        block.RequireNativeGeometryIdentity();
        require(&(block.*member)==&input,"external observer input is not its actual selected slot");
        // The real Runtime source/slot/wall preparation already completed.
        // A rejecting probe must leave before the selected physical producer.
        if(application_probe)application_probe(ctrl,id,input,grid,interval,gravity,cfg);
        std::array<long double,4> source{};
        for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
            const auto ref=moment_reference(input,grid,i,j);
            const long double dz=grid.GetAxialFacePosR(j)-grid.GetAxialFacePosL(j);
            const int cell=grid.GetIndex(i,j,0);
            const long double factor=2.*pi*dz*interval;
            source[0]+=factor*force.g_x*ref.m;
            source[1]+=factor*force.g_y*ref.m;
            source[2]+=factor*force.g_z*ref.b;
            source[3]+=factor*(force.g_x*input.mom_u[cell]*ref.v
                +force.g_y*input.mom_v[cell]*ref.v+force.g_z*ref.omega*ref.b);
        }
        actual.evaluate_patch(ctrl,id,input,grid,interval,du,ds,gravity,cfg,weight,stream,walls);
        std::array<long double,5> out{},raw_out{};
        const auto capture=input.boundary_flux_capture;
        require(bool(capture),"external actual Hydro did not have real Runtime capture storage");
        const int species=input.GetNumSpecies(),count_fields=6+species;
        for(int axis=0;axis<2;++axis)for(int side=0;side<2;++side) {
            const auto& plane=capture->stage[2*axis+side];if(plane.empty())continue;
            require(block.face_neighbors[2*axis+side].count==0,
                "external Runtime capture invented an internal AMR surface");
            const int nmax=axis==0?grid.Je()-grid.Js():grid.Ie()-grid.Is();
            require(plane.size()==static_cast<std::size_t>(nmax*count_fields),"external capture layout changed");
            for(int n=0;n<nmax;++n) {
                const int i=axis==0?(side?grid.Ie()-1:grid.Is()):grid.Is()+n;
                const int j=axis==0?grid.Js()+n:(side?grid.Je()-1:grid.Js());
                const long double l=grid.GetFacePosL(i),h=grid.GetFacePosR(i);
                const long double dz=grid.GetAxialFacePosR(j)-grid.GetAxialFacePosL(j);
                const long double r=side?h:l;
                const long double area=axis==0?2.*pi*r*dz:2.*pi*power_integral(l,h,1);
                const long double torque=axis==0?2.*pi*r*r*dz:2.*pi*power_integral(l,h,2);
                const long double factor=(side?1.L:-1.L)*interval;
                const int map[5]{0,4,3,5,6};
                for(int q=0;q<5;++q) {
                    const long double value=plane[n*count_fields+map[q]];
                    const long double amount=factor*(q==2?torque:area)*value;
                    out[q]+=amount;raw_out[q]+=amount/weight;
                }
            }
        }
        std::lock_guard lock(mutex);
        for(int q=0;q<4;++q){body[q]+=weight*source[q];unweighted_body[q]+=source[q];}
        for(int q=0;q<5;++q){outward[q]+=out[q];unweighted_outward[q]+=raw_out[q];}
        ++calls;source_reference_seen=true;
        if(abort_after_source)throw std::runtime_error("COLD_ACTUAL_SOURCE_OBSERVED");
    }
    void update_patch(const FluidState& old,const FluidState& current,FluidState& next,
        const std::vector<FluidVector>& du,const std::vector<double>& ds,const Grid& grid,
        double old_weight,double update_weight,const NumericsConfig& cfg,void* stream=nullptr) const override
    {actual.update_patch(old,current,next,du,ds,grid,old_weight,update_weight,cfg,stream);}
};

struct Fixture {
    SimConfig config;
    SpeciesManager species;
    std::unique_ptr<IdealGas> eos;
    amr::AMRControl control{32,2};
    RunState start{};
    std::unique_ptr<SimulationController> controller;
    std::unique_ptr<boundary::ScopedUserBoundarySelection> selection;
    std::unique_ptr<BCHandler> bc;
    std::unique_ptr<driver::DriverRuntime> runtime;
    std::unique_ptr<Physical::Gravity::ExternalGravity> force;
    std::unique_ptr<Hydro> actual;
    std::unique_ptr<Observer> observer;
    std::unique_ptr<driver::GravityStage> source;
    std::optional<scheduler::StageExecutionContext> context;
    dispatch::ResolvedExecutionPlan plan{};
    driver::DriverStageWorkspace workspace;
    bool poison=false,poisoned=false;
    double configured_time=0.;
    boundary::BoundaryPurpose configured_purpose=boundary::BoundaryPurpose::Hydro;
    Fixture(int direction,bool open,double phi,dispatch::TimeIntegratorId method,bool simple=false,
        bool cold=false,double inner=1.,double radial=0.) {
        // Freeze configuration before the real logical/compiled BC owner.
        config.grid.dim=2;config.grid.geometry="cylindrical";
        config.grid.nblockx1=simple?1:(direction==0?2:1);
        config.grid.nblockx2=simple?1:(direction==0?1:2);config.grid.nblockx3=0;
        config.grid.x1_min=inner;config.grid.x1_max=inner+2.;
        config.grid.x2_min=simple?0.:-1.;config.grid.x2_max=simple?2.:1.;
        config.grid.amr_max_blocks=32;config.amr.lrefinemin=0;config.amr.lrefinemax=simple?0:1;
        config.grid.x1l_boundary_type=config.grid.x1r_boundary_type=open?"outflow":"reflecting";
        config.grid.x2l_boundary_type=config.grid.x2r_boundary_type=open?"outflow":"reflecting";
        config.numerics.solver_name="HLLC";config.numerics.reconstruction="pcm";
        config.numerics.time_integrator=method==dispatch::TimeIntegratorId::Euler?"euler":
            method==dispatch::TimeIntegratorId::Rk2?"rk2":"rk3";
        config.numerics.entropy_fix_coeff=0.;config.numerics.hll_roe_wave_speed=true;
        config.numerics.sml_rho=1.e-14;config.numerics.min_eint=1.e-14;config.numerics.max_eint=1.e10;
        if(cold) {
            config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="user";
            config.grid.x2l_boundary_type=config.grid.x2r_boundary_type="periodic";
        }
        config.physics.gravity.type="external";config.physics.gravity.g_x=radial;
        config.physics.gravity.g_y=0.;config.physics.gravity.g_z=phi;
        config.physics.diffusion.use_diffusion=false;config.io.tmax=1.;
        species.add_species("gas0",1.,1.,1.4,3.);species.add_species("gas1",2.,1.,1.4,3.);
        eos=std::make_unique<IdealGas>(1.4,species);
        if(simple)control.tree->InitRootGrid(config,2,rz);
        else control.tree->LoadLeafGrid(config,2,{1,1,1,1,0},{0,1,0,1,static_cast<std::uint32_t>(direction==0?1:0)},
            {0,0,1,1,static_cast<std::uint32_t>(direction==0?0:1)},{0,0,0,0,0},rz);
        control.flux_register.EnsureSpecies(2);
        for(int id:control.tree->GetActiveBlocks()) {
            auto& b=control.pool->GetBlock(id);b.RequireNativeGeometryIdentity();const auto& g=b.grid;
            require(g.dyadic_identity.bound,"external actual fixture lacks authentic Native root provenance");
            for(auto* s:angular_runtime_checks::slots(b)) {
                s->stage_repairs.reset(2,state::RepairSemantics::RzVolumeAngular);
                for(int c=0;c<g.GetTotalSize();++c){s->set(c,{2.,0.,0.,0.,100.});s->X(0,c)=.6;s->X(1,c)=.4;s->enuc_rate[c]=.125+c/16.;}
                for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i) {
                    const int c=g.GetIndex(i,j,0);
                    const double l=g.GetFacePosL(i),h=g.GetFacePosR(i),r=g.GetCellCenterX(i),z=g.GetCellCenterY(j);
                    if(simple) {
                        const long double v=power_integral(l,h,1),w=power_integral(l,h,2);
                        const long double mass=cold?.875L*v+.25L*power_integral(l,h,3):2.L*v;
                        const long double inertia=cold?.875L*power_integral(l,h,3)+.25L*power_integral(l,h,5)
                            :2.L*power_integral(l,h,3);
                        const long double thermal=cold?0x1p-25L:100.L;
                        s->set(c,{double(mass/v),0.,0.,double(inertia/w),double(thermal*mass/v+.5L*inertia/v)});
                    } else {
                        // The original 24-case numerical inputs, unchanged.
                        const double rho=2.+.1*std::cos(double(pi)*(r-inner))*.1*std::cos(double(pi)*z);
                        const double ur=.03*std::sin(double(pi)*(r-inner)/2.);
                        const double uz=.02*std::sin(double(pi)*(z+1.)/2.);
                        const double wc=h<=0.?-GridMetrics::Rz::AngularReconstructionRadius(-h,-l):
                            GridMetrics::Rz::AngularReconstructionRadius(l,h);
                        const double up=.15*wc*(1.+.2*std::cos(double(pi)*z));
                        s->set(c,{rho,rho*ur,rho*uz,rho*up,12.5+.5*rho*(ur*ur+uz*uz+up*up)});
                        const double x=.6+.02*std::cos(double(pi)*z);s->X(0,c)=x;s->X(1,c)=1.-x;
                    }
                }
            }
        }
        start.time=.375;start.step=1;start.has_timestep_state=true;start.dt_old=dt;
        start.repairs.reset(2,state::RepairSemantics::RzVolumeAngular);
        controller=std::make_unique<SimulationController>(config,start);
        if(cold) {
            boundary::ResolvedUserBoundaries callbacks;
            callbacks.identity="native-external-actual-cold-quadratic-rho";
            callbacks.physical=[](const boundary::PhysicalBoundaryContext& c) {
                require(c.axis==boundary::BoundaryAxis::X1&&c.ghost_point.r_cy>0.
                    &&std::isfinite(c.time),"cold external boundary lost its actual positive point/time");
                PrimitiveData primitive;const double r=c.ghost_point.r_cy;
                primitive.rho=.875+.25*r*r;primitive.u=primitive.v=0.;primitive.w=r;
                primitive.SetTemperature(0x1p-25/3.);primitive.mass_fractions={.6,.4};
                boundary::PhysicalBoundaryData data;data.hydro=primitive;return data;
            };
            selection=std::make_unique<boundary::ScopedUserBoundarySelection>(
                std::move(callbacks),config,species);
        }
        bc=std::make_unique<BCHandler>(config,rz);bc->bind(*eos,species);
        bc->configure_stage(controller->t_current,boundary::BoundaryPurpose::Hydro);
        runtime=std::make_unique<driver::DriverRuntime>(control,*bc,config,species,*controller);
        runtime->bind_native_rz_eos(*eos);runtime->initialize_topology();
        force=std::make_unique<Physical::Gravity::ExternalGravity>(radial,0.,phi);
        actual=std::make_unique<Hydro>(*eos,rz);
        observer=std::make_unique<Observer>(*actual,*eos,control,*force);
        plan.time_integrator=method;
        context.emplace(runtime->stage_context());
    }
    /** Actual configure callback recreates the unique real EOS acceptance. */
    void refresh() {
        runtime->bind_native_boundary_acceptance(*context,runtime->handles());
        const auto real=context->post_boundary_acceptance;
        context->post_boundary_acceptance=[this,real](const scheduler::StageExecutionContext& frame,
            StateSlot slot,state::StateVersion version) {
            const auto stages=scheduler::supported_hydro_time_plan(hydro_method()).stages;
            if(poison&&!poisoned&&observer->descriptor.stage==static_cast<int>(stages.size())
                &&slot==stages.back().output_slot) {
                auto& b=control.pool->GetBlock(control.tree->GetActiveBlocks().back());
                const auto member=TimeIntegration::hydro_boundary_state_member(slot);
                (b.*member).eng[b.grid.GetIndex(b.grid.Is(),b.grid.Js(),0)]=-1.;poisoned=true;
            }
            real(frame,slot,version); // Never manufacture or suppress scientific acceptance.
        };
    }
    scheduler::HydroMethod hydro_method() const {
        return plan.time_integrator==dispatch::TimeIntegratorId::Euler?scheduler::HydroMethod::Euler:
            plan.time_integrator==dispatch::TimeIntegratorId::Rk2?scheduler::HydroMethod::RK2:scheduler::HydroMethod::RK3;
    }
    Integrator solve() const {
        return plan.time_integrator==dispatch::TimeIntegratorId::Euler?&SolverEuler::solve<BCHandler>:
            plan.time_integrator==dispatch::TimeIntegratorId::Rk2?&SolverRK2::solve<BCHandler>:&SolverRK3::solve<BCHandler>;
    }
    void bind_frame() {
        context->step_start_time=controller->t_current;context->step_dt=dt;
        context->boundary_start_time=controller->t_current;context->boundary_step_dt=.5*dt;
        context->configure_boundary_context=[this](double t,boundary::BoundaryPurpose purpose){
            bc->configure_stage(t,purpose);configured_time=t;configured_purpose=purpose;refresh();
        };
        context->physical_boundary_preparation=[this](StateSlot slot,double t,boundary::BoundaryPurpose purpose) {
            context->configure_boundary_context(t,purpose);runtime->ensure_fluid_ghosts(slot);
        };
        context->configure_boundary_context(controller->t_current,boundary::BoundaryPurpose::Hydro);
        runtime->bind_boundary_accounting(*context);
        require(context->hydro_flux_capture_begin&&context->hydro_flux_capture_accept,
            "actual Native built-in boundary accounting is not integrated");
        const auto real_begin=context->hydro_flux_capture_begin;
        context->hydro_flux_capture_begin=[this,real_begin](const scheduler::StageDescriptor& d) {
            observer->descriptor=d;observer->input_time=context->step_start_time+d.input_time_fraction*context->step_dt;
            require(std::isfinite(observer->input_time)&&bits(configured_time,observer->input_time)
                &&configured_purpose==boundary::BoundaryPurpose::Hydro,
                "external actual RK boundary/source time changed");
            real_begin(d);
        };
        const auto first=scheduler::supported_hydro_time_plan(hydro_method()).stages.front();
        context->hydro_flux_capture_begin(first);
        runtime->ensure_fluid_ghosts();
    }
    /** Keep disabled modules on their actual existing functions; real Hydro/source run once. */
    void advance(const Physical::Gravity::IGravityPolicy* selected=nullptr,bool advance_time=true) {
        scheduler::ScopedStageBinding binding(*context,runtime->handles());
        driver::execute_driver_macro_step(*runtime,*context,observer.get(),false,
            [](driver::BurnHalf,double,state::CompletionToken)->state::CompletionToken {
                throw std::logic_error("inactive external fixture Burn was called");
            },
            [&](double half){driver::advance_diffusion(*runtime,workspace,*context,*eos,&plan,controller->step_count,half,1.e99);},
            [&](double interval){driver::advance_hydro(*runtime,workspace,*context,&plan,interval,solve(),
                selected?selected:force.get(),observer.get());},
            [](driver::CpuStage,auto&& execute){execute();});
        if(advance_time)controller->advance(dt); // Only after the actual owner accepted.
    }
    void bind_source() {
        source=std::make_unique<driver::GravityStage>(*runtime,force.get(),
            driver::GravityStage::Qualification::NativeRzExternalCandidate);
        require(source->active()&&source->supports_host_macro_step_journal(),"real native external journal is inactive");
        context->hydro_preparation=source.get();
    }
    std::array<long double,6> totals() const {
        std::array<long double,6> result{};
        for(int id:control.tree->GetActiveBlocks()) {
            const auto& b=control.pool->GetBlock(id);const auto& g=b.grid;
            for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
                const int c=g.GetIndex(i,j,0);const long double l=g.GetFacePosL(i),h=g.GetFacePosR(i);
                const long double dz=g.GetAxialFacePosR(j)-g.GetAxialFacePosL(j);
                const long double v=2.*pi*dz*power_integral(l,h,1),w=2.*pi*dz*power_integral(l,h,2);
                result[0]+=b.fluid_state.rho[c]*v;result[1]+=b.fluid_state.eng[c]*v;
                result[2]+=b.fluid_state.mom_w[c]*w;result[3]+=std::abs(b.fluid_state.mom_w[c])*w;
                result[4]+=b.fluid_state.rho[c]*b.fluid_state.X(0,c)*v;
                result[5]+=b.fluid_state.rho[c]*b.fluid_state.X(1,c)*v;
            }
        }return result;
    }
};

/** Check genuine final Host publication and native repair identities. */
void accepted_current(Fixture& f) {
    require(!f.runtime->active_host_hydro_transaction()&&!f.force->prepared_native_external(),
        "external accepted endpoint retained a transaction/source borrow");
    for(const auto handle:f.runtime->handles()) {
        const state::StateKey key{handle,StateSlot::Current};
        const auto coherence=f.context->ledger.inspect(key);
        scheduler::detail::require_settled_destination(coherence);
        f.context->ledger.require_readable(key,{state::ExecutionSide::Host,
            coherence.interior.version,true,false});
    }
    require(f.runtime->repair_budget().semantics==state::RepairSemantics::RzVolumeAngular,
        "external accepted endpoint changed repair measure");
    for(double value:f.runtime->repair_budget().values)
        require(value==0.,"external accepted endpoint concealed floor/heating/normalization");
    for(int id:f.control.tree->GetActiveBlocks())for(auto* slot:angular_runtime_checks::slots(f.control.pool->GetBlock(id))) {
        require(slot->stage_repairs.semantics==state::RepairSemantics::RzVolumeAngular,
            "external actual stage receipt changed native measure");
        for(double value:slot->stage_repairs.values)
            require(value==0.,"external actual stage receipt concealed repair/heating");
    }
}

/** Independent actual state/owner witness, deliberately distinct from backup logic. */
struct RollbackWitness {
    Fixture& f;
    std::vector<std::pair<int,angular_runtime_checks::FieldsWitness>> values;
    std::vector<GridMetrics::DyadicGridIdentity> roots;
    driver::HostHydroTransaction::OwnerWitness owner;
    std::array<long double,4> body;
    double time,advice;
    int step;
    explicit RollbackWitness(Fixture& actual)
        : f(actual),owner(driver::HostHydroTransaction::snapshot_owner(*f.runtime,*f.context)),
          body(f.source?f.source->external_source_budget():std::array<long double,4>{}),
          time(f.controller->t_current),advice(f.controller->dt_old),step(f.controller->step_count) {
        for(int id:f.control.tree->GetActiveBlocks()) {
            auto& b=f.control.pool->GetBlock(id);values.emplace_back(id,angular_runtime_checks::FieldsWitness(b));
            roots.push_back(b.grid.dyadic_identity);
        }
    }
    void unchanged() const {
        require(driver::HostHydroTransaction::owner_matches(*f.runtime,*f.context,owner),
            "external rejection changed real Runtime owner/ledger/register/BC/receipt identity");
        for(std::size_t n=0;n<values.size();++n) {
            auto& block=f.control.pool->GetBlock(values[n].first);values[n].second.matches(block);
            require(GridMetrics::equal_identity(roots[n],block.grid.dyadic_identity),
                "external rejection changed actual root provenance");
        }
        require(bits(time,f.controller->t_current)&&bits(advice,f.controller->dt_old)
            &&step==f.controller->step_count&&!f.runtime->active_host_hydro_transaction()
            &&!f.force->prepared_native_external(),"external rejection changed accepted time/advice/source borrow");
        if(f.source)require(f.source->external_source_budget()==body,
            "external rejection leaked a source-budget accepted prefix");
    }
};

/** Exact buffer witness for the helper's mutable operands; compare values,
 * addresses and extents, never object padding. Marked nonzero buffers expose a
 * source/scratch write even when a zeroed production delta could conceal it.
 */
struct ApplicationBufferWitness {
    std::vector<FluidVector> du,scratch;
    std::vector<double> fractions;
    const FluidVector *du_data,*scratch_data;
    const double* fractions_data;
    ApplicationBufferWitness(const std::vector<FluidVector>& d,
        const std::vector<FluidVector>& s,const std::vector<double>& x)
        :du(d),scratch(s),fractions(x),du_data(d.data()),scratch_data(s.data()),fractions_data(x.data()){}
    static bool same(const FluidVector& a,const FluidVector& b) {
        return bits(a.rho,b.rho)&&bits(a.mom_u,b.mom_u)&&bits(a.mom_v,b.mom_v)
            &&bits(a.mom_w,b.mom_w)&&bits(a.eng,b.eng);
    }
    void unchanged(const std::vector<FluidVector>& d,const std::vector<FluidVector>& s,
        const std::vector<double>& x) const {
        require(d.data()==du_data&&s.data()==scratch_data&&x.data()==fractions_data
            &&d.size()==du.size()&&s.size()==scratch.size()&&bits(x,fractions)
            &&std::equal(d.begin(),d.end(),du.begin(),same)
            &&std::equal(s.begin(),s.end(),scratch.begin(),same),
            "external application rejection changed marked dU/scratch/X operands");
    }
};

/** Each fault changes one helper operand or one actual borrowed numeric limit.
 * These are engineering identity counterexamples, not alternative physical
 * configurations or permission to change Runtime inputs during parallel work.
 */
enum class ApplicationFault {
    ForeignInput,ForeignGrid,ViewSpacing,ViewStride,ViewChart,ViewUpper,
    ViewRoot,ViewPeriodic,DtUlp,DtHalf,BoundsDensity,BoundsMinimum,BoundsMaximum,
    NumericsDensity,NumericsMinimum,NumericsMaximum
};

/** Claim a genuine prepared patch inside the real macro, then prove every
 * application mismatch fails before even scratch/EOS traversal. A caught
 * failure cannot revive/commit that claim. Deliberate one-leaf injections have
 * no concurrent configuration writer; actual accepted owners are restored by
 * the production Host macro transaction after the explicit rejecting probe.
 */
void application_guard_rejections() {
    constexpr std::array faults{
        ApplicationFault::ForeignInput,ApplicationFault::ForeignGrid,
        ApplicationFault::ViewSpacing,ApplicationFault::ViewStride,
        ApplicationFault::ViewChart,ApplicationFault::ViewUpper,
        ApplicationFault::ViewRoot,ApplicationFault::ViewPeriodic,
        ApplicationFault::DtUlp,ApplicationFault::DtHalf,
        ApplicationFault::BoundsDensity,ApplicationFault::BoundsMinimum,
        ApplicationFault::BoundsMaximum,ApplicationFault::NumericsDensity,
        ApplicationFault::NumericsMinimum,ApplicationFault::NumericsMaximum};
    int checked=0;
    for(const auto fault:faults) {
        Fixture f(0,false,.025,dispatch::TimeIntegratorId::Euler,true);
        f.bind_source();f.bind_frame();RollbackWitness accepted(f);
        bool probe_seen=false,helper_rejected=false,failed_claim=false,commit_rejected=false;
        f.observer->application_probe=[&](amr::AMRControl* control,int id,
            const FluidState& input,const Grid& grid,double interval,
            const Physical::Gravity::IGravityPolicy* policy,const NumericsConfig& numerics) {
            require(!probe_seen&&control==&f.control&&policy==f.force.get()
                &&f.control.tree->GetActiveBlocks().size()==1
                &&f.runtime->active_host_hydro_transaction()!=nullptr,
                "external application probe did not enter its real one-leaf macro owner");
            probe_seen=true;
            const auto* frame=policy->prepared_native_external();
            require(frame!=nullptr,"external application probe has no genuine prepared frame");
            auto receipt=frame->claim_patch(control,id,input,grid,interval,*policy);
            const state::Bounds original_bounds{
                numerics.sml_rho,numerics.min_eint,numerics.max_eint};
            const auto original_geometry=GridMetrics::make_geometry_view(grid,rz);
            receipt.require_application(input,grid,original_geometry,interval,original_bounds);

            const int extent=grid.GetTotalSize();
            std::vector<FluidVector> du(extent),scratch(extent);
            std::vector<double> fractions(input.GetNumSpecies());
            for(int cell=0;cell<extent;++cell) {
                du[cell]={17.+cell,-23.-cell,31.+cell,-41.-cell,53.+cell};
                scratch[cell]={-67.-cell,79.+cell,-83.-cell,97.+cell,-101.-cell};
            }
            for(std::size_t s=0;s<fractions.size();++s)fractions[s]=113.+s;
            const ApplicationBufferWitness before(du,scratch,fractions);
            const FluidState foreign_input=input;const Grid foreign_grid=grid;
            for(auto field:fields)require(bits(input.*field,foreign_input.*field),
                "external foreign-input fixture is not a value-equal distinct owner");
            require(&foreign_input!=&input&&&foreign_grid!=&grid,
                "external application owner-negative is accidentally its original object");
            const FluidState* selected_input=&input;const Grid* selected_grid=&grid;
            auto geometry=original_geometry;auto bounds=original_bounds;double applied_dt=interval;
            // Restore the injected actual numeric owner even if an unexpected
            // exception escapes; macro rollback never owns public configuration.
            struct RestoreNumerics {
                NumericsConfig& config;state::Bounds original;
                void restore() noexcept {
                    config.sml_rho=original.density;config.min_eint=original.internal_min;
                    config.max_eint=original.internal_max;
                }
                ~RestoreNumerics(){restore();}
            } restore{f.config.numerics,original_bounds};
            const auto ulp=[](double value){return std::nextafter(value,std::numeric_limits<double>::infinity());};
            switch(fault) {
            case ApplicationFault::ForeignInput:selected_input=&foreign_input;break;
            case ApplicationFault::ForeignGrid:selected_grid=&foreign_grid;break;
            case ApplicationFault::ViewSpacing:geometry.dx2=ulp(geometry.dx2);break;
            case ApplicationFault::ViewStride:++geometry.stride_y;break;
            case ApplicationFault::ViewChart:geometry.semantics=GridMetrics::GeometrySemantics::Existing;break;
            case ApplicationFault::ViewUpper:geometry.actual_block_upper[0]=ulp(geometry.actual_block_upper[0]);break;
            case ApplicationFault::ViewRoot:geometry.dyadic_identity.root_lower[0]=ulp(geometry.dyadic_identity.root_lower[0]);break;
            case ApplicationFault::ViewPeriodic:geometry.dyadic_identity.periodic_axial=!geometry.dyadic_identity.periodic_axial;break;
            case ApplicationFault::DtUlp:applied_dt=ulp(interval);break;
            case ApplicationFault::DtHalf:applied_dt=.5*interval;break;
            case ApplicationFault::BoundsDensity:bounds.density=ulp(bounds.density);break;
            case ApplicationFault::BoundsMinimum:bounds.internal_min=ulp(bounds.internal_min);break;
            case ApplicationFault::BoundsMaximum:bounds.internal_max=ulp(bounds.internal_max);break;
            case ApplicationFault::NumericsDensity:f.config.numerics.sml_rho=ulp(f.config.numerics.sml_rho);break;
            case ApplicationFault::NumericsMinimum:f.config.numerics.min_eint=ulp(f.config.numerics.min_eint);break;
            case ApplicationFault::NumericsMaximum:f.config.numerics.max_eint=ulp(f.config.numerics.max_eint);break;
            }
            const bool config_drift=fault==ApplicationFault::NumericsDensity
                ||fault==ApplicationFault::NumericsMinimum||fault==ApplicationFault::NumericsMaximum;
            const bool view_drift=fault==ApplicationFault::ViewSpacing||fault==ApplicationFault::ViewStride
                ||fault==ApplicationFault::ViewChart||fault==ApplicationFault::ViewUpper
                ||fault==ApplicationFault::ViewRoot||fault==ApplicationFault::ViewPeriodic;
            const char* expected=config_drift?"Native external Runtime configuration changed":
                view_drift?"Native external application changed its actual native geometry view":
                "Native external application changed input, interval or physical bounds";
            try {
                Physical::Gravity::add_native_external_sources(
                    std::span<FluidVector>(du),std::span<FluidVector>(scratch),std::span<double>(fractions),
                    *selected_input,*f.eos,*selected_grid,geometry,applied_dt,bounds,receipt);
            } catch(const std::logic_error& error) {
                helper_rejected=std::string(error.what())==expected;
                if(!helper_rejected)throw;
            }
            restore.restore();
            before.unchanged(du,scratch,fractions);
            require(helper_rejected,"external application identity mismatch reached source math/write");
            try {receipt.require_application(input,grid,original_geometry,interval,original_bounds);}
            catch(const std::logic_error& error) {
                failed_claim=std::string(error.what())=="Native external patch claim is not its original generation";
                if(!failed_claim)throw;
            }
            try {receipt.commit({});}
            catch(const std::logic_error& error) {
                commit_rejected=std::string(error.what())=="Native external patch claim is not its original generation";
                if(!commit_rejected)throw;
            }
            before.unchanged(du,scratch,fractions);
            require(failed_claim&&commit_rejected,
                "external failed application claim revived or committed after operands were restored");
            throw std::logic_error("NATIVE_EXTERNAL_APPLICATION_ENGINEERING_REJECTED");
        };
        bool macro_rejected=false;
        try {f.advance();}
        catch(const std::logic_error& error) {
            macro_rejected=std::string(error.what())=="NATIVE_EXTERNAL_APPLICATION_ENGINEERING_REJECTED";
            if(!macro_rejected)throw;
        }
        require(macro_rejected&&probe_seen&&helper_rejected&&failed_claim&&commit_rejected
            &&f.observer->calls==0&&!f.observer->source_reference_seen,
            "external guard witness bypassed genuine preparation or ran the normal producer after rejection");
        accepted.unchanged();
        require(bits(f.config.numerics.sml_rho,1.e-14)&&bits(f.config.numerics.min_eint,1.e-14)
            &&bits(f.config.numerics.max_eint,1.e10),"external guard probe leaked a numeric-owner injection");
        ++checked;
    }
    std::cout<<"RZ_EXTERNAL_APPLICATION_GUARD_ENGINEERING negatives="<<checked
        <<" actual_frame=1 before_source_write=1 failed_claim=1 whole_macro_rollback=1 physical_grant=0\n";
}

/** Original 24 off-axis cases and normalized 1e-12 budgets, now with actual
 * Runtime, real source journal and independent body + physical-surface audit.
 * Cumulative RK weights occur once; physical time advances after acceptance.
 */
void matrix_case(int direction,bool open,double phi,dispatch::TimeIntegratorId method) {
    Fixture f(direction,open,phi,method);f.bind_source();f.bind_frame();
    const auto before=f.totals();long double max_j=0.,max_m=0.,max_e=0.,max_x=0.;
    const auto stages=scheduler::supported_hydro_time_plan(f.hydro_method()).stages.size();
    double maximum_torque_register=0.;
    for(int n=0;n<10;++n) {
        f.bind_frame();f.advance();accepted_current(f);
        const auto after=f.totals();const auto body=f.source->external_source_budget();
        const auto& out=f.observer->outward;
        const long double js=before[3]+std::abs(out[2])+std::abs(body[2]);
        const long double ms=before[0]+std::abs(out[0]);
        const long double es=before[1]+std::abs(out[1])+std::abs(body[3]);
        close_budget(after[2]-before[2]+out[2],body[2],js,"external actual Runtime angular conservation failed");
        close_budget(after[0]-before[0]+out[0],0.,ms,"external actual Runtime mass conservation failed");
        close_budget(after[1]-before[1]+out[1],body[3],es,"external actual Runtime energy/source work balance failed");
        max_j=std::max(max_j,std::abs(after[2]-before[2]+out[2]-body[2])/js);
        max_m=std::max(max_m,std::abs(after[0]-before[0]+out[0])/ms);
        max_e=std::max(max_e,std::abs(after[1]-before[1]+out[1]-body[3])/es);
        for(int x=0;x<2;++x) {
            const long double scale=before[4+x]+std::abs(out[3+x]);
            close_budget(after[4+x]-before[4+x]+out[3+x],0.,scale,
                "external actual Runtime species conservation failed");
            max_x=std::max(max_x,std::abs(after[4+x]-before[4+x]+out[3+x])/scale);
        }
        // Original whole-field scales allow actual dU addition rounding; no
        // tiny-source tolerance replaces the original conservation budget.
        require(body[0]==0.&&body[1]==0.,"pure azimuthal external source added meridional momentum");
        close_budget(body[2],f.observer->body[2],js,"actual source J journal differs from independent V/W source");
        close_budget(body[3],f.observer->body[3],es,"actual source work differs from independent physical profile");
        const auto& observed=f.runtime->hydro_boundary_budget();const int map[]{0,4,3,5,6};
        require(observed.size()==8,"actual Native boundary observer has wrong species/field extent");
        const long double scales[]{ms,es,js,before[4],before[5]};
        for(int q=0;q<5;++q)close_budget(observed[map[q]],out[q],scales[q],
            "actual Native boundary budget differs from independent captured A/W plane integral");
        if(!open)for(int q=0;q<5;++q)close_budget(out[q],0.,scales[q],
            "reflecting actual Hydro leaked mass/energy/angular/species through a physical wall");
        near(f.controller->t_current,.375+(n+1)*dt,"external actual accepted physical time changed");
        require(f.controller->step_count==2+n,"external actual accepted step count changed");
        for(int id:f.control.tree->GetActiveBlocks())for(int face=0;face<4;++face)
            if(f.control.flux_register.HasData(id,face)) {
                const int count=face/2==0?amr::BLOCK_NY:amr::BLOCK_NX;
                for(int c=0;c<count;++c)maximum_torque_register=std::max(maximum_torque_register,
                    std::abs(f.control.flux_register.GetSummedFlux(id,face,c).mom_w));
            }
    }
    require(maximum_torque_register>0.&&f.observer->calls==static_cast<int>(10*stages*f.runtime->handles().size())
        &&f.observer->source_reference_seen,"external 24 case skipped actual CF/RK/source visits");
    const auto after=f.totals();const auto body=f.source->external_source_budget();
    const auto& out=f.observer->outward;
    const long double scale=before[3]+std::abs(out[2])+std::abs(body[2]);
    require(std::abs(after[2]-before[2]+out[2])/scale>budget,
        "missing-body negative did not distinguish external angular force");
    if(stages>1)require(std::abs(f.observer->unweighted_body[2]-body[2])/scale>budget,
        "wrong-RK body negative did not distinguish actual tableau weights");
    if(open) {
        require(std::abs(out[2])>budget*scale,"open external case has no genuine outward angular flux");
        if(stages>1)require(std::abs(f.observer->unweighted_outward[2]-out[2])/scale>budget,
            "wrong-RK boundary negative did not distinguish actual tableau weights");
    }
    std::cout<<std::setprecision(17)<<"RZ_EXTERNAL_ACTUAL_RUNTIME direction="<<direction<<" open="<<open
        <<" method="<<f.config.numerics.time_integrator<<" gphi="<<phi<<" steps=10 tn=.375 dt="<<dt
        <<" J="<<double(max_j)<<" M="<<double(max_m)<<" E="<<double(max_e)<<" X="<<double(max_x)
        <<" QJ="<<double(body[2])<<" QE="<<double(body[3])<<" Jout="<<double(out[2])<<'\n';
}

/** Nonuniform physical u_phi=r on the true [1,3] domain. Each cell has its own
 * native mean; the whole-domain m_phi,W=60/13 differs from m_phi,V=13/3.
 * Source work is compared to independent initial physical antiderivatives,
 * not an EOS/closure output, and the actual source consumes one Euler input.
 */
void physical_work() {
    Fixture f(0,false,.025,dispatch::TimeIntegratorId::Euler,true);f.bind_source();f.bind_frame();
    const auto initial=f.totals();
    const long double v=2.*pi*2.*power_integral(1.,3.,1);
    const long double w=2.*pi*2.*power_integral(1.,3.,2);
    close_budget(initial[2]/w,60.L/13.L,60.L/13.L,"true native whole-domain J/W reference failed");
    close_budget(2.*power_integral(1.,3.,2)/power_integral(1.,3.,1),13.L/3.L,13.L/3.L,
        "independent V physical work mean reference failed");
    long double wrong_raw_work=0.;
    for(int id:f.control.tree->GetActiveBlocks()) {
        const auto& b=f.control.pool->GetBlock(id);const auto& g=b.grid;
        for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
            const long double cell_v=2.*pi*(g.GetAxialFacePosR(j)-g.GetAxialFacePosL(j))
                *power_integral(g.GetFacePosL(i),g.GetFacePosR(i),1);
            wrong_raw_work+=dt*f.force->g_z*b.fluid_state.mom_w[g.GetIndex(i,j,0)]*cell_v;
        }
    }
    const long double exact=dt*f.force->g_z*2.*w;
    f.advance();accepted_current(f);const auto actual=f.source->external_source_budget();
    require(actual[0]==0.&&actual[1]==0.,"pure physical work case gained meridional body momentum");
    close_budget(actual[2],exact,std::abs(exact),"true source J does not match exact physical antiderivative");
    close_budget(actual[3],exact,std::abs(exact),"true source E does not match exact physical work");
    require(std::abs(wrong_raw_work-exact)>budget*std::abs(exact),
        "raw J/W-as-point work negative became indistinguishable");
    std::cout<<"RZ_EXTERNAL_ACTUAL_WORK native_mean=60/13 physical_V_mean=13/3 actual_QE="
        <<double(actual[3])<<" wrong_raw_QE="<<double(wrong_raw_work)<<" full_domain_V="<<double(v)<<'\n';
}

/** Default/metadata alone cannot grant actual Runtime source authority. */
struct UnknownPolicy final:Physical::Gravity::IGravityPolicy {
    mutable int calls=0;
    void add_sources_on_patch(std::vector<FluidVector>&,const FluidState&,const Grid&,double,void*) const override {
        ++calls;throw std::logic_error("unknown source was evaluated");
    }
};
void rejected_source_inputs() {
    Fixture f(0,false,.025,dispatch::TimeIntegratorId::Euler);f.bind_frame();
    const RollbackWitness saved(f);
    const auto ctor_reject=[&](const Physical::Gravity::IGravityPolicy* policy,driver::GravityStage::Qualification q) {
        bool rejected=false;
        try{driver::GravityStage bad(*f.runtime,policy,q);}
        catch(const std::invalid_argument&){rejected=true;}
        require(rejected&&f.observer->calls==0,"invalid source metadata/profile reached a real flux write");
        saved.unchanged();
    };
    UnknownPolicy unknown;
    ctor_reject(&unknown,driver::GravityStage::Qualification::NativeRzExternalCandidate);
    require(unknown.calls==0,"unknown source was executed during metadata refusal");
    ctor_reject(f.force.get(),static_cast<driver::GravityStage::Qualification>(255));
    Physical::Gravity::ExternalGravity wrong(0.,0.,std::nextafter(.025,1.));
    ctor_reject(&wrong,driver::GravityStage::Qualification::NativeRzExternalCandidate);
    bool missing=false;
    try{f.advance();}
    catch(const std::invalid_argument& e){missing=std::string(e.what()).find("actual prepared source contract")!=std::string::npos;}
    require(missing&&f.observer->calls==0,"finite descriptor without actual source journal reached patch production");
    saved.unchanged();
    f.bind_source();f.bind_frame();const RollbackWitness owned(f);
    Physical::Gravity::ExternalGravity foreign(0.,0.,.025);bool refused=false;
    try{f.advance(&foreign);}
    catch(const std::logic_error& e){refused=std::string(e.what()).find("actual frame")!=std::string::npos;}
    require(refused&&f.observer->calls==0&&!foreign.prepared_native_external(),
        "foreign equal-valued source policy borrowed another owner's stage");
    owned.unchanged();
    for(int component=0;component<2;++component) {
        Fixture axis(0,false,component?.025:0.,dispatch::TimeIntegratorId::Euler,false,false,0.,component?0.:.025);
        axis.bind_frame();const RollbackWitness at_axis(axis);bool nonregular=false;
        try{axis.bind_source();}
        catch(const std::invalid_argument& e){nonregular=std::string(e.what()).find("nonregular at the axis")!=std::string::npos;}
        require(nonregular&&axis.observer->calls==0,"nonregular constant native axis vector reached production");
        at_axis.unchanged();
    }
    std::cout<<"RZ_EXTERNAL_ACTUAL_PREFLIGHT unknown=1 qualification=1 config_one_ulp=1 no_frame=1 foreign_policy=1 axis_gr_gphi=1\n";
}

/** Cold variable-density actual source/EOS consumer followed by deliberate
 * discard before update. This is NOT positive cold finite-step acceptance.
 * Real analytic radial callbacks keep the density support physically smooth;
 * no ghost fallback, heat, producer replacement or false source token is used.
 */
void cold_consumption_discard() {
    Fixture f(0,false,.025,dispatch::TimeIntegratorId::Euler,true,true);f.bind_source();f.bind_frame();
    const auto& b=f.control.pool->GetBlock(f.control.tree->GetActiveBlocks().front());
    const int cell=b.grid.GetIndex(b.grid.Is(),b.grid.Js(),0);
    require(state::recover(b.fluid_state.get(cell)).status!=state::Status::valid,
        "cold source witness does not distinguish raw point from real native thermal closure");
    const RollbackWitness before(f);f.observer->abort_after_source=true;bool discarded=false;
    try{f.advance();}
    catch(const std::runtime_error& e){discarded=std::string(e.what())=="COLD_ACTUAL_SOURCE_OBSERVED";}
    require(discarded&&f.observer->calls==1&&f.observer->source_reference_seen,
        "cold source failed before genuine point EOS/source consumption or controlled discard");
    before.unchanged();
    const long double expected=dt*f.force->g_z*2.*pi*2.*
        (.875L*power_integral(1.,3.,2)+.25L*power_integral(1.,3.,4));
    close_budget(f.observer->body[2],expected,std::abs(expected),"cold independent exact torque reference failed");
    close_budget(f.observer->body[3],expected,std::abs(expected),"cold independent exact source work reference failed");
    require(f.source->external_source_budget()==std::array<long double,4>{},
        "discarded cold source published accepted body receipts");
    std::cout<<"RZ_EXTERNAL_ACTUAL_COLD_SOURCE real_point_eos=1 raw_point_invalid=1 controlled_discard=1 cold_step_acceptance=0\n";
}

/** Reject a genuine last selected RK EOS output after an accepted prefix.
 * Poisoning is an explicit engineering fault injection, not a physics oracle.
 * The actual macro owner must preserve the old accepted source budget exactly.
 */
void late_eos_rollback() {
    Fixture f(0,false,-.025,dispatch::TimeIntegratorId::Rk3);f.bind_source();f.bind_frame();
    f.advance();accepted_current(f);f.bind_frame();const RollbackWitness before(f);
    const int old_calls=f.observer->calls;const int expected=static_cast<int>(
        scheduler::supported_hydro_time_plan(f.hydro_method()).stages.size()*f.runtime->handles().size());
    f.poison=true;bool failed=false;
    double burn_advice=.75;const double old_advice=burn_advice;
    try {
        driver::NativeMacroStepAdvice advice(*f.runtime,*f.controller,burn_advice);
        (void)f.controller->calculate_next_dt(dt,burn_advice);burn_advice=1.e99;
        f.advance();advice.commit();
    } catch(const driver::NativeBoundaryAcceptanceError& e) {
        failed=e.diagnostic.phase==RzThermodynamics::AcceptancePhase::effective_thermal
            &&e.diagnostic.status==state::Status::unresolved_energy&&e.diagnostic.inertia_mapping_valid;
        if(!failed)throw;
        std::cout<<"RZ_EXTERNAL_ACTUAL_LATE_EOS phase=effective_thermal index="<<e.diagnostic.index
            <<" actual="<<e.what()<<'\n';
    }
    require(failed&&f.poisoned&&f.observer->calls==old_calls+expected,
        "external fault did not occur after real last RK source work and genuine boundary EOS");
    before.unchanged();require(bits(burn_advice,old_advice),"external late EOS changed accepted timestep advice");
    std::cout<<"RZ_EXTERNAL_ACTUAL_ROLLBACK accepted_prefix_retained=1 pending_prefix_discarded=1 all_slots_leases_ledger_bc_time=1\n";
}

void run() {
    for(int direction=0;direction<2;++direction)for(bool open:{false,true})
        for(double phi:{-.025,.025})for(auto method:{dispatch::TimeIntegratorId::Euler,
            dispatch::TimeIntegratorId::Rk2,dispatch::TimeIntegratorId::Rk3})
            matrix_case(direction,open,phi,method);
    physical_work();rejected_source_inputs();cold_consumption_discard();late_eos_rollback();
    application_guard_rejections();
}
} // namespace external_runtime_checks

/** Bounded actual angular Runtime entry for the existing gravity-stage lane.
 * Existing standalone EOS/identity and geometry matrices remain untouched.
 * Root qualifies this genuine negative and accepted half before promotion.
 */
void run_native_rz_runtime_boundary_contract() {
    angular_runtime_checks::run();
    external_runtime_checks::run();
}

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
