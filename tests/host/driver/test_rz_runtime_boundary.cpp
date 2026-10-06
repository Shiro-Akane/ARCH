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
            GridMetrics::GeometrySemantics::AxisymmetricRz).semantics
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
    bad.fluid_state.rho[bad_cell]=std::numeric_limits<double>::quiet_NaN();
    bool invalid_density=false;
    try{(void)arch::driver::calculate_timestep_candidates(runtime,workspace,eos,nullptr);}
    catch(const std::runtime_error&){invalid_density=true;}
    require(invalid_density,"Driver ignored invalid active CFL density");
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
