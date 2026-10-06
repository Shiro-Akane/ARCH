/**
 * @file test_rz_runtime_boundary.cpp
 * @brief Actual native Runtime EOS, boundary and RKL ownership witnesses.
 *
 * Workflow:
 * 1. Bind the real IdealGas and validate completed native boundary/exchange.
 * 2. Reject incomplete RKL ownership before slot copies or flux mutation.
 * 3. Exercise actual single/composite RKL1/2 publication and existing physical
 *    references with their original budgets. No scientific output is written.
 * A whole-domain cold rotating diffusion witness additionally requires native
 * user-BC and operator EOS consumers; these tests do not certify that pending
 * coupled path from a provisional mean-state precheck.
 */
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "driver/DriverUtils.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "driver/stages/DriverStages.h"
#include "physics/eos/IdealGas.h"
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
int main() {
 try {
  real_native_eos_boundary_gate();
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
