// Actual Runtime initialization/refresh, no timestep or scientific output.
#include "driver/runtime/DriverRuntime.h"
#include "driver/DriverUtils.h"
#include "driver/schedule/DriverControl.h"
#include "driver/stages/DriverStages.h"
#include "physics/eos/IdealGas.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>
static void require(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
int main() {
 try {
  for(int direction:{0,1})for(double inner:{0.,1.}) {
    SimConfig config{};
    config.grid.dim=2;config.grid.geometry="cylindrical";
    config.grid.nblockx1=direction==0?2:1;
    config.grid.nblockx2=direction==0?1:2;config.grid.nblockx3=0;
    config.grid.x1_min=inner;config.grid.x1_max=inner+2.;
    config.grid.x2_min=-1.;config.grid.x2_max=1.;
    config.grid.amr_max_blocks=32;config.amr.lrefinemin=0;config.amr.lrefinemax=1;
    amr::AMRControl control(32,2);
    control.tree->LoadLeafGrid(config,0,{1,1,1,1,0},
        {0,1,0,1,static_cast<std::uint32_t>(direction==0?1:0)},
        {0,0,1,1,static_cast<std::uint32_t>(direction==0?0:1)},{0,0,0,0,0});
    SpeciesManager species;species.add_species("a",1.,1.,1.4,3.);
    species.add_species("b",2.,1.,1.4,3.);
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
    IdealGas eos(1.4,species);
    config.numerics.cfl=.4;
    arch::driver::DriverStageWorkspace workspace;
    const double sound=std::sqrt(1.4*(100.-14.)*.4/2.);
    double expected=1.e99;
    for(int id:active) {
        const auto& g=control.pool->GetBlock(id).grid;
        expected=std::min(expected,.5*config.numerics.cfl/
            ((1.+sound)/g.dx1+(3.+sound)/g.dx2));
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
    config.physics.diffusion.use_diffusion=false;
    auto& bad=control.pool->GetBlock(active.front());
    const int bad_cell=bad.grid.GetIndex(bad.grid.Is(),bad.grid.Js(),0);
    const double saved=bad.fluid_state.rho[bad_cell];
    bad.fluid_state.rho[bad_cell]=std::numeric_limits<double>::quiet_NaN();
    bool invalid_density=false;
    try{(void)arch::driver::calculate_timestep_candidates(runtime,workspace,eos,nullptr);}
    catch(const std::runtime_error&){invalid_density=true;}
    require(invalid_density,"Driver ignored invalid active CFL density");
    bad.fluid_state.rho[bad_cell]=saved;
    bool device=false,regrid=false;
    try{(void)runtime.prepare_backend_bindings();}catch(const std::logic_error&){device=true;}
    try{(void)runtime.perform_regrid(0,0.);}catch(const std::logic_error&){regrid=true;}
    require(device&&regrid,"Unmigrated RZ consumer accepted");
    require(runtime.stage_context().ledger.inspect(
        {runtime.handles()[0],arch::state::StateSlot::Current}).interior.version==v,
        "Rejected Runtime consumer changed version");
    require(counters.step_count==0&&counters.t_current==0.,"Runtime fixture advanced time");
    std::cout<<"RZ_RUNTIME_HALO direction="<<direction<<" inner="<<inner
        <<" leaves="<<active.size()<<" version="<<v.value<<" PASS\n";
  }
  return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
