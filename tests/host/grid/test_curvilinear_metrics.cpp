/**
 * @file test_curvilinear_metrics.cpp
 * @brief Check shared geometry and source terms against analytic measures.
 *
 * The cases span Cartesian, cylindrical and spherical coordinates, including
 * thin shells, poles and radial-origin behavior.
 */
#include "driver/DriverUtils.h"
#include "numerics/integrator/GeometricSources.h"
#include "numerics/integrator/HydroSolverImpl.h"
#include "numerics/integrator/TimeIntegratorEuler.h"
#include "numerics/integrator/TimeIntegratorRK2.h"
#include "numerics/integrator/TimeIntegratorRK3.h"
#include "numerics/flux/FluxHLLC.h"
#include "math/geometry/CurvilinearMetricCases.h"
#include "math/geometry/RzMetricCases.h"
#include "math/geometry/ViscousGeometryCases.h"
#include "numerics/diffusion/DiffFlux.h"
#include "physics/eos/IdealGas.h"
#include "physics/diagnostics/VelocityDiagnostics.h"
#include <iostream>
#include <stdexcept>

namespace {
void close(double actual, double expected, const char* name) {
    if (!std::isfinite(actual) || std::abs(actual - expected) >
        2.e-12 * std::max(1.0, std::abs(expected)))
        throw std::runtime_error(name);
}
struct ConstantEos {
    double get_pressure(const FluidVector&, const double*) const { return 5.0; }
    double get_sound_speed(const FluidVector&, double, const double*) const { return 2.0; }
};
}


void test_rz_angular_measures() {
    using namespace GridMetrics::Rz;
    constexpr double pi=3.141592653589793238462643383279502884;
    // Independently integrated solid rotation rho=Omega=1, r=[1/2,1], dz=1:
    // J=2*pi*integral(r^3 dr)=15*pi/32; m_phi=J/W=45/56.
    const double v=CellVolume(.5,1.,1.);
    const double w=AngularMomentumMeasure(.5,1.,1.);
    close(v,3.*pi/4.,"RZ angular volume");
    close(w,7.*pi/12.,"RZ W measure");
    close(VolumeCentroidRadius(.5,1.),7./9.,"RZ V centroid");
    close(AngularReconstructionRadius(.5,1.),45./56.,"RZ W centroid");
    close(arch::state::rz_angular_integral(45./56.,w),15.*pi/32.,"RZ rigid rotation J");
    close(arch::state::rz_angular_density(45./56.,w,v),5./8.,"RZ derived ell");
    close(arch::state::rz_representative_azimuthal_velocity(45./56.,1.),
          45./56.,"RZ representative velocity");
    if(RadialTorqueMeasure(0.,1.)!=0.)
        throw std::runtime_error("RZ axis torque not exact zero");
    close(RadialTorqueMeasure(.5,1.),pi/2.,"RZ radial torque measure");
    close(AxialTorqueMeasure(.5,1.),7.*pi/12.,"RZ axial torque measure");
    for(double left : {0.,.5,1.e10}) {
        const double right=left+1.;
        const double mid=left+.5;
        const double parent=AngularMomentumMeasure(left,right,1.);
        const double children=AngularMomentumMeasure(left,mid,.5)
                             +AngularMomentumMeasure(mid,right,.5);
        close(2.*children,parent,"RZ W radial/axial partition");
        const double c=AngularReconstructionRadius(left,right);
        if(!(c>left && c<right))throw std::runtime_error("RZ W centroid outside cell");
    }
    // Frozen inadmissible parent: W weights 1:7, V weights 1:3.
    // This is the arithmetic reference, not an AMR transaction acceptance.
    const double parent_m=(1.-7.*16.)/8.;
    const double parent_e=(9./16.+3.*2049./16.)/4.;
    close(parent_m,-111./8.,"RZ frozen angular parent");
    close(parent_e-.5*parent_m*parent_m,-9./128.,"RZ frozen parent veto reference");
    std::cout<<"RZ_ANGULAR_MEASURES_PASS\\n";
}

void test_rz_host_hydro() {
    using namespace GridMetrics;
    SpeciesManager species;
    species.add_species("gas", 1., 1., 1.4, 3.);
    IdealGas eos(1.4, species);
    const auto rz = GeometrySemantics::AxisymmetricRz;
    for (double inner : {0., 1.}) for (double swirl : {0., 2.}) {
        // Nonzero constant swirl has no regular axis extension; its source
        // witness is restricted to the non-axis domain.
        if (inner == 0. && swirl != 0.) continue;
        Grid grid(amr::MAX_NG, inner, inner+1., -.5, .5, 0., 1.);
        grid.dim=2; grid.geometry="cylindrical"; grid.InitializeTopology();
        FluidState state, updated;
        const int size=grid.GetTotalSize();
        state.Preallocate(size); state.InitSpecies(1);
        updated.Preallocate(size); updated.InitSpecies(1);
        constexpr double rho=2., axial=3., pressure=5., dt=.001;
        const double energy=pressure/.4+.5*rho*(axial*axial+swirl*swirl);
        for (int cell=0;cell<size;++cell) {
            state.set(cell,{rho,0.,rho*axial,rho*swirl,energy});
            state.X(0,cell)=1.;
        }
        std::vector<FluidVector> delta(size),flux(size);
        std::vector<double> species_delta(size),species_flux(size);
        TimeIntegration::evaluate_all_dimensions<FluxHLLC<PCMReconstruction>>(
            nullptr,-1,state,eos,grid,dt,delta,species_delta,
            flux,species_flux,nullptr,0.,1.,true,rz);
        TimeIntegration::perform_stage_update(state,state,updated,
            delta,species_delta,grid,0.,1.,1.e-14,1.e-14,1.e10,rz);
        double max_error=0.;
        for (int j=grid.Js();j<grid.Je();++j) for(int i=grid.Is();i<grid.Ie();++i) {
            const int cell=grid.GetIndex(i,j,0);
            const double inverse_radius=2./(grid.GetFacePosL(i)+grid.GetFacePosR(i));
            const double expected=dt*rho*swirl*swirl*inverse_radius;
            close(delta[cell].mom_u,expected,"RZ Host Hydro centrifugal source");
            close(delta[cell].mom_v,0.,"RZ Host Hydro axial momentum");
            close(delta[cell].mom_w,0.,"RZ Host Hydro swirl momentum");
            close(delta[cell].rho,0.,"RZ Host Hydro density");
            close(delta[cell].eng,0.,"RZ Host Hydro energy");
            close(species_delta[cell],0.,"RZ Host Hydro species");
            close(updated.mom_u[cell],expected,"RZ Host Hydro RK update");
            close(updated.mom_v[cell],rho*axial,"RZ Host Hydro updated axial momentum");
            close(updated.X(0,cell),1.,"RZ Host Hydro updated composition");
            max_error=std::max(max_error,std::abs(delta[cell].mom_u-expected));
        }
        if (updated.stage_repairs.values[0]!=0.)
            throw std::runtime_error("RZ constant-state Hydro unexpectedly repaired");
        // Reject legacy gravity before resetting output or calling its owner.
        struct UnmigratedGravity : Physical::Gravity::IGravityPolicy {
            mutable int calls=0;
            void add_sources_on_patch(std::vector<FluidVector>&,const FluidState&,
                const Grid&,double,void*) const override { ++calls; }
        } unmigrated;
        delta[0].rho=123.;
        bool rejected=false;
        try {
            TimeIntegration::evaluate_all_dimensions<FluxHLLC<PCMReconstruction>>(
                nullptr,-1,state,eos,grid,dt,delta,species_delta,
                flux,species_flux,&unmigrated,0.,1.,true,rz);
        } catch (const std::invalid_argument& error) {
            rejected=std::string(error.what()).find("not migrated")!=std::string::npos;
        }
        if (!rejected || delta[0].rho!=123. || unmigrated.calls!=0)
            throw std::runtime_error("RZ unmigrated gravity changed Hydro output");
        // An intentional existing density-floor repair checks the *budget*
        // metric, not a physical scenario or a new choice of science floors.
        FluidState low, repaired;
        low.Preallocate(size); low.InitSpecies(0);
        repaired.Preallocate(size); repaired.InitSpecies(0);
        for(int cell=0;cell<size;++cell) low.set(cell,{.5,0.,0.,0.,100.});
        std::fill(delta.begin(),delta.end(),FluidVector{});
        std::vector<double> no_species;
        TimeIntegration::perform_stage_update(low,low,repaired,delta,no_species,
            grid,0.,1.,1.,1.e-14,1.e10,rz);
        const double volume=arch::constants::math::pi
            *((inner+1.)*(inner+1.)-inner*inner);
        close(repaired.stage_repairs.values[0],amr::BLOCK_NX*amr::BLOCK_NY,
            "RZ Hydro repair count");
        close(repaired.stage_repairs.values[1],volume,"RZ Hydro repaired full-ring volume");
        close(repaired.stage_repairs.values[2],.5*volume,"RZ Hydro repair mass measure");
        std::cout<<"RZ_HOST_HYDRO inner="<<inner<<" swirl="<<swirl
            <<" max_radial_error="<<max_error<<" repair_volume="
            <<repaired.stage_repairs.values[1]<<'\n';
    }
}


void test_rz_host_cfl() {
    using namespace GridMetrics;
    SpeciesManager species;
    species.add_species("gas",1.,1.,1.4,3.);
    IdealGas eos(1.4,species);
    constexpr double cfl=.4,rho=2.,pressure=5.;
    const double sound=std::sqrt(1.4*pressure/rho);
    for (double inner : {0.,1.}) {
        Grid grid(amr::MAX_NG,inner,inner+1.,-1.,1.,0.,1.);
        grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology();
        FluidState state;state.Preallocate(grid.GetTotalSize());state.InitSpecies(1);
        double zero_swirl_dt=0.;
        for (double swirl : {0.,2.}) {
            double reference=std::numeric_limits<double>::max();
            for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
                const int cell=grid.GetIndex(i,j,0);
                const double radius=grid.GetCellCenterX(i),z=grid.GetCellCenterY(j);
                const double radial=.1+.2*radius,axial=-.5+.3*z;
                state.set(cell,{rho,rho*radial,rho*axial,rho*swirl,
                    pressure/.4+.5*rho*(radial*radial+axial*axial+swirl*swirl)});
                state.X(0,cell)=1.;
                if(i>=grid.Is() && i<grid.Ie() && j>=grid.Js() && j<grid.Je()) {
                    // Independent two-face acoustic transport bound, dr != dz.
                    const double rate=(std::abs(radial)+sound)*amr::BLOCK_NX
                        +(std::abs(axial)+sound)*amr::BLOCK_NY/2.;
                    reference=std::min(reference,.5*cfl/rate);
                }
            }
            const double serial=adaptive_dt(state,eos,grid,cfl,false,
                GeometrySemantics::AxisymmetricRz);
            const double parallel=adaptive_dt(state,eos,grid,cfl,true,
                GeometrySemantics::AxisymmetricRz);
            close(serial,reference,"RZ Host CFL r/z physical transport bound");
            close(parallel,reference,"RZ Host parallel CFL bound");
            if(serial!=parallel)throw std::runtime_error("RZ CFL reduction schedule drift");
            if(swirl==0.)zero_swirl_dt=serial;
            else close(serial,zero_swirl_dt,"inactive phi entered RZ acoustic CFL");
            std::cout<<"RZ_HOST_CFL inner="<<inner<<" swirl="<<swirl
                <<" dt="<<serial<<" reference="<<reference
                <<" absolute_error="<<std::abs(serial-reference)<<'\n';
        }
        state.rho[grid.GetIndex(grid.Is(),grid.Js(),0)]=
            std::numeric_limits<double>::quiet_NaN();
        bool rejected=false;
        try { (void)adaptive_dt(state,eos,grid,cfl,false,GeometrySemantics::AxisymmetricRz); }
        catch(const std::runtime_error&) { rejected=true; }
        if(!rejected)throw std::runtime_error("RZ CFL accepted invalid active density");
    }
}


void test_rz_mixed_hydro_stage(int direction,double inner) {
    const auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    SpeciesManager species;
    species.add_species("gas0",1.,1.,1.4,3.);
    species.add_species("gas1",2.,1.,1.4,3.);
    IdealGas eos(1.4,species);
    Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> policy(eos,rz);
    const Numerics::IHydroSolver& hydro=policy;
    Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> legacy(eos);
    if(hydro.geometry_semantics()!=rz
        || legacy.geometry_semantics()!=GridMetrics::GeometrySemantics::Existing)
        throw std::runtime_error("Host Hydro type-erased chart identity");
    bool invalid_rejected=false;
    try {
        Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> invalid(
            eos,static_cast<GridMetrics::GeometrySemantics>(255));
    } catch(const std::invalid_argument&) { invalid_rejected=true; }
    if(!invalid_rejected)throw std::runtime_error("Unknown Hydro chart accepted");
    NumericsConfig numerics{};
    numerics.entropy_fix_coeff=0.;numerics.hll_roe_wave_speed=true;
    numerics.sml_rho=1.e-14;numerics.min_eint=1.e-14;numerics.max_eint=1.e10;
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
    const auto& active=control.tree->GetActiveBlocks();
    std::vector<amr::BlockHandle> handles;
    const FluidVector reference{2.,0.,6.,0.,21.5}; // p5, vz3, zero swirl.
    for(std::size_t n=0;n<active.size();++n) {
        handles.push_back({{4000+n},{97}});
        auto& block=control.pool->GetBlock(active[n]);
        block.fluid_state.InitSpecies(2);block.state_next.InitSpecies(2);
        for(int cell=0;cell<block.grid.GetTotalSize();++cell) {
            block.fluid_state.set(cell,reference);
            block.fluid_state.X(0,cell)=.6;block.fluid_state.X(1,cell)=.4;
        }
    }
    control.BindActiveHandles(handles);control.flux_register.EnsureSpecies(2);
    control.flux_register.Clear();
    BCHandler boundary(config,rz);
    for(int id:active) {
        auto& block=control.pool->GetBlock(id);
        boundary.apply(block.fluid_state,block.grid);
    }
    control.ghost_exchange.ExecuteExchange(control.pool,control.tree,2,
        &amr::Block::fluid_state,handles,amr::CoordinateSeamGeometry::RzAxisymmetric);
    constexpr double dt=.001;
    double max_delta=0.;
    for(int id:active) {
        auto& block=control.pool->GetBlock(id);
        const auto& g=block.grid;const int size=g.GetTotalSize();
        std::vector<FluidVector> delta(size);
        std::vector<double> ds(2*size);
        hydro.evaluate_patch(&control,id,block.fluid_state,g,dt,delta,ds,
            nullptr,numerics,1.);
        hydro.update_patch(block.fluid_state,block.fluid_state,
            block.state_next,delta,ds,g,0.,1.,numerics);
        for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
            const auto d=delta[g.GetIndex(i,j,0)];
            for(double v:{d.rho,d.mom_u,d.mom_v,d.mom_w,d.eng}) {
                close(v,0.,"RZ mixed Hydro uniform-state derivative");
                max_delta=std::max(max_delta,std::abs(v));
            }
        }
        if(block.state_next.stage_repairs.values[0]!=0.)
            throw std::runtime_error("RZ mixed Hydro manufactured repair");
    }
    const auto& topology=control.RequireFluxTopologyPlan(2,rz);
    if(topology.semantics!=rz)throw std::runtime_error("Hydro registered legacy AMR chart");
    double max_register=0.;
    for(int id:active)for(int face=0;face<4;++face) {
        if(!control.flux_register.HasData(id,face))continue;
        const int count=face/2==0?amr::BLOCK_NY:amr::BLOCK_NX;
        for(int cell=0;cell<count;++cell) {
            const auto f=control.flux_register.GetSummedFlux(id,face,cell);
            for(double v:{f.rho,f.mom_u,f.mom_v,f.mom_w,f.eng}) {
                close(v,0.,"RZ mixed Hydro constant face-register balance");
                max_register=std::max(max_register,std::abs(v));
            }
        }
    }
    for(int id:active) {
        auto& block=control.pool->GetBlock(id);
        boundary.apply(block.state_next,block.grid);
    }
    control.ghost_exchange.ExecuteExchange(control.pool,control.tree,2,
        &amr::Block::state_next,handles,amr::CoordinateSeamGeometry::RzAxisymmetric);
    control.ApplyReflux(dt,&amr::Block::state_next,rz);
    double max_state_error=0.;
    for(int id:active) {
        const auto& block=control.pool->GetBlock(id);const auto& g=block.grid;
        for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
            const int cell=g.GetIndex(i,j,0);const auto u=block.state_next.get(cell);
            const double errors[]{u.rho-reference.rho,u.mom_u-reference.mom_u,
                u.mom_v-reference.mom_v,u.mom_w-reference.mom_w,u.eng-reference.eng,
                block.state_next.X(0,cell)-.6,block.state_next.X(1,cell)-.4};
            for(double v:errors) {
                close(v,0.,"RZ mixed Hydro stage/reflux state drift");
                max_state_error=std::max(max_state_error,std::abs(v));
            }
        }
    }
    std::cout<<"RZ_MIXED_HYDRO direction="<<direction<<" inner="<<inner
        <<" max_delta="<<max_delta<<" max_register="<<max_register
        <<" max_state_error="<<max_state_error<<'\n';
}

template<typename Solver>
void test_rz_scheduled_hydro(int direction,double inner) {
    const auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    SpeciesManager species;
    species.add_species("gas0",1.,1.,1.4,3.);
    species.add_species("gas1",2.,1.,1.4,3.);
    IdealGas eos(1.4,species);
    Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> policy(eos,rz);
    const Numerics::IHydroSolver& hydro=policy;
    Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> legacy(eos);
    if(hydro.geometry_semantics()!=rz
        || legacy.geometry_semantics()!=GridMetrics::GeometrySemantics::Existing)
        throw std::runtime_error("Host Hydro type-erased chart identity");
    bool invalid_rejected=false;
    try {
        Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> invalid(
            eos,static_cast<GridMetrics::GeometrySemantics>(255));
    } catch(const std::invalid_argument&) { invalid_rejected=true; }
    if(!invalid_rejected)throw std::runtime_error("Unknown Hydro chart accepted");
    NumericsConfig numerics{};
    numerics.entropy_fix_coeff=0.;numerics.hll_roe_wave_speed=true;
    numerics.sml_rho=1.e-14;numerics.min_eint=1.e-14;numerics.max_eint=1.e10;
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
    const auto& active=control.tree->GetActiveBlocks();
    std::vector<amr::BlockHandle> handles;
    const FluidVector reference{2.,0.,6.,0.,21.5}; // p5, vz3, zero swirl.
    for(std::size_t n=0;n<active.size();++n) {
        handles.push_back({{4000+n},{97}});
        auto& block=control.pool->GetBlock(active[n]);
        block.fluid_state.InitSpecies(2);block.state_next.InitSpecies(2);block.state_scratch.InitSpecies(2);
        for(int cell=0;cell<block.grid.GetTotalSize();++cell) {
            block.fluid_state.set(cell,reference);
            block.fluid_state.X(0,cell)=.6;block.fluid_state.X(1,cell)=.4;
        }
    }
    control.BindActiveHandles(handles);control.flux_register.EnsureSpecies(2);
    control.flux_register.Clear();
    BCHandler boundary(config,rz);
    for(int id:active) {
        auto& block=control.pool->GetBlock(id);
        boundary.apply(block.fluid_state,block.grid);
    }
    control.ghost_exchange.ExecuteExchange(control.pool,control.tree,2,
        &amr::Block::fluid_state,handles,amr::CoordinateSeamGeometry::RzAxisymmetric);

    using namespace arch::state;
    using namespace arch::scheduler;
    StateResidencyLedger ledger({97});
    for(auto handle:handles) {
        ledger.register_block(handle,{1},{1,CompletionState::Complete});
        ledger.publish_ghost({handle,StateSlot::Current},ExecutionSide::Host,
            {1},{2,CompletionState::Complete});
    }
    MonotonicSchedulerClock clock(2,1);
    StageExecutionContext context{ExecutionSide::Host,ledger,clock};
    ScopedStageBinding scope(context,handles);
    // Mismatch must fail before advancing the clock/ledger. Use the actual
    // scheduled entry, not only its helper.
    BCHandler wrong_boundary(config);
    control.flux_register.AddFineFlux(active.front(),0,0,{123.,0.,0.,0.,0.},1.);
    bool mismatch=false;
    try { Solver::solve(control,.001,wrong_boundary,nullptr,&hydro,numerics); }
    catch(const std::invalid_argument&) { mismatch=true; }
    if(!mismatch || clock.last_version()!=1 || clock.last_token()!=2)
        throw std::runtime_error("RZ scheduler mismatch mutated publication");
    for(auto h:handles)
        if(ledger.inspect({h,StateSlot::Current}).interior.version!=StateVersion{1})
            throw std::runtime_error("RZ mismatch changed state ledger");
    if(!control.flux_register.HasData(active.front(),0)
        ||control.flux_register.GetSummedFlux(active.front(),0,0).rho!=123.)
        throw std::runtime_error("RZ mismatch cleared accumulated flux");
    Solver::solve(control,.001,boundary,nullptr,&hydro,numerics);
    const std::uint64_t stages=std::is_same_v<Solver,SolverEuler>?1:
        (std::is_same_v<Solver,SolverRK2>?2:3);
    if(clock.last_version()!=1+stages+1)
        throw std::runtime_error("RZ scheduler publication count");
    double error=0.;
    for(std::size_t n=0;n<active.size();++n) {
        const auto& block=control.pool->GetBlock(active[n]);
        const auto& g=block.grid;
        const auto state=ledger.inspect({handles[n],StateSlot::Current});
        if(state.interior.version.value!=clock.last_version()
            ||state.interior.residency!=StateResidency::HostValid
            ||state.ghost.residency!=StateResidency::Invalid)
            throw std::runtime_error("RZ final reflux ledger identity");
        for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
            const int cell=g.GetIndex(i,j,0);
            const auto u=block.fluid_state.get(cell);
            for(double v:{u.rho-reference.rho,u.mom_u-reference.mom_u,
                u.mom_v-reference.mom_v,u.mom_w-reference.mom_w,u.eng-reference.eng,
                block.fluid_state.X(0,cell)-.6,block.fluid_state.X(1,cell)-.4}) {
                close(v,0.,"RZ scheduled mixed-AMR constant-state drift");
                error=std::max(error,std::abs(v));
            }
        }
    }
    if(control.RequireFluxTopologyPlan(2,rz).semantics!=rz)
        throw std::runtime_error("Scheduled Hydro reflux chart mismatch");
    std::cout<<"RZ_SCHEDULED_HYDRO method="<<Solver::name()
        <<" direction="<<direction<<" inner="<<inner<<" max_state_error="<<error
        <<" version="<<clock.last_version()<<'\n';
}


void test_rz_native_coordinates()
{
    using GridMetrics::GeometrySemantics;
    constexpr auto rz=GeometrySemantics::AxisymmetricRz;
    for (const auto [radius,z] : std::array<std::pair<double,double>,5>{
        {{0.,-4.},{0.,4.},{3.,-4.},{3.,4.},{1.e150,-1.e150}}}) {
        const auto p=Grid::PhysicalCoordsFromNative(2,"cylindrical",radius,z,0.,rz);
        if(p.x!=radius || p.y!=0. || p.z!=z || p.r_cy!=radius || p.z_cy!=z ||
           p.phi_cy!=0. || p.phi!=0. || p.r!=std::hypot(radius,z) ||
           p.theta!=std::atan2(radius,z))
            throw std::runtime_error("RZ native coordinate expansion mismatch");
    }
    Grid grid(amr::MAX_NG,0.,2.,-10.,10.,0.,1.);
    grid.dim=2;grid.geometry="cylindrical";
    grid.InitializeTopology(rz); // z length >2pi is valid; not an angle.
    if(grid.GetAxisNames(rz)!=std::vector<std::string>{"r_cy","z_cy"})
        throw std::runtime_error("RZ native axes mismatch");
    for(int j=grid.Js();j<grid.Je();++j) for(int i=grid.Is();i<grid.Ie();++i) {
        const auto p=grid.GetPhysicalCoords(i,j,grid.Ks(),rz);
        if(p.r_cy!=grid.GetCellCenterX(i) || p.z_cy!=grid.GetCellCenterY(j) ||
           p.z!=p.z_cy || p.r!=std::hypot(p.r_cy,p.z_cy))
            throw std::runtime_error("RZ native cell center mismatch");
    }
    bool rejected=false;
    try {grid.InitializeTopology();} catch(const std::invalid_argument&) {rejected=true;}
    if(!rejected) throw std::runtime_error("legacy polar angle guard was removed");
    // Exact legacy coordinate witnesses and 3D cylindrical mapping.
    const auto old=Grid::PhysicalCoordsFromNative(2,"cylindrical",3.,.5);
    if(old.z_cy!=0. || old.phi_cy!=.5 || old.x!=3.*std::cos(.5) ||
       old.y!=3.*std::sin(.5))
        throw std::runtime_error("legacy cylindrical polar mapping changed");
    const auto three=Grid::PhysicalCoordsFromNative(3,"cylindrical",3.,-4.,.5);
    if(three.r_cy!=3. || three.z_cy!=-4. || three.phi_cy!=.5 || three.r!=5.)
        throw std::runtime_error("3D cylindrical mapping changed");
    for(const auto [dim,geometry] : std::array<std::pair<int,const char*>,4>{
        {{1,"cylindrical"},{3,"cylindrical"},{2,"cartesian"},{2,"spherical"}}}) {
        rejected=false;
        try {(void)Grid::PhysicalCoordsFromNative(dim,geometry,1.,2.,0.,rz);}
        catch(const std::invalid_argument&) {rejected=true;}
        if(!rejected) throw std::runtime_error("invalid RZ coordinate profile accepted");
    }
    std::cout<<"RZ_NATIVE_COORDINATES physical/native/axes/domain/legacy PASS\n";
}
int main()
{
    test_rz_angular_measures();
    test_rz_native_coordinates();
    for(int direction:{0,1})for(double inner:{0.,1.}) {
        test_rz_scheduled_hydro<SolverEuler>(direction,inner);
        test_rz_scheduled_hydro<SolverRK2>(direction,inner);
        test_rz_scheduled_hydro<SolverRK3>(direction,inner);
    }
    for(int direction:{0,1})for(double inner:{0.,1.})
        test_rz_mixed_hydro_stage(direction,inner);
    test_rz_host_cfl();
    test_rz_host_hydro();
    using namespace GridMetrics;
    const double conditioning = CurvilinearMetricCases::conditioning_error();
    if (!std::isfinite(conditioning) || conditioning > 2.e-12)
        throw std::runtime_error("independent thin-shell/polar measures");
    std::cout << "INDEPENDENT_METRIC_MAX_RELATIVE_ERROR=" << conditioning << '\n';
    // Same existing arithmetic metric gate; no new production science budget.
    const double rz_conditioning = RzMetricCases::conditioning_error();
    if (!std::isfinite(rz_conditioning) || rz_conditioning > 2.e-12)
        throw std::runtime_error("independent full-rotation RZ measures");
    std::cout << "RZ_METRIC_MAX_RELATIVE_ERROR=" << rz_conditioning << '\n';
    for (const auto& sample : RzMetricCases::cases) {
        const double dr = sample.upper-sample.lower;
        if (Rz::PhysicalSpacing(0,dr,sample.dz)!=dr
            || Rz::PhysicalSpacing(1,dr,sample.dz)!=sample.dz)
            throw std::runtime_error("RZ physical spacing changed");
    }
    for (double left : {0.,1.,4.}) {
        const double right=left+.25, dz=.5;
        const double volume=Rz::CellVolume(left,right,dz);
        const double radial_lower=Rz::RadialFaceArea(left,dz);
        const double radial_upper=Rz::RadialFaceArea(right,dz);
        const double axial=Rz::AxialFaceArea(left,right);
        // Independent div(r e_r + z e_z)=3; full rotating face fluxes.
        close((right*radial_upper-left*radial_lower+dz*axial)/volume,
              3.,"RZ linear vector divergence");
        close((radial_upper-radial_lower)/volume,
              2./(left+right),"RZ volume-average inverse radius");
        const double middle=.5*(left+right);
        const double fine=Rz::CellVolume(left,middle,dz/2.)
            +Rz::CellVolume(middle,right,dz/2.);
        close(2.*fine/volume,1.,"RZ finite source partition");
        close((Rz::AxialFaceArea(left,middle)+Rz::AxialFaceArea(middle,right))/axial,
              1.,"RZ coarse/fine axial area sum");
    }
    for (double left : {0.,1.,4.}) {
        const double right=left+.25, dz=.5, dt=.125;
        const double inv=2./(left+right);
        close(Rz::InverseRadiusVolumeAverage(left,right),inv,"RZ axis-cell inverse radius");
        const FluidVector rest{2.,0.,0.,0.,20.};
        FluidVector rest_delta{};
        TimeIntegration::add_rz_geometric_source_cell(rest,nullptr,ConstantEos{},
            left,right,dt,rest_delta);
        const double pressure_flux=dt*5.*(Rz::RadialFaceArea(right,dz)
            -Rz::RadialFaceArea(left,dz))/Rz::CellVolume(left,right,dz);
        close(rest_delta.mom_u-pressure_flux,0.,"RZ constant-pressure axis balance");
        if (rest_delta.rho!=0. || rest_delta.eng!=0.
            || rest_delta.mom_v!=0. || rest_delta.mom_w!=0.)
            throw std::runtime_error("RZ rest source changed unrelated component");
        // Distinct z and phi velocities catch accidental polar/axis mapping.
        const FluidVector moving{2.,6.,14.,10.,20.};
        FluidVector delta{17.,19.,23.,29.,31.};
        TimeIntegration::add_rz_geometric_source_cell(moving,nullptr,ConstantEos{},
            left,right,dt,delta);
        close(delta.mom_u-19.,dt*(2.*25.+5.)*inv,"RZ centrifugal source");
        close(delta.mom_w-29.,-dt*2.*3.*5.*inv,"RZ phi curvature source");
        if (delta.mom_v!=23. || delta.rho!=17. || delta.eng!=31.)
            throw std::runtime_error("RZ source changed z/mass/energy");
        auto translated=moving;
        translated.mom_v=-1000.;
        FluidVector other{17.,19.,23.,29.,31.};
        TimeIntegration::add_rz_geometric_source_cell(translated,nullptr,ConstantEos{},
            left,right,dt,other);
        if (delta.mom_u!=other.mom_u || delta.mom_w!=other.mom_w)
            throw std::runtime_error("RZ geometric source depends on axial velocity");
        GeometryView full{};
        full.geometry=Geometry::Cylindrical;full.dim=3;full.x1_min=left;
        full.dx1=right-left;full.dx2=dz;full.dx3=.3;
        FluidVector existing{17.,19.,23.,29.,31.};
        TimeIntegration::add_geometric_source_cell(moving,nullptr,ConstantEos{},
            full,0,0,dt,existing);
        if (existing.mom_u!=delta.mom_u || existing.mom_w!=delta.mom_w
            || existing.mom_v!=delta.mom_v)
            throw std::runtime_error("RZ and full cylindrical source diverged");
    }
    // Preserve the pre-extraction polar/full cylindrical formulas exactly.
    for (int dim : {1,2,3}) {
        GeometryView grid{};
        grid.geometry=Geometry::Cylindrical;grid.dim=dim;
        grid.x1_min=1.;grid.dx1=.25;grid.dx2=.5;grid.dx3=.3;
        const FluidVector u{2.,6.,14.,10.,20.};
        FluidVector actual{17.,19.,23.,29.,31.}, expected=actual;
        const double rho=u.rho,vr=u.mom_u/rho;
        const double vp=dim==2?u.mom_v/rho:(dim==3?u.mom_w/rho:0.);
        const double inv=(1.25-1.)/(.5*(1.25-1.)*(1.25+1.));
        expected.mom_u += .125*(rho*vp*vp+5.)*inv;
        if (dim==2) expected.mom_v += .125*(-rho*vr*(u.mom_v/rho))*inv;
        if (dim==3) expected.mom_w += .125*(-rho*vr*(u.mom_w/rho))*inv;
        TimeIntegration::add_geometric_source_cell(u,nullptr,ConstantEos{},
            grid,0,0,.125,actual);
        if (actual.rho!=expected.rho || actual.eng!=expected.eng
            || actual.mom_u!=expected.mom_u || actual.mom_v!=expected.mom_v
            || actual.mom_w!=expected.mom_w)
            throw std::runtime_error("Legacy cylindrical source formula changed");
    }
    for (double left : {0.,1.,4.}) {
        const auto legacy=make_geometry_view(Geometry::Cylindrical,2,
            {left,-2.,0.},{.25,.5,0.});
        const auto rz=make_rz_geometry_view(legacy);
        if (legacy.semantics!=GeometrySemantics::Existing)
            throw std::runtime_error("RZ conversion changed input view");
        if (CellVolume(rz,0,0,0)!=Rz::CellVolume(left,left+.25,.5)
            || FaceArea(rz,0,0,0,0,false)!=Rz::RadialFaceArea(left,.5)
            || FaceArea(rz,0,0,0,0,true)!=Rz::RadialFaceArea(left+.25,.5)
            || FaceArea(rz,1,0,0,0,false)!=Rz::AxialFaceArea(left,left+.25)
            || FaceArea(rz,1,0,0,0,true)!=Rz::AxialFaceArea(left,left+.25)
            || PhysicalSpacing(rz,0,0,0)!=.25 || PhysicalSpacing(rz,1,0,0)!=.5)
            throw std::runtime_error("RZ view did not consume shared full-ring measures");
        if (PhysicalPosition(rz,{left+.125,-1.75,0.})
                !=std::array<double,3>{left+.125,0.,-1.75})
            throw std::runtime_error("RZ representative position lost axial coordinate");
        FluidVector direct{},through_view{};
        const FluidVector u{2.,6.,14.,10.,100.};
        TimeIntegration::add_rz_geometric_source_cell(u,nullptr,ConstantEos{},
            left,left+.25,.125,direct);
        TimeIntegration::add_geometric_source_cell(u,nullptr,ConstantEos{},
            rz,0,0,.125,through_view);
        if (direct.mom_u!=through_view.mom_u || direct.mom_w!=through_view.mom_w
            || through_view.mom_v!=0. || through_view.rho!=0. || through_view.eng!=0.)
            throw std::runtime_error("RZ shared view source mapping drifted");
    }
    for (Geometry kind : {Geometry::Cartesian,Geometry::Spherical}) {
        bool rejected=false;
        try { (void)make_rz_geometry_view(make_geometry_view(kind,2,{0.,0.,0.},{1.,1.,0.})); }
        catch (const std::invalid_argument&) {rejected=true;}
        if (!rejected) throw std::runtime_error("RZ view accepted unrelated geometry");
    }
    for (int dimension : {1,3}) {
        bool rejected=false;
        try { (void)make_rz_geometry_view(make_geometry_view(
            Geometry::Cylindrical,dimension,{0.,0.,0.},{1.,1.,1.})); }
        catch (const std::invalid_argument&) {rejected=true;}
        if (!rejected) throw std::runtime_error("RZ view accepted unsupported dimension");
    }
    // Independent axisymmetric polynomial witness:
    // vr=(a+b*z)*r, vz=c*r*r+d*z, vphi=(w+q*z)*r.
    // div=2(a+b*z)+d; curl=(-q*r, (b-2c)*r, 2(w+q*z)).
    for (double left : {0.,1.,4.}) {
        auto grid=make_rz_geometry_view(make_geometry_view(
            Geometry::Cylindrical,2,{left,-1.,0.},{.25,.5,0.}));
        grid.ng=1;grid.stride_y=5;grid.stride_z=25;grid.total_size=25;
        std::vector<double> vr(25),vz(25),vp(25);
        const double a=.5,b=.25,c=-.75,d=1.5,w=2.,q=.5;
        for (int j=0;j<5;++j) for (int i=0;i<5;++i) {
            const int cell=grid.GetIndex(i,j);
            const double radius=grid.GetCellCenterX(i),z=grid.GetCellCenterY(j);
            vr[cell]=(a+b*z)*radius;vz[cell]=c*radius*radius+d*z;vp[cell]=(w+q*z)*radius;
        }
        for (int j : {1,2,3}) for (int i : {1,2,3}) {
            const double radius=grid.GetCellCenterX(i),z=grid.GetCellCenterY(j);
            const auto value=VelocityDiagnostics::evaluate(grid,vr,vz,vp,i,j,0);
            close(value.divergence,2.*(a+b*z)+d,"RZ analytic finite-volume divergence");
            close(value.vorticity,std::sqrt(q*q*radius*radius+(b-2.*c)*(b-2.*c)*radius*radius
                +4.*(w+q*z)*(w+q*z)),"RZ analytic axisymmetric curl");
        }
    }
    const double pi = arch::constants::math::pi;
    for (Geometry geometry : {Geometry::Cartesian, Geometry::Cylindrical, Geometry::Spherical})
    for (int dimension : {1, 2, 3})
    for (double radius : {0.0, 1.0, 10.0})
    for (double theta_left : {0.0, pi/3, pi/2, 5*pi/6}) {
        GeometryView grid{};
        grid.geometry = geometry; grid.dim = dimension;
        grid.dx1 = .25; grid.dx2 = pi/6; grid.dx3 = .2;
        grid.x1_min = radius; grid.x2_min = theta_left;
        const double theta = theta_left + pi/12;
        const double r = radius + .125;
        double width_y = grid.dx2, width_z = grid.dx3;
        if (geometry != Geometry::Cartesian) {
            if (dimension == 2 || geometry == Geometry::Spherical) width_y *= r;
            if (dimension == 3) width_z *= r;
            if (dimension == 3 && geometry == Geometry::Spherical) width_z *= std::sin(theta);
        }
        close(PhysicalSpacing(grid, 0, 0, 0), .25, "radial length");
        if (dimension >= 2) close(PhysicalSpacing(grid, 1, 0, 0), width_y, "second length");
        if (dimension == 3) close(PhysicalSpacing(grid, 2, 0, 0), width_z, "third length");
        const FluidVector state{1, 0, 0, 0, 20};
        double inverse_dt = 2/.25;
        if (dimension >= 2) inverse_dt += 2/width_y;
        if (dimension == 3) inverse_dt += 2/width_z;
        close(evaluate_cfl_cell_dt(state, nullptr, ConstantEos{}, grid, 0, 0),
              0.5/inverse_dt, "two-face convex CFL");

        // Independent rest-state balance: fluxes have constant pressure on
        // normal momentum faces, no mass/energy flux. It must cancel sources.
        FluidVector source{};
        TimeIntegration::add_geometric_source_cell(state, nullptr, ConstantEos{}, grid, 0, 0, 1, source);
        const double sources[]{source.mom_u, source.mom_v, source.mom_w};
        for (int direction = 0; direction < dimension; ++direction) {
            const double divergence = 5.0 * (FaceArea(grid, direction, 0, 0, 0, true)
                - FaceArea(grid, direction, 0, 0, 0, false)) / CellVolume(grid, 0, 0, 0);
            close(sources[direction] - divergence, 0, "constant-pressure equilibrium");
        }
        if (geometry == Geometry::Spherical && dimension == 3) {
            const double integral_r = (std::pow(radius + .25, 2) - radius * radius) / 2;
            close(FaceArea(grid, 1, 0, 0, 0, true), integral_r * std::sin(theta_left + pi/6) * .2,
                  "theta physical area");
            close(FaceArea(grid, 2, 0, 0, 0, false), integral_r * pi/6,
                  "phi physical area");
        }
    }
    std::cout << "CURVILINEAR_METRICS_PASS\n";
    SpeciesManager species;
    species.add_species("gas", 1., 1., 1.4, 3.);
    IdealGas eos(1.4, species);
    SimConfig config{};
    config.physics.diffusion.use_diffusion = true;
    config.physics.diffusion.use_viscous_diffusion = true;
    config.physics.diffusion.nu_visc = ViscousGeometryCases::viscosity;
    const auto evaluate = [&](const FluidState& state, const Grid& grid) {
        FluidState delta;
        delta.Preallocate(grid.GetTotalSize()); delta.InitSpecies(1);
        DiffFlux::compute_diffusion_operator(state, delta, eos, grid, config);
        ViscousGeometryCases::Evaluation result{};
        result.derivative.resize(grid.GetTotalSize());
        for (int cell=0; cell<grid.GetTotalSize(); ++cell) result.derivative[cell] = delta.get(cell);
        result.raw_dt = DiffFlux::adaptive_dt_diff(state, eos, grid, config, 1.);
        return result;
    };
    // Independent Cartesian vector-Laplacian witness for axisymmetric flow:
    // vr=r, vz=3z, vphi=2r => Cartesian v=(x-2y,2x+y,3z).
    // Momentum Laplacian is zero and work divergence is mu*(1+4+4+1+9)=19mu.
    for (double left : {0.,1.,4.}) {
        auto grid=make_rz_geometry_view(make_geometry_view(
            Geometry::Cylindrical,2,{left,-1.,0.},{.25,.5,0.}));
        grid.ng=1;grid.stride_y=3;grid.stride_z=9;grid.total_size=9;
        std::vector<FluidVector> states(9);
        for (int j=0;j<3;++j) for (int i=0;i<3;++i) {
            const double r=grid.GetCellCenterX(i),z=grid.GetCellCenterY(j);
            states[grid.GetIndex(i,j)]={2.,2.*r,6.*z,4.*r,1000.};
        }
        const auto cfg=DiffFlux::make_diffusion_config_view(config);
        DiffFlux::DiffusionCoefficients coefficients{};
        coefficients.nu_visc=ViscousGeometryCases::viscosity;
        const double composition[]{1.};
        const int cell=grid.GetIndex(1,1);
        FluidVector delta{};
        int neighbour_reads=0;
        const auto status=DiffFlux::evaluate_geometric_diffusion_cell(
            states[cell],composition,eos,species.get_host_view(),cfg,grid,1,1,0,1.,
            nullptr,nullptr,delta,[&](int index) {++neighbour_reads;return states.at(index);});
        if (!status.valid || !status.active || neighbour_reads!=0)
            throw std::runtime_error("RZ viscous source accessed inactive phi neighbour");
        const double r=grid.GetCellCenterX(1),inv=2./(left+left+.25);
        close(delta.mom_u,-2.*coefficients.nu_visc*inv,"RZ radial viscous connection");
        close(delta.mom_w,-4.*coefficients.nu_visc*inv,"RZ swirl viscous connection");
        if (delta.mom_v!=0. || delta.rho!=0. || delta.eng!=0.)
            throw std::runtime_error("RZ viscous source changed z/mass/energy");
        close(DiffFlux::viscous_source_stability_rate(coefficients.nu_visc,grid,1,1),
            coefficients.nu_visc*inv/r,"RZ unresolved phi row bound");
        for (int direction=0;direction<2;++direction) {
            const int stride=direction==0?1:grid.stride_y;
            FluidVector flux[2];
            for (int side=0;side<2;++side) {
                const auto& a=states[cell+(side?0:-stride)];
                const auto& b=states[cell+(side?stride:0)];
                DiffFlux::assemble_diffusion_face_flux(a,b,1.,1.,2.,
                    composition,composition,1,PhysicalSpacing(grid,direction,1,1),
                    1.,coefficients,cfg,flux[side],nullptr,0,
                    DiffFlux::viscous_basis_rotation(grid,direction,1,1));
            }
            delta=delta-(flux[1]*FaceArea(grid,direction,1,1,0,true)
                -flux[0]*FaceArea(grid,direction,1,1,0,false))/CellVolume(grid,1,1,0);
        }
        close(delta.mom_u,0.,"RZ radial linear-vector Laplacian");
        close(delta.mom_v,0.,"RZ axial linear-vector Laplacian");
        close(delta.mom_w,0.,"RZ swirl linear-vector Laplacian");
        close(delta.eng,19.*2.*coefficients.nu_visc,"RZ conservative viscous work flux");
        if (delta.rho!=0.) throw std::runtime_error("RZ viscosity changed mass");
    }
    // Complete Host operator with native padded storage and explicit RZ chart.
    for (double left : {0.,1.}) {
        Grid grid(amr::MAX_NG,left,left+1.,-.5,.5,0.,1.);
        grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology();
        FluidState state,delta;
        state.Preallocate(grid.GetTotalSize());state.InitSpecies(1);
        delta.Preallocate(grid.GetTotalSize());delta.InitSpecies(1);
        for(int j=0;j<grid.GetTotalY();++j) for(int i=0;i<grid.GetTotalX();++i) {
            const int cell=grid.GetIndex(i,j,0);
            const double r=grid.GetCellCenterX(i),z=grid.GetCellCenterY(j);
            state.set(cell,{2.,2.*r,6.*z,4.*r,1000.});state.X(0,cell)=1.;
        }
        DiffFlux::compute_diffusion_operator(state,delta,eos,grid,config,
            GeometrySemantics::AxisymmetricRz);
        for(int j=grid.Js();j<grid.Je();++j) for(int i=grid.Is();i<grid.Ie();++i) {
            const int cell=grid.GetIndex(i,j,0);
            close(delta.mom_u[cell],0.,"RZ Host radial linear operator");
            close(delta.mom_v[cell],0.,"RZ Host axial linear operator");
            close(delta.mom_w[cell],0.,"RZ Host swirl linear operator");
            close(delta.eng[cell],19.*2.*ViscousGeometryCases::viscosity,
                "RZ Host conservative work divergence");
            if(delta.rho[cell]!=0. || delta.X(0,cell)!=0.)
                throw std::runtime_error("RZ Host viscosity changed mass/species");
        }
        const double dt=DiffFlux::adaptive_dt_diff(state,eos,grid,config,1.,
            GeometrySemantics::AxisymmetricRz);
        // Constant nu: radial/axial face sum is 2nu/dr²+2nu/dz²,
        // and the strongest unresolved phi source is at the first radial cell.
        const double center=grid.GetCellCenterX(grid.Is());
        const double inv=2./(2.*left+grid.dx1);
        const double rate=2.*ViscousGeometryCases::viscosity
            *(1./(grid.dx1*grid.dx1)+1./(grid.dx2*grid.dx2))
            +ViscousGeometryCases::viscosity*inv/center;
        close(dt,1./rate,"RZ Host explicit diffusion limit");
    }
    // Variable mu=nu*rho, rho=2+alpha*r. Independent cell-volume
    // average of Cartesian div(mu*v.grad(v)) is nu*(38+24alpha*<r>).
    double previous_rz_work_error=0.;
    for(double h : {.05,.025,.0125}) {
        const double lower=.5-(amr::BLOCK_NX/2+.5)*h;
        Grid grid(amr::MAX_NG,lower,lower+amr::BLOCK_NX*h,
            -.5,-.5+amr::BLOCK_NY*h,0.,1.);
        grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology();
        FluidState state,delta;
        state.Preallocate(grid.GetTotalSize());state.InitSpecies(1);
        delta.Preallocate(grid.GetTotalSize());delta.InitSpecies(1);
        constexpr double alpha=.1;
        for(int j=0;j<grid.GetTotalY();++j) for(int i=0;i<grid.GetTotalX();++i) {
            const int cell=grid.GetIndex(i,j,0);
            const double r=grid.GetCellCenterX(i),z=grid.GetCellCenterY(j),rho=2.+alpha*r;
            state.set(cell,{rho,rho*r,rho*3.*z,rho*2.*r,rho*1000.});
            state.X(0,cell)=1.;
        }
        DiffFlux::compute_diffusion_operator(state,delta,eos,grid,config,
            GeometrySemantics::AxisymmetricRz);
        const int i=grid.Is()+amr::BLOCK_NX/2,j=grid.Js()+amr::BLOCK_NY/2;
        const int cell=grid.GetIndex(i,j,0);
        const double lo=grid.GetFacePosL(i),hi=grid.GetFacePosR(i);
        const double average_r=(2./3.)*(hi*hi+hi*lo+lo*lo)/(hi+lo);
        const double nu=ViscousGeometryCases::viscosity;
        close(delta.mom_u[cell],nu*alpha,"RZ variable-mu radial derivative");
        close(delta.mom_v[cell],0.,"RZ variable-mu axial derivative");
        close(delta.mom_w[cell],2.*nu*alpha,"RZ variable-mu swirl derivative");
        const double reference=nu*(38.+24.*alpha*average_r);
        const double error=std::abs(delta.eng[cell]-reference);
        std::cout<<"RZ_HOST_VARIABLE_MU h="<<h<<" work_error="<<error<<'\n';
        if(!std::isfinite(error) || (previous_rz_work_error>1.e-10
            && previous_rz_work_error<3.5*error))
            throw std::runtime_error("RZ variable-mu work lost second-order consistency");
        previous_rz_work_error=error;
    }
    if(previous_rz_work_error>1.e-4)
        throw std::runtime_error("RZ variable-mu existing analytic engineering budget exceeded");
    ViscousGeometryCases::convergence("cpu", evaluate);
    ViscousGeometryCases::radial_origin("cpu", evaluate);
    ViscousGeometryCases::density_stability("cpu", evaluate);
}
