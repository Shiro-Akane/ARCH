// Scale invariance and independent analytic references; no tolerance has units.
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <string>
#include "physics/eos/IdealGas.h"
#include "physics/gravity/GravitySource.h"
#include "numerics/flux/FluxHLL.h"
#include "numerics/flux/FluxHLLC.h"
#include "numerics/flux/FluxRoe.h"
#include "numerics/flux/FluxSW.h"
#include "numerics/flux/FluxVL.h"
#include "numerics/linalg/DenseWrap.h"
#include "core/config/ConfigValidation.h"
#include "driver/DriverUtils.h"
#include "numerics/integrator/TimeIntegratorHelper.h"
#include "numerics/state/RzNativeClosure.h"
#include "numerics/state/RzCellAverage.h"
#include "driver/schedule/DriverControl.h"
#include "driver/schedule/StageScheduler.h"
#include "numerics/diffusion/DiffFlux.h"
#include "numerics/diffusion/DiffFunction.h"
#include "numerics/diffusion/DiffusionAMRStages.h"
#include "math/diffusion/ThermalFiveDecayCases.h"
#include "fixtures/hydro/MeanThermoCases.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void close(double actual, double expected, const char* message, double tolerance=1e-11) {
    if (!std::isfinite(actual) || std::abs(actual-expected)>tolerance*std::max(std::abs(expected),1.0)) {
        std::cerr << message << ": " << actual << " vs " << expected << '\n';
        throw std::runtime_error(message);
    }
}
FluidVector conserved(double rho, double u, double p) { return {rho,rho*u,0,0,p/0.4+0.5*rho*u*u}; }
void compare(FluidVector actual, FluidVector expected, double scale, const char* message) {
    close(actual.rho/scale,expected.rho,message); close(actual.mom_u/scale,expected.mom_u,message);
    close(actual.mom_v/scale,expected.mom_v,message); close(actual.mom_w/scale,expected.mom_w,message);
    close(actual.eng/scale,expected.eng,message);
}
template<class Flux> void check_flux(const IdealGasView& eos) {
    double x[]{1.0}, spec[1]; FluidVector reference;
    const auto left=conserved(1.0,0.3,1.0), right=conserved(0.125,-0.1,0.1);
    Flux::compute_face_flux(left,right,x,x,1,eos,0,0.1,reference,spec);
    for (double scale : {1.,1e-12,1e-20,1e-30,1e-60,1e-100}) {
        FluidVector f; Flux::compute_face_flux(scale*left,scale*right,x,x,1,eos,0,0.1,f,spec);
        compare(f,reference,scale,"flux scale invariance");
        FluxAdmissibility::limit_face(scale*left,scale*right,x,x,1,eos,0,f,spec);
        close(spec[0]/scale,f.rho/scale,"species/mass face consistency");
        Flux::compute_face_flux(scale*left,scale*left,x,x,1,eos,0,0.1,f,spec);
        // Euler analytic flux at a uniform face; independent of any solver.
        compare(f,{0.3,1.09,0,0,0.3*(left.eng+1.0)},scale,"uniform analytic flux");
    }
}
void timestep_controls() {
    SimConfig config; config.io.tmax=1.; config.numerics.dt_max=.125;
    SimulationController controller(config,{});
    close(controller.calculate_next_dt(1.,1.),.125,"physical dt cap on first step");
    controller.advance(.125);
    close(controller.calculate_next_dt(.25,1.),.125,"physical dt cap on later step");
    close(controller.dt_old,.125,"cap retained in restart controller state");
    config.io.plt_dt=.1;
    SimulationController aligned(config,{});
    close(aligned.sync_dt(aligned.calculate_next_dt(1.,1.)),.1,"output alignment after cap");
    const auto identity=arch::config::StateControlIdentity(config);
    for(int control=0;control<3;++control) {
        auto changed=config;
        if(control==0) changed.numerics.dt_max=.2;
        if(control==1) changed.physics.eos_coulomb_mult=.5;
        if(control==2) changed.numerics.hll_roe_wave_speed=false;
        require(identity!=arch::config::StateControlIdentity(changed),"new control missing from restart identity");
    }
}
/** Separate native stage prechecks from actual density-closure thermal acceptance. */
void native_rz_stage_prechecks() {
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    const auto same_native=[](const FluidVector& a,const FluidVector& b) {
        for(const auto member:std::array<double FluidVector::*,5>{
            &FluidVector::rho,&FluidVector::mom_u,&FluidVector::mom_v,
            &FluidVector::mom_w,&FluidVector::eng})
            if(std::bit_cast<std::uint64_t>(a.*member)!=std::bit_cast<std::uint64_t>(b.*member))return false;
        return true;
    };
    // The existing min_eint=100 remains a real thermal bound. A scalar native
    // precheck cannot interpret J/W using a fictitious Cartesian kinetic term.
    arch::state::RepairBudget rz_ledger(0,arch::state::RepairSemantics::RzVolumeAngular);
    const FluidVector rz_unresolved{1.,0.,0.,3.,5.},rz_zero_delta{};
    FluidVector proposed;
    const auto preliminary=TimeIntegration::update_stage_cell(rz_unresolved,rz_unresolved,rz_zero_delta,
        nullptr,nullptr,nullptr,0,1,0.,1.,1e-14,100.,1e10,proposed,nullptr,
        rz_ledger.view(),2.,0,true,5.);
    require(preliminary==arch::state::Status::valid&&same_native(proposed,rz_unresolved)
        &&std::all_of(rz_ledger.values.begin(),rz_ledger.values.end(),[](double x){return x==0.;}),
        "RZ provisional zero-delta update changed native state or conservation ledger");

    Grid grid(amr::MAX_NG,0.,amr::BLOCK_NX,-.5,.5,0.,1.);
    grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(rz);
    FluidState field;field.Preallocate(grid.GetTotalSize());field.InitSpecies(0);
    for(int cell=0;cell<grid.GetTotalSize();++cell)field.set(cell,rz_unresolved);
    const auto geometry=GridMetrics::make_geometry_view(grid,rz);
    const auto read=[&](int cell){return field.get(cell);};
    const int i=grid.Is(),index=grid.GetIndex(i,grid.Js(),0);
    const auto closure=RzThermodynamics::make_cell(read,index,geometry,i);
    // Independent first-cell V=1/2,W=1/3,I=1/4 gives kappa=8/9,
    // so this native candidate has e=5-(8/9)*3^2/2=1, still below 100.
    require(closure.valid(),"positive unconstrained native closure rejected");
    close(closure.internal,1.,"native thermal reference for unchanged min_eint=100");
    require(!RzThermodynamics::make_cell(read,index,geometry,i,{1e-14,100.,1e10}).valid(),
        "actual native closure bypassed the original thermal lower bound");
    IdealGasView eos;
    bool thermal_rejected=false;
    try{RzThermodynamics::validate_patch_eos(field,grid,0,{1e-14,100.,1e10},eos);}
    catch(const std::runtime_error& error){
        thermal_rejected=std::string(error.what()).find("RZ native closure/EOS rejected")!=std::string::npos;
        if(!thermal_rejected)throw;
    }
    require(thermal_rejected,"actual post-ghost EOS closure accepted e below min_eint=100");
    require(same_native(field.get(index),rz_unresolved)
        &&std::all_of(rz_ledger.values.begin(),rz_ledger.values.end(),[](double x){return x==0.;}),
        "thermal rejection heated native fields or recorded a repair");

    // Independent antiderivatives of rho=Omega=1 and e0=1/64 supply every
    // actual positive/negative radial stencil cell, including reflected ghosts.
    // J=integral r^3 dr, W=integral r^2 dr, K_V=(integral r^3 dr)/(2*V).
    constexpr long double internal=1.L/64.L;
    for(int j=0;j<grid.GetTotalY();++j)for(int radial=0;radial<grid.GetTotalX();++radial) {
        const long double a=grid.GetFacePosL(radial),b=grid.GetFacePosR(radial);
        const long double volume=(b*b-a*a)/2.L;
        const long double angular=(b*b*b-a*a*a)/3.L;
        const long double inertia=(b*b*b*b-a*a*a*a)/4.L;
        field.set(grid.GetIndex(radial,j,0),{1.,0.,0.,static_cast<double>(inertia/angular),
            static_cast<double>(internal+inertia/(2.L*volume))});
    }
    const FluidVector cold{1.,0.,0.,3./4.,17./64.};
    require(same_native(field.get(index),cold),"independent first-cell native moments changed");
    require(arch::state::recover(cold).status==arch::state::Status::unresolved_energy,
        "cold witness no longer distinguishes raw and native energy interpretation");
    const auto cold_status=TimeIntegration::update_stage_cell(cold,cold,rz_zero_delta,
        nullptr,nullptr,nullptr,0,1,0.,1.,1e-14,1e-12,1e10,proposed,nullptr,
        rz_ledger.view(),.5,0,true,1./3.);
    require(cold_status==arch::state::Status::valid&&same_native(proposed,cold),
        "valid cold native zero-delta update was rejected or changed");
    RzThermodynamics::validate_patch_eos(field,grid,0,{1e-14,1e-12,1e10},eos);
    const auto cold_closure=RzThermodynamics::make_cell(read,index,geometry,i,{1e-14,1e-12,1e10});
    close(cold_closure.internal,static_cast<double>(internal),"independent cold native internal energy");
    close(eos.get_pressure(cold_closure.effective_mean,nullptr),.4/64.,
        "actual IdealGas native mean pressure");

    // Each finite/rho/simplex failure is rejected provisionally without an
    // energy/composition repair. This is not a whole-Hydro flux qualification.
    for(const auto member:std::array<double FluidVector::*,5>{
        &FluidVector::rho,&FluidVector::mom_u,&FluidVector::mom_v,
        &FluidVector::mom_w,&FluidVector::eng}) {
        auto invalid=cold;invalid.*member=std::numeric_limits<double>::quiet_NaN();
        require(!arch::state::accepted(TimeIntegration::update_stage_cell(
            invalid,invalid,rz_zero_delta,nullptr,nullptr,nullptr,0,1,0.,1.,
            1e-14,1e-12,1e10,proposed,nullptr,rz_ledger.view(),.5,0,true,1./3.)),
            "nonfinite native stage candidate passed provisional checks");
    }
    for(double rho:{0.,-1.,1e-15}) {
        auto invalid=cold;invalid.rho=rho;
        require(!arch::state::accepted(TimeIntegration::update_stage_cell(
            invalid,invalid,rz_zero_delta,nullptr,nullptr,nullptr,0,1,0.,1.,
            1e-14,1e-12,1e10,proposed,nullptr,rz_ledger.view(),.5,0,true,1./3.)),
            "invalid native stage density passed provisional checks");
    }
    for(const auto bounds:std::array<arch::state::Bounds,3>{{
        {1e-14,-1.,1e10},{1e-14,100.,1.},{1e-14,1e-12,std::numeric_limits<double>::quiet_NaN()}}})
        require(!arch::state::accepted(TimeIntegration::update_stage_cell(
            cold,cold,rz_zero_delta,nullptr,nullptr,nullptr,0,1,0.,1.,
            bounds.density,bounds.internal_min,bounds.internal_max,proposed,nullptr,
            rz_ledger.view(),.5,0,true,1./3.)),"malformed native stage bounds passed provisional checks");
    arch::state::RepairBudget species_ledger(2,arch::state::RepairSemantics::RzVolumeAngular);
    for(int failure=-1;failure<3;++failure) {
        // Real species-major stride two leaves an unrelated sentinel between values.
        double fractions[]{.25,77.,.75},delta[]{0.,77.,0.},output[]{0.,77.,0.};
        if(failure==0)fractions[0]=-.25;
        if(failure==1)fractions[0]=std::numeric_limits<double>::infinity();
        if(failure==2)fractions[2]=.5;
        const auto status=TimeIntegration::update_stage_cell(cold,cold,rz_zero_delta,
            fractions,fractions,delta,2,2,0.,1.,1e-14,1e-12,1e10,proposed,output,
            species_ledger.view(),.5,0,true,1./3.);
        require((status==arch::state::Status::valid)==(failure==-1),
            "native stage simplex result differs from strict physical fractions");
        require(output[1]==77.,"native stage wrote outside actual species stride");
        if(failure==-1)require(same_native(proposed,cold)&&output[0]==.25&&output[2]==.75,
            "native provisional stage normalized valid composition");
    }
    for(const auto layout:std::array<std::array<int,2>,3>{{{-1,1},{0,0},{1,1}}})
        require(!arch::state::accepted(TimeIntegration::update_stage_cell(cold,cold,rz_zero_delta,
            nullptr,nullptr,nullptr,layout[0],layout[1],0.,1.,1e-14,1e-12,1e10,
            proposed,nullptr,rz_ledger.view(),.5,0,true,1./3.)),
            "invalid native species layout passed provisional checks");
    require(std::all_of(rz_ledger.values.begin(),rz_ledger.values.end(),[](double x){return x==0.;})
        &&std::all_of(species_ledger.values.begin(),species_ledger.values.end(),[](double x){return x==0.;}),
        "native stage precheck recorded a floor, heat or composition repair");
}
/** Check only native mean thermal inputs, not complete RZ stress/BC qualification. */
void native_rz_diffusion_thermodynamics() {
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    constexpr double internal=1./64.,cv=3.,alpha=.2;
    SpeciesManager species;species.add_species("native-gas",1.,1.,1.4,cv);
    IdealGas eos(1.4,species);
    const auto species_view=species.get_host_view();
    Grid grid(amr::MAX_NG,0.,amr::BLOCK_NX,-.5,.5,0.,1.);
    grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(rz);
    FluidState field;field.Preallocate(grid.GetTotalSize());field.InitSpecies(1);
    // Independent signed antiderivatives, including actual reflected ghosts:
    // V=integral r dr, W=integral r^2 dr, J=I=integral r^3 dr.
    for(int j=0;j<grid.GetTotalY();++j)for(int radial=0;radial<grid.GetTotalX();++radial) {
        const long double a=grid.GetFacePosL(radial),b=grid.GetFacePosR(radial);
        const long double volume=(b*b-a*a)/2.L;
        const long double angular=(b*b*b-a*a*a)/3.L;
        const long double inertia=(b*b*b*b-a*a*a*a)/4.L;
        const int cell=grid.GetIndex(radial,j,0);
        field.set(cell,{1.,0.,0.,static_cast<double>(inertia/angular),
            static_cast<double>(static_cast<long double>(internal)+inertia/(2.L*volume))});
        field.X(0,cell)=1.;
    }
    const auto geometry=GridMetrics::make_geometry_view(grid,rz);
    const DiffFlux::HostDiffusionStateReader read{field};
    const int i=grid.Is(),j=grid.Js(),cell=grid.GetIndex(i,j,0);
    const auto raw=field.get(cell),left_raw=field.get(cell-1);
    require(raw.rho==1.&&raw.mom_w==3./4.&&raw.eng==17./64.,
        "native diffusion cold witness lost independent first-annulus moments");
    require(arch::state::recover(raw).status==arch::state::Status::unresolved_energy,
        "native diffusion witness no longer distinguishes raw point energy");
    const auto thermal=DiffFlux::diffusion_thermal_input(raw,read,geometry,cell,i);
    const auto left_thermal=DiffFlux::diffusion_thermal_input(left_raw,read,geometry,cell-1,i-1);
    require(thermal.valid&&left_thermal.valid,"actual native/reflected thermal closure rejected");
    double fraction[]{1.},face_fraction[1],charge[1],inverse_mass[1];
    close(eos.get_temperature(1.,arch::state::recover(thermal.state).internal,fraction),internal/cv,
        "native cold IdealGas T=e/Cv");
    close(eos.get_cv(1.,internal/cv,fraction),cv,"native cold actual IdealGas Cv");
    close(eos.get_pressure(thermal.state,fraction),.4*internal,"native cold actual IdealGas pressure");
    const DiffFlux::DiffusionConfigView conduction{true,true,false,false,0.,alpha,0.};
    const auto properties=DiffFlux::evaluate_diffusion_face_properties(left_raw,raw,
        fraction,fraction,1,eos,species_view,conduction,face_fraction,charge,inverse_mass,
        &left_thermal,&thermal);
    require(properties.coefficients.valid,"native face thermal coefficients rejected");
    close(properties.temperature_left,internal/cv,"reflected native face T");
    close(properties.temperature_right,internal/cv,"native face T");
    close(properties.heat_capacity,cv,"native face Cv");
    close(properties.coefficients.alpha_therm,alpha,"native face thermal transport");
    const auto raw_properties=DiffFlux::evaluate_diffusion_face_properties(left_raw,raw,
        fraction,fraction,1,eos,species_view,conduction,face_fraction,charge,inverse_mass);
    require(!raw_properties.coefficients.valid,
        "absent native thermal inputs silently guessed a rotational inertia");
    require(!DiffFlux::diffusion_thermal_input(raw,read,geometry,cell,i+1).valid,
        "native thermal input ignored actual radial/stencil identity");

    double composition[1],neighbour_composition[1];
    const auto timestep=[&] {
        return DiffFlux::evaluate_diffusion_dt_candidate(field.get(cell),field.mass_fractions.data()+cell,
            1,grid.GetTotalSize(),eos,species_view,conduction,geometry,i,j,0,composition,
            neighbour_composition,face_fraction,charge,inverse_mass,read);
    };
    const auto dt=timestep();
    require(dt.valid,"native cold cell/neighbor thermal timestep rejected");
    // First full-ring annulus A_r+/V=2/h_r, A_r-/V=0; both axial ratios=1/h_z.
    close(dt.value,1./(alpha*(2./(grid.dx1*grid.dx1)+2./(grid.dx2*grid.dx2))),
        "native thermal face-capacity FE bound");

    // EOS effective momenta must not replace the original generic traction or
    // its work. This is the preexisting face assembly, before the RZ torque owner.
    const DiffFlux::DiffusionConfigView viscous{true,true,true,false,.03,alpha,0.};
    double species_left[1],species_right[1];FluidVector flux;
    const auto face=DiffFlux::evaluate_diffusion_face(left_raw,raw,fraction,fraction,1,1,
        grid.dx1,eos,species_view,viscous,species_left,species_right,face_fraction,charge,
        inverse_mass,flux,nullptr,1,{},nullptr,&left_thermal,&thermal);
    require(face.active&&face.valid,"native thermal input did not enable existing traction assembly");
    close(flux.mom_w,-.03*1.5/grid.dx1,"EOS mapping changed native generic face momentum");
    close(flux.eng,0.,"EOS mapping changed symmetric generic face work");
    FluidVector delta;
    const auto source=DiffFlux::evaluate_geometric_diffusion_cell(raw,fraction,eos,species_view,
        viscous,geometry,i,j,0,1.,charge,inverse_mass,delta,read);
    require(source.active&&source.valid,"native cold geometric thermal input rejected");
    compare(delta,FluidVector{},1.,"EOS mapping changed retained zero radial connection");
    require(field.get(cell).mom_w==raw.mom_w&&field.get(cell).eng==raw.eng,
        "native thermal leaves mutated evolved momentum or energy");

    // No closure may be guessed from a raw vector alone. Invalid center thermal
    // energy and an independently invalid adjacent face must fail explicitly.
    auto invalid=raw;invalid.eng=.25;field.set(cell,invalid); // I/(2V), hence e=0.
    const auto invalid_thermal=DiffFlux::diffusion_thermal_input(invalid,read,geometry,cell,i);
    require(!invalid_thermal.valid&&!timestep().valid,"zero native internal energy was repaired or skipped");
    FluidVector sentinel{7.,8.,9.,10.,11.};
    const auto rejected_face=DiffFlux::evaluate_diffusion_face(left_raw,invalid,fraction,fraction,
        1,1,grid.dx1,eos,species_view,conduction,species_left,species_right,face_fraction,
        charge,inverse_mass,sentinel,nullptr,1,{},nullptr,&left_thermal,&invalid_thermal);
    require(!rejected_face.valid&&sentinel.rho==7.&&sentinel.mom_w==10.&&sentinel.eng==11.,
        "invalid native face wrote flux or silently succeeded");
    const auto rejected_source=DiffFlux::evaluate_geometric_diffusion_cell(invalid,fraction,eos,
        species_view,viscous,geometry,i,j,0,1.,charge,inverse_mass,sentinel,read);
    require(!rejected_source.valid&&sentinel.rho==7.&&sentinel.mom_w==10.&&sentinel.eng==11.,
        "invalid native geometric thermal input changed source output");
    field.set(cell,raw);
    for(int neighbour:std::array<int,4>{cell-1,cell+1,cell-grid.stride_y,cell+grid.stride_y}) {
        const auto saved=field.get(neighbour);auto bad=saved;bad.eng=-1.;field.set(neighbour,bad);
        require(!timestep().valid,"invalid true neighbor thermal state escaped face timestep check");
        field.set(neighbour,saved);
    }
    for(int density_cell:std::array<int,2>{cell,cell+1}) {
        const auto saved=field.get(density_cell);auto bad=saved;bad.rho=-1.;field.set(density_cell,bad);
        require(!DiffFlux::diffusion_thermal_input(field.get(cell),read,geometry,cell,i).valid
            &&!timestep().valid,"invalid actual native density stencil was replaced by guessed inertia");
        field.set(density_cell,saved);
    }

    // Exercise the real Host face traversal with a thermal jump, rather than
    // supplying a handcrafted effective vector to the coefficient API.
    for(int radial=0;radial<grid.GetTotalX();++radial) {
        const int index=grid.GetIndex(radial,j+1,0);auto value=field.get(index);
        value.eng+=internal;field.set(index,value);
    }
    SimConfig config;config.physics.diffusion.use_diffusion=true;
    config.physics.diffusion.use_thermal_diffusion=true;
    config.physics.diffusion.use_viscous_diffusion=false;
    config.physics.diffusion.use_species_diffusion=false;
    config.physics.diffusion.alpha_therm=alpha;
    std::vector<FluidVector> fluxes(grid.GetTotalSize());
    std::vector<double> species_fluxes(grid.GetTotalSize());
    DiffFlux::compute_fluxes(field,eos,grid,config,fluxes,species_fluxes,1,false,rz);
    const auto& axial=fluxes[grid.GetIndex(i,j+1,0)];
    close(axial.eng,-alpha*internal/grid.dx2,"actual native Host Fourier flux=-rho*alpha*de/dz");
    close(axial.rho,0.,"native conduction introduced mass flux");
    close(axial.mom_w,0.,"native conduction changed angular momentum flux");

    // Ordinary charts use the unchanged raw vector and never consult a native
    // density reader. This independent Cartesian view carries no native chart
    // identity; a cylindrical 2D Existing view is an explicitly retired chart.
    GridMetrics::GeometryView existing;
    existing.geometry=GridMetrics::Geometry::Cartesian;existing.dim=2;
    int reads=0;const auto counting_read=[&](int index){++reads;return field.get(index);};
    const auto legacy=DiffFlux::diffusion_thermal_input(raw,counting_read,existing,cell,i);
    require(legacy.valid&&reads==0&&legacy.state.rho==raw.rho&&legacy.state.mom_w==raw.mom_w
        &&legacy.state.eng==raw.eng,"legacy diffusion thermal convention changed");
}
/** Actual native angular FE counterexample, with independent physical means.
 * Workflow: furnish analytical logical ghosts (not a BC qualification), check
 * the real EOS and every real row dt, evaluate the genuine diffusion operator,
 * then apply the existing one-stage RKL1 helper. Stable angular graph rows do
 * not guarantee nonlinear kinetic/thermal admissibility after an explicit step.
 * No Runtime/clock is owned here, so no macro rollback or BC grant is claimed.
 */
void native_rz_angular_fe_thermal_reference()
{
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    constexpr long double e0=1.L/33554432.L,nu=1.L;
    const auto relative=[](double actual,long double expected,const char* why) {
        require(std::isfinite(actual)&&std::isfinite(expected)
            &&std::abs(static_cast<long double>(actual)-expected)
                <=64.L*std::numeric_limits<double>::epsilon()*std::abs(expected),why);
    };
    require(amr::BLOCK_NX==16&&amr::BLOCK_NY==16,
        "angular FE independent reference requires the actual 16x16 block");
    Grid grid(amr::MAX_NG,0.,16.,0.,16.,0.,1.,1,1,1);
    grid.dim=2;grid.geometry="cylindrical";
    grid.dyadic_identity={true,{0.,0.},{16.,16.},{1,1},0,{0,0},false};
    grid.InitializeTopology(rz);
    require(GridMetrics::matches_identity(grid.dyadic_identity,
        {grid.x1_min,grid.x2_min},{grid.x1_max,grid.x2_max},{grid.dx1,grid.dx2})
        &&grid.dx1==1.&&grid.dx2==1.,"angular FE real native root identity changed");
    const auto geometry=GridMetrics::make_geometry_view(grid,rz);
    SpeciesManager species;species.add_species("angular-fe-gas",1.,1.,1.4,2.);
    IdealGas eos(1.4,species);const auto species_view=species.get_host_view();
    SimConfig config{};
    config.physics.diffusion.use_diffusion=true;
    config.physics.diffusion.use_viscous_diffusion=true;
    config.physics.diffusion.use_thermal_diffusion=false;
    config.physics.diffusion.use_species_diffusion=false;
    config.physics.diffusion.nu_visc=double(nu);
    config.physics.diffusion.alpha_therm=0.;config.physics.diffusion.D_spec=0.;
    require(config.physics.diffusion.diff_cfl==.8,
        "angular FE witness must retain the actual default diffusion CFL");
    const arch::state::Bounds bounds{config.numerics.sml_rho,
        config.numerics.min_eint,config.numerics.max_eint};
    require(bounds.density<1.&&bounds.internal_min<e0&&bounds.internal_max>e0,
        "angular FE source does not satisfy the unchanged physical bounds");
    FluidState field;field.Preallocate(grid.GetTotalSize());field.InitSpecies(1);
    field.stage_repairs.reset(1,arch::state::RepairSemantics::RzVolumeAngular);
    const double padding=std::bit_cast<double>(std::uint64_t{0x7ff8000000000075});
    for(int index=0;index<grid.GetTotalSize();++index) {
        field.set(index,{padding,padding,padding,padding,padding});
        field.enuc_rate[index]=padding;field.X(0,index)=padding;
    }
    // True antiderivatives; signed negative ghosts have odd phi momentum and
    // even density/energy. All logical ghosts are analytic fixture inputs;
    // neither a manufactured phased handler nor actual BC completion is used.
    for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
        const long double l=grid.GetFacePosL(i),h=grid.GetFacePosR(i);
        const long double V=(h*h-l*l)/2.L,W=(h*h*h-l*l*l)/3.L;
        const long double C=(h*h*h*h-l*l*l*l)/4.L;
        const long double omega=std::max(std::abs(l),std::abs(h))<=1.L?0.L:1.L;
        const long double J=omega*C,Et=V*e0+omega*omega*C/2.L;
        const int index=grid.GetIndex(i,j,0);
        field.set(index,{1.,0.,0.,double(J/W),double(Et/V)});
        field.enuc_rate[index]=std::ldexp(double(index),-10);field.X(0,index)=1.;
    }
    struct ArraySnapshot {
        std::array<std::vector<double>,7> values;
        std::array<const double*,7> addresses{};
    };
    const auto arrays=[](const FluidState& state) {
        return std::array<const std::vector<double>*,7>{&state.rho,&state.mom_u,
            &state.mom_v,&state.mom_w,&state.eng,&state.enuc_rate,&state.mass_fractions};
    };
    const auto pin=[&](const FluidState& state) {
        ArraySnapshot snapshot;const auto current=arrays(state);
        for(int n=0;n<7;++n) {
            snapshot.values[n]=*current[n];snapshot.addresses[n]=current[n]->data();
        }
        return snapshot;
    };
    const auto unchanged=[&](const FluidState& state,const ArraySnapshot& snapshot) {
        const auto current=arrays(state);
        for(int n=0;n<7;++n) {
            require(current[n]->data()==snapshot.addresses[n]
                &&current[n]->size()==snapshot.values[n].size(),
                "angular FE reference changed an original array lease");
            for(std::size_t k=0;k<snapshot.values[n].size();++k)
                require(std::bit_cast<std::uint64_t>((*current[n])[k])
                    ==std::bit_cast<std::uint64_t>(snapshot.values[n][k]),
                    "angular FE reference changed original logical/padding bits");
        }
    };
    const auto before=pin(field);const auto repair_before=field.stage_repairs;
    const auto repair_address=field.stage_repairs.values.data();
    const auto source_diffusion_boundary=field.diffusion_boundary;
    const auto source_flux_capture=field.boundary_flux_capture;
    RzThermodynamics::validate_completed_patch_eos(field,grid,1,bounds,eos);
    const DiffFlux::HostDiffusionStateReader read{field};
    const int i=grid.Is(),j=grid.Js(),first=grid.GetIndex(i,j,0);
    relative(RzThermodynamics::make_cell(read,first,geometry,i,bounds).internal,e0,
        "actual first native source EOS lost independent positive e0");
    double composition[1],neighbor_composition[1],face_composition[1],charge[1],inverse_mass[1];
    const auto diffusion=DiffFlux::make_diffusion_config_view(config);
    // Independent capacities and s centroids of every REAL radial row. The
    // graph includes both axial neighbors even though this field has zero dz
    // gradient. Its old scalar bound remains an independent angular check;
    // the complete meridional Stokes rows now determine the actual timestep.
    const auto C=[](int n) {
        const long double l=n,h=n+1;
        return (h*h*h*h-l*l*l*l)/4.L;
    };
    const auto s=[&](int n) {
        const long double l=n,h=n+1;
        return (h*h*h*h*h*h-l*l*l*l*l*l)/(6.L*C(n));
    };
    const auto K=[&](int face) {
        if(face==0)return 0.L;
        const long double r=face;
        return 2.L*nu*r*r*r*r/(s(face)-s(face-1));
    };
    for(int n=0;n<amr::BLOCK_NX;++n) {
        const int radial=i+n,index=grid.GetIndex(radial,j,0);
        const long double q_generic=4.L*nu+4.L*nu/((2.L*n+1.L)*(2.L*n+1.L));
        const long double q_phi=(K(n)+K(n+1))/C(n)+2.L*nu;
        require(q_generic>=q_phi,"independent actual angular row exceeds retained row");
        // Unit true-volume cells: a normal Stokes diagonal has absolute row
        // 10/3+2/(3r), and a transverse shear has row 3. Sum their real V
        // capacities and the connection row 4/(3r^2)+4/(3r). At the axis
        // the zero-area inner face vanishes, leaving 8+6+8=22. For n>0
        // both radial faces give the closed expression below. No production
        // stress, capacity or row-bound helper supplies this reference.
        const long double radius=n+.5L;
        const long double q_full=nu*(n==0?22.L:
            38.L/3.L+8.L/(3.L*radius)+4.L/(3.L*radius*radius));
        require(q_full>=q_generic&&q_full>=q_phi,
            "independent complete Stokes row does not bound the angular block");
        const auto dt=DiffFlux::evaluate_diffusion_dt_candidate(field.get(index),
            field.mass_fractions.data()+index,1,grid.GetTotalSize(),eos,species_view,
            diffusion,geometry,radial,j,0,composition,neighbor_composition,
            face_composition,charge,inverse_mass,read);
        require(dt.valid,"actual angular FE row rejected valid analytical source");
        relative(dt.value,1.L/q_full,"actual full-Stokes FE row differs from integral reference");
    }
    const double raw_fe=DiffFlux::adaptive_dt_diff(field,eos,grid,config,1.,rz);
    relative(raw_fe,1.L/22.L,"actual all-cell FE minimum differs from complete 16-row reference");
    std::vector<FluidVector> face_flux(grid.GetTotalSize());
    std::vector<double> face_species(grid.GetTotalSize());
    DiffFlux::compute_fluxes(field,eos,grid,config,face_flux,face_species,0,false,rz);
    const auto face=face_flux[grid.GetIndex(i+1,j,0)];
    relative(face.mom_w,-15.L/16.L,"actual angular face traction differs from true capacity centroid");
    relative(face.eng,-75.L/512.L,"actual angular face work differs from same true face velocity");
    require(face_flux[first].mom_w==0.&&face_flux[first].eng==0.,
        "actual regular axis contributes artificial traction/work");
    FluidState L;L.Preallocate(grid.GetTotalSize());L.InitSpecies(1);
    DiffFlux::compute_diffusion_operator(field,L,eos,grid,config,rz);
    relative(L.mom_w[first],45.L/16.L,"actual first-cell native angular operator differs from J'/W");
    relative(L.eng[first],75.L/256.L,"actual first-cell native work operator differs from E'/V");
    for(int row=grid.Js();row<grid.Je();++row)for(int column=i;column<grid.Ie();++column) {
        const int index=grid.GetIndex(column,row,0);
        require(L.rho[index]==0.&&L.mom_u[index]==0.&&L.mom_v[index]==0.&&L.X(0,index)==0.,
            "pure native angular diffusion changed unrelated conserved quantities");
    }
    for(bool negative:{false,true}) {
        // Preserve both original explicit FE counterexamples and their exact
        // values. These finite leaf proposals are NOT a timestep-selected
        // whole RKL method: the new tensor bound selects two stages for both.
        // Actual selected three-stage rejection/two-stage acceptance and full
        // macro rollback are exercised by the existing Runtime owner.
        const double dt=negative?1./10.:1./16.;
        const int stages=DiffFunction::compute_stages_rkl1(dt,raw_fe,
            config.physics.diffusion.diff_cfl,config.physics.diffusion.max_stages);
        require(stages==2,"actual tensor-bound RKL selection differs from the independent interval");
        const auto coefficient=DiffFunction::get_rkl1_coeffs(1,1);
        require(coefficient.tilde_mu==1.,"actual one-stage RKL1 no longer equals FE");
        std::vector<FluidVector> increment(grid.GetTotalSize());
        std::vector<double> species_increment(grid.GetTotalSize());
        for(int index=0;index<grid.GetTotalSize();++index) {
            increment[index]=dt*L.get(index);species_increment[index]=dt*L.X(0,index);
        }
        FluidState proposal=field;
        Numerics::Diffusion::detail::apply_first_rkl_stage(field,proposal,increment,
            species_increment,grid,coefficient.tilde_mu);
        TimeIntegration::accept_stage_state(proposal,grid,config.numerics,rz);
        // Independent finite-step physics: H_new=H_n+dt*E'-dt^2*J'^2/(2*C).
        // Strict recovery intentionally returns no usable e for the negative
        // candidate, so do not read its invalid internal field as an oracle.
        const long double tau=static_cast<long double>(dt)*nu;
        const long double J_new=15.L*tau/16.L;
        const long double E_native=e0+75.L*tau/256.L;
        const long double expected_e=e0+75.L*tau/256.L-225.L*tau*tau/64.L;
        relative(proposal.mom_w[first],J_new/(1.L/3.L),"genuine first RKL1 native angular mean");
        relative(proposal.eng[first],E_native,"genuine first RKL1 native energy mean");
        const long double inferred_e=static_cast<long double>(proposal.eng[first])
            -(.5L*(static_cast<long double>(proposal.mom_w[first])/3.L)
                *(static_cast<long double>(proposal.mom_w[first])/3.L)/(1.L/4.L))/(1.L/2.L);
        relative(double(inferred_e),expected_e,"independent finite-step true-inertia thermal value");
        const auto candidate_before=pin(proposal);
        const auto candidate_read=[&](int index){return proposal.get(index);};
        const auto closure=RzThermodynamics::make_cell(candidate_read,first,geometry,i,bounds);
        if(negative) {
            require(expected_e<0.L&&expected_e<-1.L/1024.L,
                "negative FE witness became a rounding-scale cancellation");
            require(!closure.valid()&&closure.inertia_mapping_valid
                &&closure.status==arch::state::Status::unresolved_energy,
                "genuine negative native FE thermal state was repaired or accepted");
            const double fraction[1]{1.};
            require(RzThermodynamics::validate_mean_eos(closure,fraction,1,eos)
                ==arch::state::Status::unresolved_energy,
                "selected EOS mean path accepted negative actual native closure");
            bool rejected=false;
            try {RzThermodynamics::validate_completed_patch_eos(proposal,grid,1,bounds,eos);}
            catch(const RzThermodynamics::AcceptanceError& error) {
                const auto& diagnostic=error.diagnostic();
                rejected=diagnostic.phase==RzThermodynamics::AcceptancePhase::effective_thermal
                    &&diagnostic.status==arch::state::Status::unresolved_energy
                    &&diagnostic.index==first&&diagnostic.inertia_mapping_valid;
                if(!rejected)throw;
            }
            require(rejected,"actual completed-cell EOS gate did not reject the FE thermal counterexample");
        } else {
            require(expected_e>e0&&closure.valid(),"genuine smaller angular FE step rejected positive thermal state");
            relative(closure.internal,e0+75.L/16384.L,"positive FE native closure differs from independent exact reference");
            const double fraction[1]{1.};
            require(RzThermodynamics::validate_mean_eos(closure,fraction,1,eos)==arch::state::Status::valid,
                "selected IdealGas rejected genuinely positive smaller angular FE step");
            RzThermodynamics::validate_completed_patch_eos(proposal,grid,1,bounds,eos);
        }
        unchanged(proposal,candidate_before);unchanged(field,before);
    }
    unchanged(field,before);
    require(field.GetNumSpecies()==1&&field.block_total_size_==grid.GetTotalSize()
        &&field.diffusion_boundary==source_diffusion_boundary
        &&field.boundary_flux_capture==source_flux_capture
        &&field.stage_repairs.values.data()==repair_address
        &&field.stage_repairs.values==repair_before.values
        &&field.stage_repairs.semantics==repair_before.semantics
        &&field.stage_repairs.block_uid==repair_before.block_uid
        &&field.stage_repairs.stage==repair_before.stage
        &&std::bit_cast<std::uint64_t>(field.stage_repairs.time)
            ==std::bit_cast<std::uint64_t>(repair_before.time),
        "angular FE leaves changed source bookkeeping/observer ownership");
    for(int axis=0;axis<3;++axis)
        require(std::bit_cast<std::uint64_t>(field.stage_repairs.position[axis])
            ==std::bit_cast<std::uint64_t>(repair_before.position[axis]),
            "angular FE leaves changed source receipt position");
    std::cout<<"RZ_ANGULAR_FE_REFERENCE_PASS actual_operator=true explicit_FE_leaf=true selected_RKL1_stages=2 full_tensor_rows=16 negative_thermal_rejected=true smaller_step_positive=true BC_or_Runtime_qualified=false\n";
}

/** Destination numerical-inertia counterexample; not an AMR transfer qualification.
 * Independent physical rho=32-r^4, Omega=1, e0=1/1024 supplies true V/W means.
 * Coarsening preserves M/J/E but refits rho_* and I_*; the fine numerical
 * closures accept while the actual coarse closure rejects without a repair.
 */
void native_rz_coarsening_thermal_reference() {
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    constexpr long double physical_internal=1.L/1024.L;
    constexpr double cv=3.;
    struct Reference {long double mass,inertia,angular,energy,internal;};
    // Independent Fraction fit/antiderivatives, omitting common 2*pi*dz.
    // E and J come from the true quartic field, never from the fitted rho ray.
    constexpr std::array<Reference,3> references{{
        {95.L/6.L,70.L/9.L,63.L/8.L,24287.L/6144.L,-1037.L/486400.L},
        {1535.L/384.L,575.L/1152.L,1023.L/2048.L,99743.L/393216.L,839659.L/903808000.L},
        {1515.L/128.L,943.L/128.L,15105.L/2048.L,484875.L/131072.L,61005.L/97528832.L}}};
    static_assert(1535+3*1515==64*95&&1023+15105==256*63
        &&99743+3*484875==64*24287,"independent exact M/J/E split sums changed");
    SpeciesManager species;species.add_species("inertia-gas",1.,1.,1.4,cv);
    IdealGas eos(1.4,species);
    const arch::state::Bounds bounds{1e-14,1e-12,1e10};
    const double fractions[]{1.};
    std::array<std::array<long double,3>,3> actual_integrals{};
    for(int resolution=0;resolution<2;++resolution) {
        const double spacing=resolution==0?1.:.5;
        Grid grid(amr::MAX_NG,0.,spacing*amr::BLOCK_NX,-.5,.5,0.,1.);
        grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(rz);
        FluidState field;field.Preallocate(grid.GetTotalSize());field.InitSpecies(1);
        const auto geometry=GridMetrics::make_geometry_view(grid,rz);
        const int j=grid.Js(),first=grid.GetIndex(grid.Is()-1,j,0);
        const int last=grid.GetIndex(grid.Is()+(resolution==0?1:2),j,0);
        std::array<FluidVector,4> before{};
        // Only this real required stencil is populated: coarse [-1,0],[0,1],
        // [1,2], or fine [-1/2,0],[0,1/2],[1/2,1],[1,3/2]. The reader below
        // rejects every other cell; no unused patch/padding receives a grant.
        for(int radial=grid.Is()-1;radial<=grid.Is()+(resolution==0?1:2);++radial) {
            const long double lower=grid.GetFacePosL(radial),upper=grid.GetFacePosR(radial);
            require(!(lower<0.&&upper>0.),"coarsening reference cell crosses the axis");
            const bool reflected=upper<=0.;
            const long double a=reflected?-upper:lower,b=reflected?-lower:upper;
            // rho(abs(r)) is even. Integrate true rho*r and rho*r^3 on the
            // reflected positive annulus; only physical azimuthal parity flips.
            const auto moment=[&](int power) {
                return 32.L*(std::pow(b,power+1)-std::pow(a,power+1))/(power+1)
                    -(std::pow(b,power+5)-std::pow(a,power+5))/(power+5);
            };
            const long double volume=(b*b-a*a)/2.L,angular=(b*b*b-a*a*a)/3.L;
            const long double mass=moment(1),true_angular=moment(3);
            const long double energy=physical_internal*mass+true_angular/2.L;
            const int cell=grid.GetIndex(radial,j,0);
            const FluidVector native{static_cast<double>(mass/volume),0.,0.,
                static_cast<double>((reflected?-1.L:1.L)*true_angular/angular),
                static_cast<double>(energy/volume)};
            field.set(cell,native);field.X(0,cell)=1.;before[cell-first]=native;
            require(RzThermodynamics::provisional_native_state(native,fractions,1,1,bounds)
                ==arch::state::Status::valid,"true quartic native input failed finite/rho/simplex checks");
        }
        const auto read=[&](int cell) {
            require(cell>=first&&cell<=last,"coarsening closure accessed an unfurnished density stencil");
            return field.get(cell);
        };
        for(int child=0;child<(resolution==0?1:2);++child) {
            const int reference_index=resolution==0?0:child+1;
            const auto& reference=references[reference_index];
            const int radial=grid.Is()+child,cell=grid.GetIndex(radial,j,0);
            const long double a=grid.GetFacePosL(radial),b=grid.GetFacePosR(radial);
            const long double volume=(b*b-a*a)/2.L,angular=(b*b*b-a*a*a)/3.L;
            const auto native=field.get(cell);
            const long double mass=native.rho*volume,j_moment=native.mom_w*angular;
            const long double energy=native.eng*volume;
            actual_integrals[reference_index]={mass,j_moment,energy};
            close(static_cast<double>(mass),static_cast<double>(reference.mass),"true quartic native mass integral");
            close(static_cast<double>(j_moment),static_cast<double>(reference.angular),"true quartic native J integral");
            close(static_cast<double>(energy),static_cast<double>(reference.energy),"true quartic native E integral");
            const auto closure=RzThermodynamics::make_cell(read,cell,geometry,radial,bounds);
            require(closure.density.valid,"actual quartic V-mean density fit rejected");
            close(closure.density.capacity,static_cast<double>(reference.inertia),"independent fitted density inertia");
            const long double thermal=(energy-j_moment*j_moment/(2.L*closure.density.capacity))/mass;
            close(static_cast<double>(thermal),static_cast<double>(reference.internal),
                "independent destination fitted-inertia thermal reference");
            if(resolution==0) {
                require(thermal<0.&&!closure.valid()
                    &&closure.status==arch::state::Status::unresolved_energy,
                    "genuine coarse numerical-inertia deficit was repaired or accepted");
                require(RzThermodynamics::validate_mean_eos(closure,fractions,1,eos)
                    !=arch::state::Status::valid,"actual EOS gate accepted rejected coarse closure");
            } else {
                require(closure.valid()&&RzThermodynamics::validate_mean_eos(closure,fractions,1,eos)
                    ==arch::state::Status::valid,"positive fine numerical closure failed actual IdealGas");
                close(closure.internal,static_cast<double>(reference.internal),"positive fine actual native thermal energy");
                close(eos.get_temperature(native.rho,closure.internal,fractions),
                    static_cast<double>(reference.internal/cv),"positive fine actual IdealGas temperature");
                close(eos.get_cv(native.rho,closure.internal/cv,fractions),cv,"positive fine actual IdealGas Cv");
                for(int node=0;node<RzThermodynamics::physical_node_count;++node)
                    require(arch::state::validate_eos(RzThermodynamics::base_point(closure,
                        RzThermodynamics::physical_node_radius(closure,node)),fractions,1,bounds,eos)
                        ==arch::state::Status::valid,"fine numerical physical baseline rejected by actual EOS");
            }
        }
        // Validation remains read-only: no floor, heating, species projection
        // or repair receipt is created. Compare every furnished native field.
        for(int cell=first;cell<=last;++cell) {
            const auto after=field.get(cell);
            for(const auto member:std::array<double FluidVector::*,5>{&FluidVector::rho,
                &FluidVector::mom_u,&FluidVector::mom_v,&FluidVector::mom_w,&FluidVector::eng})
                require(std::bit_cast<std::uint64_t>(after.*member)
                    ==std::bit_cast<std::uint64_t>(before[cell-first].*member),
                    "numerical coarsening rejection changed true native input");
            require(field.X(0,cell)==1.,"numerical coarsening validation changed composition");
        }
    }
    // Exact independent split sums are asserted alongside the actual stored
    // mean integrals. This is not a violation of additive true-inertia Cauchy:
    // I_* changes when the destination density polynomial is refitted.
    for(int component=0;component<3;++component)
        close(static_cast<double>(actual_integrals[1][component]+actual_integrals[2][component]),
            static_cast<double>(actual_integrals[0][component]),"true M/J/E conserved across native coarsening reference");
}
/** Independently constructed full logical ghost EOS witness, not BC/AMR acceptance.
 * Constant rho=Omega=1, e=1/64 supplies native V/W means by antiderivatives on
 * each real signed interval. All storage padding remains NaN. The completed
 * gate must accept every logical cell using real one-sided edge support, then
 * reject malformed last-row/last-column ghosts without altering any array.
 */
void native_rz_completed_ghost_eos_reference() {
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    constexpr double internal=1./64.,cv=3.;
    Grid grid(amr::MAX_NG,0.,static_cast<double>(amr::BLOCK_NX),-1.,1.,0.,1.);
    grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(rz);
    const int nx=grid.GetTotalX(),ny=grid.GetTotalY();
    require(nx>=3&&nx<grid.stride_y,"completed ghost witness requires real x padding");
    FluidState field;field.Preallocate(grid.GetTotalSize());field.InitSpecies(1);
    const double nan=std::numeric_limits<double>::quiet_NaN();
    for(int index=0;index<grid.GetTotalSize();++index) {
        field.set(index,{nan,nan,nan,nan,nan});field.enuc_rate[index]=nan;field.X(0,index)=nan;
    }
    // Signed V=integral r dr and I=integral r^3 dr are both negative on an
    // axis-reflected annulus; W=integral r^2 dr is positive. Hence E remains
    // even, m_phi=I/W is odd and physical omega stays +1 on either side.
    for(int j=0;j<ny;++j)for(int i=0;i<nx;++i) {
        const long double a=grid.GetFacePosL(i),b=grid.GetFacePosR(i);
        require(!(a<0.&&b>0.),"positive completed fixture straddles the axis");
        const long double volume=(b*b-a*a)/2.L;
        const long double angular=(b*b*b-a*a*a)/3.L;
        const long double inertia=(b*b*b*b-a*a*a*a)/4.L;
        const int index=grid.GetIndex(i,j,0);
        field.set(index,{1.,0.,0.,static_cast<double>(inertia/angular),
            static_cast<double>(internal+inertia/(2.L*volume))});
        field.enuc_rate[index]=0.;field.X(0,index)=1.;
    }
    SpeciesManager species;species.add_species("ghost-reference",1.,1.,1.4,cv);
    IdealGas eos(1.4,species);const double fractions[]{1.};
    const arch::state::Bounds bounds{1e-14,1e-12,1e10};
    const auto view=GridMetrics::make_geometry_view(grid,rz);
    const auto read=[&](int index) {
        require(index>=0&&index<grid.GetTotalSize()&&index%grid.stride_y<nx,
            "completed closure read storage padding instead of logical support");
        return field.get(index);
    };
    using Snapshot=std::array<std::vector<double>,7>;
    const auto snapshot=[&]() -> Snapshot {
        return {field.rho,field.mom_u,field.mom_v,field.mom_w,field.eng,
            field.enuc_rate,field.mass_fractions};
    };
    const auto unchanged=[&](const Snapshot& before) {
        const auto after=snapshot();
        for(std::size_t component=0;component<before.size();++component) {
            require(before[component].size()==after[component].size(),"completed EOS changed array extent");
            for(std::size_t index=0;index<before[component].size();++index)
                require(std::bit_cast<std::uint64_t>(before[component][index])
                    ==std::bit_cast<std::uint64_t>(after[component][index]),
                    "completed EOS validation altered native values, Xi or NaN padding");
        }
    };
    const auto valid_before=snapshot();
    RzThermodynamics::validate_patch_eos(field,grid,1,bounds,eos);
    RzThermodynamics::validate_completed_patch_eos(field,grid,1,bounds,eos);
    unchanged(valid_before);
    // A selected EOS can report a failure while returning finite values.
    // The common cell leaf must preserve the actual failed query's phase,
    // and a valid traversal still calls mean + six points in original order.
    struct QueryProbe {
        const IdealGas& eos;std::array<int,3>& calls;bool& failed;int fail_call;
        bool required_query_failed() const {return failed;}
        double get_temperature(double rho,double energy,const double* x) const {
            ++calls[0];if(calls[0]==fail_call)failed=true;
            return eos.get_temperature(rho,energy,x);
        }
        double get_pressure(const FluidVector& value,const double* x) const {
            ++calls[1];return eos.get_pressure(value,x);
        }
        double get_sound_speed(const FluidVector& value,double pressure,const double* x) const {
            ++calls[2];return eos.get_sound_speed(value,pressure,x);
        }
    };
    const auto supported=[nx](const auto& reader,int index,const auto& geometry,int i,const auto& limits) {
        return RzThermodynamics::make_cell_supported(reader,index,geometry,i,std::clamp(i-1,0,nx-3),limits);
    };
    for(int fail_call:{0,1,4}) {
        std::array<int,3> calls{};bool failed=false;
        const QueryProbe probe{eos,calls,failed,fail_call};
        const auto result=RzThermodynamics::detail::check_patch_eos_cell(view,1,bounds,probe,
            grid.Is(),grid.Js(),grid.GetIndex(grid.Is(),grid.Js(),0),supported,read,fractions);
        const int expected_calls=fail_call?fail_call:7;
        require(calls==std::array<int,3>{expected_calls,expected_calls,expected_calls},
            "native shared acceptance changed the original EOS query order/count");
        require((result.status==arch::state::Status::valid)==(fail_call==0),
            "native shared acceptance ignored a finite failed EOS query");
        if(fail_call)require(result.phase==(fail_call==1?RzThermodynamics::AcceptancePhase::mean_eos:
            RzThermodynamics::AcceptancePhase::physical_eos)&&result.node==(fail_call==1?-1:2),
            "native shared acceptance lost actual mean/physical failure identity");
    }
    unchanged(valid_before);
    for(int j=0;j<ny;++j)for(int i=0;i<nx;++i) {
        const int index=grid.GetIndex(i,j,0),support=std::clamp(i-1,0,nx-3);
        const auto cell=RzThermodynamics::make_cell_supported(read,index,view,i,support,bounds);
        const long double a=grid.GetFacePosL(i),b=grid.GetFacePosR(i);
        const long double inertia=(b*b*b*b-a*a*a*a)/4.L;
        require(cell.valid()&&RzThermodynamics::validate_mean_eos(cell,fractions,1,eos)
            ==arch::state::Status::valid,"actual completed logical closure/IdealGas rejected");
        close(cell.density.capacity,static_cast<double>(inertia),"signed constant-density ghost inertia");
        close(cell.omega,1.,"signed native ghost parity changed physical omega");
        close(cell.internal,internal,"independent native ghost specific thermal reference");
        close(eos.get_temperature(1.,cell.internal,fractions),internal/cv,"actual ghost IdealGas temperature");
        for(int node=0;node<RzThermodynamics::physical_node_count;++node) {
            const double radius=RzThermodynamics::physical_node_radius(cell,node);
            const auto point=RzThermodynamics::base_point(cell,radius);
            compare(point,{1.,0.,0.,radius,internal+.5*radius*radius},1.,
                "independent physical constant-density rotating ghost baseline");
            require(arch::state::validate_eos(point,fractions,1,bounds,eos)
                ==arch::state::Status::valid,"actual ghost physical baseline EOS rejected");
        }
    }
    // Place each bad value in the very last logical ghost, outside the old
    // active domain/density support. The active-only gate must still accept;
    // the completed gate must discover this new region and leave all bytes.
    const int late=grid.GetIndex(nx-1,ny-1,0);
    const auto rejects=[&](const Grid& candidate,const char* expected) {
        const auto before=snapshot();bool rejected=false;
        try {RzThermodynamics::validate_completed_patch_eos(field,candidate,1,bounds,eos);}
        catch(const std::runtime_error& error) {
            rejected=std::string(error.what()).find(expected)!=std::string::npos;
            if(!rejected)throw;
        }
        require(rejected,"invalid completed logical ghost escaped actual native/EOS gate");
        unchanged(before);
    };
    const double saved_rho=field.rho[late],saved_energy=field.eng[late],saved_x=field.X(0,late);
    for(double bad:{0.,-1.,nan}) {
        field.rho[late]=bad;
        RzThermodynamics::validate_patch_eos(field,grid,1,bounds,eos);
        // An earlier same-row closure may encounter the bad required density
        // support before reaching this target's provisional check.
        const auto before=snapshot();bool rejected=false;
        try {RzThermodynamics::validate_completed_patch_eos(field,grid,1,bounds,eos);}
        catch(const std::runtime_error& error) {
            const std::string message=error.what();
            rejected=message.find("RZ native provisional state rejected")!=std::string::npos
                ||message.find("RZ native closure/EOS rejected")!=std::string::npos;
            if(!rejected)throw;
        }
        require(rejected,"invalid completed ghost density was repaired or skipped");unchanged(before);
    }
    field.rho[late]=saved_rho;field.eng[late]=-1.;
    RzThermodynamics::validate_patch_eos(field,grid,1,bounds,eos);
    rejects(grid,"RZ native closure/EOS rejected");field.eng[late]=saved_energy;
    field.X(0,late)=0.;RzThermodynamics::validate_patch_eos(field,grid,1,bounds,eos);
    rejects(grid,"RZ native provisional state rejected");field.X(0,late)=saved_x;
    Grid straddling=grid;straddling.x1_min=.5;straddling.x1_max+=.5;
    straddling.InitializeTopology(rz);
    const auto straddle_view=GridMetrics::make_geometry_view(straddling,rz);
    const int crossing=straddling.ng-1;
    require(straddling.GetFacePosL(crossing)<0.&&straddling.GetFacePosR(crossing)>0.,
        "straddling ghost negative lost its actual geometric counterexample");
    require(!RzThermodynamics::make_cell_supported(read,
        straddling.GetIndex(crossing,ny-1,0),straddle_view,crossing,crossing-1,bounds).valid(),
        "straddling ghost support was silently folded or repaired");
    rejects(straddling,"RZ native closure/EOS rejected");
    // NaN padding survived both successful and rejecting traversals. These
    // arrays were independently furnished, not produced by a real BC/exchange.
    for(int j=0;j<ny;++j)for(int i=nx;i<grid.stride_y;++i) {
        const int index=grid.GetIndex(i,j,0);
        require(std::isnan(field.rho[index])&&std::isnan(field.mom_u[index])
            &&std::isnan(field.mom_v[index])&&std::isnan(field.mom_w[index])
            &&std::isnan(field.eng[index])&&std::isnan(field.enuc_rate[index])
            &&std::isnan(field.X(0,index)),"completed gate consumed or filled storage padding");
    }
}


/** Genuine bound Native face/row spacing, with independently integrated
 * constant-density annular capacities. Actual Host flux traversal and raw
 * thermal FE traversal share the face distance; axial torque rows additionally
 * use the CURRENT cell height. This grants no full tensor/RKL qualification.
 */
void native_rz_actual_diffusion_distances()
{
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    constexpr long double rho=1.25L,alpha=.2L,nu=.03L;
    const auto relative=[](double actual,long double expected,const char* why) {
        const long double band=64.L*std::numeric_limits<double>::epsilon()*std::abs(expected);
        require(std::isfinite(actual)&&std::isfinite(expected)
            &&std::abs(static_cast<long double>(actual)-expected)<=band,why);
    };
    Grid root(amr::MAX_NG,.1,1.3,-.3,.8,0.,1.,1,1,1);
    root.dim=2;root.geometry="cylindrical";root.InitializeTopology(rz);
    amr::Block block{};block.Reset();block.id=0;block.level=14;
    block.logical_x1=0;block.logical_x2=(std::uint32_t{1}<<14)-1;
    block.InitGeometry(root,(root.x1_max-root.x1_min)/amr::BLOCK_NX,
        (root.x2_max-root.x2_min)/amr::BLOCK_NY,1.,rz);
    const auto& g=block.grid;const auto geometry=GridMetrics::make_geometry_view(g,rz);
    const int i=g.Is()+5;
    const auto height=[&](int row) {
        return static_cast<long double>(g.GetAxialFacePosR(row))-g.GetAxialFacePosL(row);
    };
    int j=-1;
    for(int row=g.Js()+1;row<g.Je()-1;++row)
        if((height(row-1)+height(row))!=(height(row)+height(row+1))) {j=row;break;}
    require(j>=0,"real nonbinary canonical grid lacks distinct lower/upper face distances");
    const long double dz=height(j),d_lower=(height(j-1)+dz)/2.L,d_upper=(dz+height(j+1))/2.L;
    require(dz>0.&&d_lower>0.&&d_upper>0.&&d_lower!=d_upper,
        "actual positive unequal Native face distances missing");
    relative(DiffFlux::diffusion_face_spacing(geometry,1,i,j),d_lower,"Native lower actual face distance");
    relative(DiffFlux::diffusion_face_spacing(geometry,1,i,j+1),d_upper,"Native upper actual face distance");
    require(DiffFlux::diffusion_face_spacing(geometry,1,i,j)
        !=DiffFlux::diffusion_face_spacing(geometry,1,i,j+1),"both Native rows reused current height");
    SpeciesManager species;species.add_species("spacing-gas",1.,1.,1.4,3.);
    IdealGas eos(1.4,species);const auto species_view=species.get_host_view();
    FluidState field;field.Preallocate(g.GetTotalSize());field.InitSpecies(1);
    // Thermal mode uses exact e=100,102,105 steps; torque mode uses actual
    // physical Omega=0,1,3 and e=100. Both fill every legitimate radial halo.
    const auto fill=[&](bool rotating) {
        for(int index=0;index<g.GetTotalSize();++index) {
            field.set(index,{double(rho),0.,0.,0.,double(100.L*rho)});field.X(0,index)=1.;
        }
        for(int row=0;row<g.GetTotalY();++row)for(int column=0;column<g.GetTotalX();++column) {
            const long double l=g.GetFacePosL(column),h=g.GetFacePosR(column);
            const long double omega=rotating?(row<j?0.L:row==j?1.L:3.L):0.L;
            const long double e=rotating?100.L:row<j?100.L:row==j?102.L:105.L;
            const long double mphi=rho*omega*.75L*(h+l)*(h*h+l*l)/(h*h+h*l+l*l);
            const long double energy=rho*(e+omega*omega*(h*h+l*l)/4.L);
            field.set(g.GetIndex(column,row,0),{double(rho),0.,0.,double(mphi),double(energy)});
        }
    };
    fill(false);const FluidState before=field;
    SimConfig config{};config.physics.diffusion.use_diffusion=true;
    config.physics.diffusion.use_thermal_diffusion=true;
    config.physics.diffusion.use_viscous_diffusion=false;config.physics.diffusion.use_species_diffusion=false;
    config.physics.diffusion.alpha_therm=double(alpha);
    std::vector<FluidVector> flux(g.GetTotalSize());std::vector<double> species_flux(g.GetTotalSize());
    DiffFlux::compute_fluxes(field,eos,g,config,flux,species_flux,1,false,rz);
    relative(flux[g.GetIndex(i,j,0)].eng,-rho*alpha*2.L/d_lower,"actual Host lower Fourier spacing");
    relative(flux[g.GetIndex(i,j+1,0)].eng,-rho*alpha*3.L/d_upper,"actual Host upper Fourier spacing");
    const int index=g.GetIndex(i,j,0);const DiffFlux::HostDiffusionStateReader read{field};
    const DiffFlux::DiffusionConfigView thermal{true,true,false,false,0.,double(alpha),0.};
    double x[1],nx[1],fx[1],charge[1],inverse_mass[1];
    const auto dt=DiffFlux::evaluate_diffusion_dt_candidate(field.get(index),field.mass_fractions.data()+index,
        1,g.GetTotalSize(),eos,species_view,thermal,geometry,i,j,0,x,nx,fx,charge,inverse_mass,read);
    require(dt.valid,"actual Native thermal FE row rejected");
    const long double rl=g.GetFacePosL(i),rh=g.GetFacePosR(i);
    const long double dr=rh-rl,dr_high=static_cast<long double>(g.GetFacePosR(i+1))-g.GetFacePosL(i+1);
    // Actual annular A_r/V=2*r_face/((rh-rl)*(rh+rl)); axial A/V=1/dz.
    const long double inverse=alpha*(2.L*rl/(dr*(rh+rl)*dr)
        +2.L*rh/(dr*(rh+rl)*dr_high)+(1.L/d_lower+1.L/d_upper)/dz);
    relative(dt.value,1.L/inverse,"actual thermal dt row did not use both producer face distances");
    require(field.rho==before.rho&&field.mom_w==before.mom_w&&field.eng==before.eng
        &&field.mass_fractions==before.mass_fractions,"flux/dt qualification mutated inputs");

    fill(true);config.physics.diffusion.use_thermal_diffusion=false;
    config.physics.diffusion.use_viscous_diffusion=true;config.physics.diffusion.nu_visc=double(nu);
    DiffFlux::compute_fluxes(field,eos,g,config,flux,species_flux,1,false,rz);
    const long double C=rho*dr*(rh+rl)*(rh*rh+rl*rl)/4.L;
    const long double W=dr*(rh*rh+rh*rl+rl*rl)/3.L;
    require(C>0.&&W>0.,"independent annular angular capacity invalid");
    relative(flux[g.GetIndex(i,j,0)].mom_w,-nu*C/(d_lower*W),"actual lower torque producer spacing");
    relative(flux[g.GetIndex(i,j+1,0)].mom_w,-2.L*nu*C/(d_upper*W),"actual upper torque producer spacing");
    const auto center=RzViscousStress::angular_cell(read,index,geometry,i);
    const auto low=RzViscousStress::angular_cell(read,index-g.stride_y,geometry,i);
    const auto high=RzViscousStress::angular_cell(read,index+g.stride_y,geometry,i);
    require(center.valid&&low.valid&&high.valid,"actual annular row capacities rejected");
    relative(center.capacity,C,"actual angular capacity versus independent rho*r^3 integral");
    relative(RzViscousStress::face_row_rate(center,low,1,double(d_lower),double(nu),double(rl),double(dz)),
        nu*C/(d_lower*dz*C),"lower angular row nu*Cface/(d_face*dz_center*Ccenter)");
    relative(RzViscousStress::face_row_rate(center,high,1,double(d_upper),double(nu),double(rl),double(dz)),
        nu*C/(d_upper*dz*C),"upper angular row nu*Cface/(d_face*dz_center*Ccenter)");
    require(std::isnan(RzViscousStress::face_row_rate(center,low,1,double(d_lower),double(nu),double(rl),-1.)),
        "invalid actual angular-row height accepted");
    // The retained ordinary angular metric belongs to spherical 2D. Use a
    // fresh mathematical view, without relabeling a bound native RZ identity.
    GridMetrics::GeometryView ordinary;
    ordinary.geometry=GridMetrics::Geometry::Spherical;ordinary.dim=2;
    ordinary.ng=g.ng;ordinary.dx1=g.dx1;ordinary.dx2=g.dx2;ordinary.dx3=g.dx3;
    ordinary.x1_min=g.x1_min;ordinary.x2_min=g.x2_min;ordinary.x3_min=g.x3_min;
    require(DiffFlux::diffusion_face_spacing(ordinary,1,i,j)==DiffFlux::diffusion_face_spacing(
        ordinary.geometry,ordinary.dim,1,ordinary.dx1,ordinary.dx2,ordinary.dx3,
        ordinary.GetCellCenterX(i),ordinary.SourceTheta(j)),"ordinary producer literal spacing changed");
    auto unbound=geometry;unbound.dyadic_identity={};
    require(DiffFlux::diffusion_face_spacing(unbound,1,i,j)==unbound.dx2,
        "unbound Native uniform axial distance changed");
    require(RzViscousStress::face_row_rate(center,low,1,double(d_lower),double(nu),double(rl))
        ==RzViscousStress::face_row_rate(center,low,1,double(d_lower),double(nu),double(rl),double(d_lower)),
        "omitted old row height marker changed uniform-spacing formula");
    std::cout<<"RZ_ACTUAL_DIFFUSION_DISTANCE_PASS actual_flux_and_thermal_dt=true axial_angular_rows=true whole_tensor_qualified=false\n";
}

/**
 * Actual native representation refusal at huge radius.
 * Workflow: construct a real three-cell constant-density support, independently
 * integrate V/W/I, then check the shared thermal and viscous angular consumers.
 * This is a scalar representation test, not a complete Hydro/diffusion stage.
 */
void native_rz_angular_velocity_underflow()
{
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    Grid grid(amr::MAX_NG,0.,16.*1.e70,-.5,.5,0.,1.);
    grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(rz);
    FluidState field;field.Preallocate(grid.GetTotalSize());field.InitSpecies(1);
    constexpr double tiny_angular_mean=1.e-280;
    for(int cell=0;cell<grid.GetTotalSize();++cell) {
        field.set(cell,{1.,0.,0.,tiny_angular_mean,1.});
        field.enuc_rate[cell]=0.;field.X(0,cell)=1.;
    }
    const auto geometry=GridMetrics::make_geometry_view(grid,rz);
    const auto read=[&](int cell){return field.get(cell);};
    const int i=grid.Is(),index=grid.GetIndex(i,grid.Js(),0);
    const long double lower=grid.GetFacePosL(i),upper=grid.GetFacePosR(i);
    require(lower==0.L&&upper>0.L,"underflow witness is not the real first annulus");
    const long double volume=(upper*upper-lower*lower)/2.L;
    const long double angular_measure=(upper*upper*upper-lower*lower*lower)/3.L;
    const long double inertia=(upper*upper*upper*upper-lower*lower*lower*lower)/4.L;
    const long double expected_omega=static_cast<long double>(tiny_angular_mean)*angular_measure/inertia;
    const long double physical_point_momentum=expected_omega*upper;
    require(std::isfinite(expected_omega)&&expected_omega>0.L
        &&static_cast<double>(expected_omega)==0.,
        "independent required nonzero omega does not actually underflow double");
    require(std::isfinite(static_cast<double>(inertia))&&inertia>0.L
        &&std::isfinite(static_cast<double>(physical_point_momentum))
        &&static_cast<double>(physical_point_momentum)>0.,
        "angular underflow witness lost its real finite inertia/point momentum");
    const auto relative=[](double actual,long double expected,const char* message) {
        require(std::isfinite(actual)&&std::isfinite(expected)
            &&std::abs(static_cast<long double>(actual)-expected)
                <=64.L*std::numeric_limits<double>::epsilon()*std::abs(expected),message);
    };
    // Independent first-annulus identities: omega=4*j/(3*rho*h),
    // rho*omega*h=4*j/3. The nonzero momentum must not be silently discarded.
    relative(static_cast<double>(physical_point_momentum),
        4.L*static_cast<long double>(tiny_angular_mean)/3.L,
        "independent tiny point momentum antiderivatives disagree");
    relative(static_cast<double>(volume),upper*upper/2.L,
        "independent first-annulus volume antiderivative disagrees");
    const auto density=RzDensity::density_cell(read,index,geometry,i);
    require(density.valid&&RzDensity::detail::cell_valid(density),
        "actual huge-radius constant-density owner is invalid");
    relative(density.capacity,inertia,"actual constant-density inertia differs from independent integral");

    const auto arrays=[&]() {
        return std::array<const std::vector<double>*,7>{&field.rho,&field.mom_u,&field.mom_v,
            &field.mom_w,&field.eng,&field.enuc_rate,&field.mass_fractions};
    };
    const auto pin_arrays=[&]() {
        std::array<std::vector<double>,7> snapshot;
        const auto current=arrays();
        for(int n=0;n<7;++n)snapshot[n]=*current[n];
        return snapshot;
    };
    const auto pin_addresses=[&]() {
        std::array<const double*,7> addresses{};const auto current=arrays();
        for(int n=0;n<7;++n)addresses[n]=current[n]->data();
        return addresses;
    };
    const auto unchanged=[&](const auto& snapshot,const auto& addresses) {
        const auto current=arrays();
        for(int n=0;n<7;++n) {
            require(current[n]->data()==addresses[n]&&current[n]->size()==snapshot[n].size(),
                "angular representation check changed source array ownership");
            for(std::size_t k=0;k<snapshot[n].size();++k)
                require(std::bit_cast<std::uint64_t>((*current[n])[k])
                    ==std::bit_cast<std::uint64_t>(snapshot[n][k]),
                    "angular representation check modified a source array bit");
        }
    };
    const auto tiny_snapshot=pin_arrays();const auto tiny_addresses=pin_addresses();
    require(std::isnan(RzDensity::angular_velocity(tiny_angular_mean,density)),
        "unrepresentable nonzero required omega was silently converted to zero");
    const auto closure=RzThermodynamics::from_density(read(index),density);
    require(!closure.valid()&&!closure.inertia_mapping_valid&&std::isnan(closure.omega),
        "thermal native consumer accepted an unrepresentable nonzero omega");
    const auto base=RzThermodynamics::base_point(closure,static_cast<double>(upper));
    require(!std::isfinite(base.rho)&&!std::isfinite(base.mom_w),
        "invalid required angular representation produced a usable physical baseline");
    const auto angular=RzViscousStress::angular_cell(read,index,geometry,i);
    require(!angular.valid&&std::isnan(angular.omega),
        "viscous native consumer accepted an unrepresentable nonzero omega");
    require(!RzViscousStress::azimuthal_face(angular,angular,1,grid.dx2,1.,
        static_cast<double>(upper)).valid,
        "invalid angular representation produced a usable viscous traction");
    unchanged(tiny_snapshot,tiny_addresses);

    // A genuinely zero J has exactly zero omega and remains a valid baseline.
    field.set(index,{1.,0.,0.,0.,1.});
    const auto zero_snapshot=pin_arrays();const auto zero_addresses=pin_addresses();
    const auto zero=RzThermodynamics::from_density(read(index),density);
    const auto zero_angular=RzViscousStress::angular_cell(read,index,geometry,i);
    require(RzDensity::angular_velocity(0.,density)==0.&&zero.valid()&&zero.omega==0.
        &&zero.internal==1.&&zero_angular.valid&&zero_angular.omega==0.,
        "true zero angular momentum was rejected or changed");
    const auto zero_face=RzViscousStress::azimuthal_face(zero_angular,zero_angular,
        1,grid.dx2,1.,static_cast<double>(upper));
    require(zero_face.valid&&zero_face.momentum==0.&&zero_face.energy==0.,
        "true zero angular momentum changed the original zero traction/work");
    unchanged(zero_snapshot,zero_addresses);

    // Represented omega=1/h, rho=1, e=1 gives j=3/4 and E=5/4.
    // This exact native first-annulus reference stays valid on the same Grid.
    field.set(index,{1.,0.,0.,.75,1.25});
    const auto normal_snapshot=pin_arrays();const auto normal_addresses=pin_addresses();
    const auto represented=RzThermodynamics::from_density(read(index),density);
    const auto represented_angular=RzViscousStress::angular_cell(read,index,geometry,i);
    require(represented.valid()&&represented_angular.valid&&represented.omega>0.
        &&represented_angular.omega>0.,"represented native omega baseline was rejected");
    const long double represented_omega=.75L*angular_measure/inertia;
    relative(represented.omega,represented_omega,"represented thermal omega changed");
    relative(represented_angular.omega,represented_omega,"represented viscous omega changed");
    relative(represented.internal,1.L,"represented native thermal baseline changed");
    relative(RzThermodynamics::base_point(represented,static_cast<double>(upper)).mom_w,
        represented_omega*upper,"represented native point momentum changed");
    unchanged(normal_snapshot,normal_addresses);
    std::cout<<"RZ_ANGULAR_REPRESENTATION_PASS nonzero_underflow_rejected=true zero_and_represented_preserved=true whole_stage_qualified=false\n";
}

/** Endpoint items use the supplied physical V; they never count cumulative flux. */
void diffusion_activity_scalar_contract() {
    const auto same=DiffFlux::diffusion_energy_activity_term(.5,2.,2.);
    require(same.valid&&same.signed_energy_change==0.&&same.absolute_energy_change==0.,
        "identical energy endpoints must have zero scalar activity");
    const auto outward=DiffFlux::diffusion_energy_activity_term(.5,2.,6.);
    const auto inward=DiffFlux::diffusion_energy_activity_term(2.,3.,2.);
    require(outward.valid&&inward.valid&&outward.signed_energy_change==-2.
        &&inward.signed_energy_change==2.
        &&outward.signed_energy_change+inward.signed_energy_change==0.
        &&outward.absolute_energy_change+inward.absolute_energy_change==4.,
        "volume-weighted signed cancellation concealed nonzero endpoint activity");
    const auto overflow=DiffFlux::diffusion_energy_activity_term(2.,
        std::numeric_limits<double>::max(),0.);
    require(!overflow.valid,"nonrepresentable activity product was accepted");
    for(const double volume:{0.,-1.,std::numeric_limits<double>::infinity()})
        require(!DiffFlux::diffusion_energy_activity_term(volume,2.,1.).valid,
            "invalid true cell measure was accepted for endpoint activity");
    require(!DiffFlux::diffusion_energy_activity_term(1.,
            std::numeric_limits<double>::quiet_NaN(),1.).valid
        &&!DiffFlux::diffusion_energy_activity_term(1.,2.,
            std::numeric_limits<double>::infinity()).valid,
        "nonfinite energy endpoint was accepted for activity");
}

void leaves() {
    diffusion_activity_scalar_contract();
    native_rz_angular_velocity_underflow();
    native_rz_stage_prechecks();
    native_rz_diffusion_thermodynamics();
    native_rz_angular_fe_thermal_reference();
    native_rz_actual_diffusion_distances();
    native_rz_coarsening_thermal_reference();
    native_rz_completed_ghost_eos_reference();
    timestep_controls();
    require(MeanThermoCases::evaluate(),"shared mean thermodynamic view contract");
    IdealGasView eos;
    check_flux<FluxHLL<PCMReconstruction>>(eos); check_flux<FluxHLLC<PCMReconstruction>>(eos);
    check_flux<FluxRoe<PCMReconstruction>>(eos); check_flux<FluxSW<PCMReconstruction>>(eos);
    check_flux<FluxVL<PCMReconstruction>>(eos);
    for (double scale : {1.,1e-12,1e-20,1e-30,1e-60,1e-100}) {
        auto u=conserved(scale,2.,3.*scale);
        const auto k=arch::state::recover(u);
        require(k.status==arch::state::Status::valid,"valid low-density state rejected");
        close(k.internal,7.5,"scale-independent internal energy");
        close(eos.get_pressure(u,nullptr)/scale,3.,"ideal-gas analytic pressure");
        FluidVector delta;
        Physical::Gravity::add_external_gravity_source_cell(u,{3.,0.,0.,true},0.5,delta);
        close(delta.mom_u/scale,1.5,"gravity at any positive density");
        close(delta.eng/scale,3.,"gravity work analytic reference");
        close(compute_limited_slope<VanLeer>(scale,2.*scale,3.*scale)/scale,0.5,"MUSCL slope scaling");
        double v[]{0.2,1.,1.1,0.3,0.8,1.}, scaled[6],l,r,sl,sr;
        for(int i=0;i<6;++i) scaled[i]=scale*v[i];
        PPMReconstruction::reconstruct_scalar_ppm(v,l,r);
        PPMReconstruction::reconstruct_scalar_ppm(scaled,sl,sr);
        close(sl/scale,l,"PPM left scale"); close(sr/scale,r,"PPM right scale");
        close(compute_cfl_cell_dt(u,3.,1,.25,1.,1.),.025,"analytic two-face CFL");
        DiffFlux::DiffusionConfigView diffusion{true,true,false,false,0.,.2,0.};
        FluidVector diffusion_flux;
        const auto face = DiffFlux::evaluate_diffusion_face(
            conserved(scale,0.,.4*scale), conserved(scale,0.,.8*scale),
            nullptr,nullptr,0,1,.5,eos,SpeciesPODView{},diffusion,
            nullptr,nullptr,nullptr,nullptr,nullptr,diffusion_flux,nullptr,1);
        require(face.valid && face.active,"low-density conduction rejected");
        close(diffusion_flux.eng/scale,-.4,"Fourier heat flux analytic reference");
        const auto invalid_face = DiffFlux::evaluate_diffusion_face(
            FluidVector{}, u,nullptr,nullptr,0,1,.5,eos,SpeciesPODView{},diffusion,
            nullptr,nullptr,nullptr,nullptr,nullptr,diffusion_flux,nullptr,1);
        require(!invalid_face.valid,"zero-density diffusion treated as inactive success");
        DenseMatrixData<2> matrix; matrix(1,1)=2.*scale; matrix(1,2)=scale;
        matrix(2,1)=scale; matrix(2,2)=3.*scale; double rhs[]{4.*scale,7.*scale};
        require(DenseLUSolver::solve<2,2>(matrix,rhs),"small nonsingular LU rejected");
        close(rhs[0],1.,"dense original-system solution x"); close(rhs[1],2.,"dense original-system solution y");
        auto repair=arch::state::apply_bounds(u,2.*scale,1.,100.);
        require(repair.status==arch::state::Status::repaired,"positive density floor not recorded");
        close(u.rho/scale,2.,"density bound"); close(u.mom_u/u.rho,2.,"floor preserves velocity");
        close(arch::state::recover(u).internal,7.5,"floor preserves thermal state");
    }
    // Original-system residuals and failure controls, not provider status alone.
    for (double scale : {1.,1e-100}) {
        DenseMatrixData<2> identity; identity(1,1)=identity(2,2)=scale;
        double zero_rhs[]{0.,0.};
        require(DenseLUSolver::solve<2,2>(identity,zero_rhs),"scaled identity zero RHS rejected");
        require(zero_rhs[0]==0. && zero_rhs[1]==0.,"zero RHS changed");
        DenseMatrixData<2> singular; singular(1,1)=singular(2,1)=scale;
        singular(1,2)=singular(2,2)=scale; double b[]{scale,scale};
        require(!DenseLUSolver::solve<2,2>(singular,b),"singular system reported success");
    }
    require(VanLeer::calc(std::numeric_limits<double>::infinity())==2.,"unbounded ratio limiter");
    for (double rho : {0.,-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
        auto bad=conserved(rho,0.,1.);
        require(!arch::state::accepted(arch::state::apply_bounds(bad,1.,1.,10.).status),"invalid state accepted");
    }
    auto unresolved=FluidVector{1.,1e10,0.,0.,5e19};
    require(arch::state::recover(unresolved).status==arch::state::Status::unresolved_energy,"cancellation hidden");
    auto hot=conserved(1.,0.,40.);
    require(arch::state::apply_bounds(hot,1e-12,1e-10,10.).status==arch::state::Status::energy_ceiling,"ceiling silently clipped");
    double bounded[]{.999,.001};
    require(arch::state::normalize_composition(bounded,2,1,.2),"floor simplex rejected");
    require(bounded[0]>=.2 && bounded[1]>=.2,"normalization undid the requested floor");
    close(bounded[0]+bounded[1],1.,"floor simplex mass closure");
    double zero[]{0.,0.}; require(!arch::state::normalize_composition(zero,2,1,1e-20),"invented empty composition");
    double mixture[]{0.3,0.7}; require(arch::state::normalize_composition(mixture,2,1,1e-20),"valid composition rejected");
    close(mixture[0]+mixture[1],1.,"composition normalization");
}
}

/** Real native weights must not silently erase a nonzero represented point
 * momentum. The constant has a represented exact integral, but all actual
 * Gauss products round to zero; this unsupported direct arithmetic is rejected.
 */
void check_native_weighted_component_underflow()
{
    SpeciesManager species;species.add_species("weighted-range",1.,1.,1.4,2.);
    IdealGas eos(1.4,species);
    const arch::state::Bounds bounds{1.e-14,0.,10.};
    const double xi[]{1.};
    Grid grid(amr::MAX_NG,0.,16.,-.125,.125,0.,1.);
    grid.dim=2;grid.geometry="cylindrical";
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    grid.InitializeTopology(rz);
    const auto samples=GridMetrics::Rz::CellAverageSamples(
        grid.GetFacePosL(grid.Is()),grid.GetFacePosR(grid.Is()),
        grid.x2_min,grid.x2_min+grid.dx2);
    const double tiny=std::numeric_limits<double>::denorm_min();
    for(int field:{1,2,3})for(double sign:{-1.,1.}) {
        std::array<FluidVector,8> points;
        long double exact=0.L;
        for(std::size_t k=0;k<samples.size();++k) {
            points[k]={1.,0.,0.,0.,1.};
            double* component=field==1?&points[k].mom_u:field==2?&points[k].mom_v:&points[k].mom_w;
            *component=sign*tiny;
            const double weight=field==3?samples[k].angular_weight:samples[k].volume_weight;
            require(weight>0.&&weight<.5&&weight*(*component)==0.,
                "actual native Gauss witness does not lose its nonzero product");
            exact+=static_cast<long double>(weight)*(*component);
            require(arch::state::validate_eos(points[k],xi,1,bounds,eos)==arch::state::Status::valid,
                "weighted component witness is not a real selected EOS point");
        }
        require(static_cast<double>(exact)==sign*tiny,
            "constant native component's independent integral is not representable");
        const auto frozen=points;
        const auto result=RzCellAverage::conserved_mean(samples,[&](std::size_t k){return points[k];});
        require(!result.valid()&&result.status==RzCellAverage::Status::unrepresentable,
            "native weighted mean quietly erased a represented nonzero point component");
        for(std::size_t k=0;k<points.size();++k)
            for (const auto member:{&FluidVector::rho,&FluidVector::mom_u,&FluidVector::mom_v,&FluidVector::mom_w,&FluidVector::eng})
                require(std::bit_cast<std::uint64_t>(points[k].*member)==std::bit_cast<std::uint64_t>(frozen[k].*member),
                    "weighted rejection changed immutable point input");
    }
    // A legitimate zero and ordinary tiny normal products keep the original
    // arithmetic; no floor, normalisation or conservative correction is added.
    for(double angular:{0.,1.e-280,-1.e-280}) {
        const FluidVector point{1.,0.,0.,angular,1.};
        const auto result=RzCellAverage::conserved_mean(samples,[&](std::size_t){return point;});
        require(result.valid(),"native weighted range guard rejected a represented original product");
        long double reference=0.L;
        for(const auto& sample:samples)reference+=static_cast<long double>(sample.angular_weight)*angular;
        if(angular==0.)require(result.value.mom_w==0.,"native weighted zero acquired a floor");
        else require(std::abs((static_cast<long double>(result.value.mom_w)-reference)/reference)
            <=64.L*std::numeric_limits<double>::epsilon(),"native weighted normal arithmetic changed");
    }
}

namespace {
/** Independent stationary ODE reference: L(q)=0 implies q(t)=q(0).
 * Used values are checked by IEEE bits; no magnitude threshold or floor.
 * The active hexadecimal cases below are exact rational arithmetic, not the
 * production recurrence repeated as an expected-value helper.
 */
void check_rkl_stationary_cell_reference() {
    namespace rkl=Numerics::Diffusion::detail;
    const auto bits=[](double value){return std::bit_cast<std::uint64_t>(value);};
    const auto equal_fluid=[&](const FluidVector& actual,const FluidVector& expected) {
        for(auto member:std::array<double FluidVector::*,5>{&FluidVector::rho,
            &FluidVector::mom_u,&FluidVector::mom_v,&FluidVector::mom_w,&FluidVector::eng})
            require(bits(actual.*member)==bits(expected.*member),"RKL exact component reference changed");
    };
    const FluidVector seed{std::nextafter(1.e7,0.),.13,-.27,.41,1.e8};
    const std::array<double,2> fraction{.37,1.-.37},zero_species{};
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const double inf=std::numeric_limits<double>::infinity();
    // Each non-density component evolves in turn. Density and every other
    // component are independent stationary ODEs, despite an active energy.
    for(auto member:std::array<double FluidVector::*,4>{&FluidVector::mom_u,
            &FluidVector::mom_v,&FluidVector::mom_w,&FluidVector::eng}) {
        FluidVector rhs{};rhs.*member=2.;
        FluidVector first=seed;auto first_x=fraction;
        rkl::apply_first_rkl_stage_cell(first,first_x.data(),rhs,zero_species.data(),
            2,1,.125,first,first_x.data()); // actual first-stage alias
        for(auto inactive:std::array<double FluidVector::*,5>{&FluidVector::rho,
                &FluidVector::mom_u,&FluidVector::mom_v,&FluidVector::mom_w,&FluidVector::eng})
            if(inactive!=member)require(bits(first.*inactive)==bits(seed.*inactive),
                "first RKL stationary component with active neighbor changed");
        require(first.*member!=seed.*member,"first RKL active component lost its RHS");
        for(int s=0;s<2;++s)require(bits(first_x[s])==bits(fraction[s]),"first RKL stationary Xi changed");
        for(bool scaled:{false,true})for(bool second:{false,true}) {
            const auto order=second?DiffFunction::RKLOrder::Second:DiffFunction::RKLOrder::First;
            const auto coefficient=DiffFunction::get_rkl_coeffs(order,2,5);
            FluidVector older=seed;auto older_x=fraction;
            rkl::apply_recursive_rkl_stage_cell(seed,fraction.data(),seed,fraction.data(),
                older,older_x.data(),rhs,zero_species.data(),FluidVector{},zero_species.data(),
                2,1,coefficient,second,.125,scaled,older,older_x.data());
            for(auto inactive:std::array<double FluidVector::*,5>{&FluidVector::rho,
                    &FluidVector::mom_u,&FluidVector::mom_v,&FluidVector::mom_w,&FluidVector::eng})
                if(inactive!=member)require(bits(older.*inactive)==bits(seed.*inactive),
                    "recursive RKL stationary component changed under older alias");
            require(older.*member!=seed.*member,"recursive RKL active component lost its RHS");
            for(int s=0;s<2;++s)require(bits(older_x[s])==bits(fraction[s]),
                "recursive RKL stationary Xi changed under older alias");
        }
    }
    // Tiny nonzero operators have a representable result at a zero seed. An
    // exact-zero optimization must not turn these independent ODEs stationary.
    const DiffFunction::RKLCoeffs dyadic{1.5,-.5,.125,-.0625};
    for(bool scaled:{false,true})for(bool second:{false,true}) {
        const double q=scaled?rkl::evaluate_recursive_rkl_component<true>(0.,0.,0.,
            0x1p-500,0.,dyadic,second,.5):rkl::evaluate_recursive_rkl_component<false>(
            0.,0.,0.,0x1p-500,0.,dyadic,second,.5);
        require(bits(q)==bits(scaled?0x1p-503:0x1p-504),"tiny nonzero RKL RHS mistaken for stationary");
    }
    {
        const FluidVector rho_one{1.,0.,0.,0.,2.};const double x[]{0.,1.},sx[]{0x1p-500,-0x1p-500};
        double outx[2]{};FluidVector out;
        rkl::apply_first_rkl_stage_cell(rho_one,x,FluidVector{},sx,2,1,.125,out,outx);
        require(bits(outx[0])==bits(0x1p-503),"tiny represented species RHS was erased");
    }
    // First-stage coefficient and genuinely used recursive controls must not
    // hide IEEE nonfinite arithmetic behind the stationary fast path.
    for(double bad:{nan,inf}) {
        FluidVector out;double outx[2]{};
        rkl::apply_first_rkl_stage_cell(seed,fraction.data(),FluidVector{},zero_species.data(),
            2,1,bad,out,outx);
        require(!std::isfinite(out.rho)&&!std::isfinite(outx[0]),
            "nonfinite first RKL coefficient masked by stationary identity");
        for(bool scaled:{false,true})for(bool second:{false,true})for(int field=0;field<4;++field) {
            if(field==3&&!second)continue; // gamma is genuinely unused for RKL1
            auto c=dyadic;
            constexpr std::array<double DiffFunction::RKLCoeffs::*,4> controls{
                &DiffFunction::RKLCoeffs::mu,&DiffFunction::RKLCoeffs::nu,
                &DiffFunction::RKLCoeffs::tilde_mu,&DiffFunction::RKLCoeffs::gamma};
            c.*controls[field]=bad;
            const double q=scaled?rkl::evaluate_recursive_rkl_component<true>(1.,1.,1.,0.,0.,c,second,.5)
                :rkl::evaluate_recursive_rkl_component<false>(1.,1.,1.,0.,0.,c,second,.5);
            require(!std::isfinite(q),"nonfinite used RKL coefficient masked by stationary identity");
        }
        for(bool second:{false,true}) {
            require(!std::isfinite(rkl::evaluate_recursive_rkl_component<false>(
                1.,1.,1.,0.,0.,dyadic,second,bad)),"nonfinite used RKL dt masked");
            require(bits(rkl::evaluate_recursive_rkl_component<true>(1.,1.,1.,0.,0.,
                dyadic,second,bad))==bits(1.),"scaled RKL inspected mathematically unused dt");
        }
        auto first_order=dyadic;first_order.gamma=bad;
        for(bool scaled:{false,true}) {
            const double q=scaled?rkl::evaluate_recursive_rkl_component<true>(seed.rho,seed.rho,
                seed.rho,0.,bad,first_order,false,bad):rkl::evaluate_recursive_rkl_component<false>(
                seed.rho,seed.rho,seed.rho,0.,bad,first_order,false,.5);
            require(bits(q)==bits(seed.rho),"RKL1 unused gamma/initial RHS was wrongly rejected");
        }
        require(!std::isfinite(rkl::evaluate_recursive_rkl_component<false>(
            bad,bad,bad,0.,0.,dyadic,true,.5)),"nonfinite used RKL state gained stationary validity");
        require(!std::isfinite(rkl::evaluate_recursive_rkl_component<false>(
            1.,1.,1.,bad,0.,dyadic,true,.5)),"nonfinite used RKL RHS gained stationary validity");
        require(!std::isfinite(rkl::evaluate_recursive_rkl_component<false>(
            1.,1.,1.,0.,bad,dyadic,true,.5)),"nonfinite used RKL2 initial RHS gained stationary validity");
    }
    // All active operations are dyadic and exactly representable. These fixed
    // hexadecimal vectors come from independent rational evaluation of the
    // original affine scheme, making them insensitive to helper regrouping.
    const FluidVector n{8.,2.,-4.,6.,16.},p{10.,4.,-2.,8.,18.},o{6.,0.,-6.,4.,14.};
    const FluidVector lp{2.,-4.,8.,-12.,32.},ln{-2.,4.,-8.,12.,-16.};
    const DiffFunction::RKLCoeffs active{1.5,-.5,.25,-.125};
    const std::array<FluidVector,4> expected{{
        {0x1.88p+3,0x1.6p+2,0x1p+0,0x1.1p+3,0x1.8p+4},
        {0x1.8cp+3,0x1.5p+2,0x1.8p+0,0x1.fp+2,0x1.9p+4},
        {0x1.9p+3,0x1.4p+2,0x1p+1,0x1.cp+2,0x1.cp+4},
        {0x1.98p+3,0x1.2p+2,0x1.8p+1,0x1.6p+2,0x1.ep+4}}};
    {
        FluidVector alias=n;double x[]{.25,.75};const double sp[]{.375,1.625};
        rkl::apply_first_rkl_stage_cell(alias,x,lp,sp,2,1,.125,alias,x);
        equal_fluid(alias,{0x1.08p+3,0x1.8p+0,-0x1.8p+1,0x1.2p+2,0x1.4p+4});
        require(bits(x[0])==bits(131./528.)&&bits(x[1])==bits(397./528.),
            "active first RKL original rhoXi quotient/alias changed");
    }
    // RKL1's initial operator and gamma are mathematical non-inputs, including
    // fractions. Both public scaled/unscaled wrappers must retain the exact
    // stationary polynomial, without borrowing a new validity helper API.
    for(bool scaled:{false,true}) {
        auto c=active;c.gamma=inf;FluidVector alias=seed;auto x=fraction;
        const FluidVector unused{nan,nan,nan,nan,nan};const double sx[]{nan,inf};
        rkl::apply_recursive_rkl_stage_cell(seed,fraction.data(),seed,fraction.data(),
            alias,x.data(),FluidVector{},zero_species.data(),unused,sx,2,1,c,false,
            scaled?nan:.5,scaled,alias,x.data());
        equal_fluid(alias,seed);
        for(int species=0;species<2;++species)require(bits(x[species])==bits(fraction[species]),
            "RKL1 unused initial species operator poisoned stationary Xi");
    }
    int reference=0;
    for(bool scaled:{false,true})for(bool second:{false,true}) {
        FluidVector alias=o;const double xn[]{.25,.75},xp[]{.5,.5};double xo[]{.125,.875};
        const double sp[]{.375,1.625},sn[]{-.25,-1.75};
        rkl::apply_recursive_rkl_stage_cell(n,xn,p,xp,alias,xo,lp,sp,ln,sn,2,1,
            active,second,.5,scaled,alias,xo);
        equal_fluid(alias,expected[reference++]);
        // Conservation for each active species uses a separate exact rational
        // source table, with rational-to-double conversion at publication only.
        const std::array<std::array<double,2>,4> exact_x{{
            {459./784.,325./784.},{115./198.,83./198.},
            {231./400.,169./400.},{29./51.,22./51.}}};
        for(int s=0;s<2;++s)require(bits(xo[s])==bits(exact_x[reference-1][s]),
            "active RKL rhoXi quotient or older-alias arithmetic changed");
        const auto scalar=scaled?rkl::evaluate_recursive_rkl_hydro_cell<true>(n,p,o,lp,ln,active,second,.5)
            :rkl::evaluate_recursive_rkl_hydro_cell<false>(n,p,o,lp,ln,active,second,.5);
        equal_fluid(alias,scalar);
    }
    std::cout<<"RKL_STATIONARY_CELL_REFERENCE checked_nonbinary_seed=true active_hex=true alias=true physics_qualified=false\n";
}

/** Exercise the actual Host AMR scaled adapter with output==older, not only
 * the cell wrapper. Logical halos, padding, ENUC and source leases are fixed.
 * It is a stage-arithmetic fixture, not BC/EOS or full evolution qualification.
 */
void check_rkl_stationary_host_alias_reference() {
    Grid grid(2,0.,16.,0.,1.,0.,1.);grid.dim=1;grid.InitializeTopology();
    const int total=grid.GetTotalSize();
    FluidState n;n.Preallocate(total);n.InitSpecies(2);
    const FluidVector seed{std::nextafter(1.e7,0.),.13,-.27,.41,1.e8};
    for(int i=0;i<total;++i){n.set(i,seed);n.X(0,i)=.37;n.X(1,i)=1.-.37;n.enuc_rate[i]=17.+i;}
    FluidState p=n,older=n;
    std::vector<FluidVector> rhs(total,FluidVector{}),initial(total,FluidVector{});
    std::vector<double> sx(2*total,0.);
    for(int i=grid.Is();i<grid.Ie();++i)rhs[grid.GetIndex(i,grid.Js(),grid.Ks())].eng=2.;
    const auto bits=[](double q){return std::bit_cast<std::uint64_t>(q);};
    const auto arrays=[](const FluidState& q){return std::array<const double*,7>{q.rho.data(),q.mom_u.data(),
        q.mom_v.data(),q.mom_w.data(),q.eng.data(),q.mass_fractions.data(),q.enuc_rate.data()};};
    const auto n_addresses=arrays(n),p_addresses=arrays(p);
    for(bool second:{false,true}) {
        older=n;const auto addresses=arrays(older);
        const auto c=DiffFunction::get_rkl_coeffs(second?DiffFunction::RKLOrder::Second:DiffFunction::RKLOrder::First,2,5);
        Numerics::Diffusion::detail::apply_recursive_rkl_stage(n,p,older,older,rhs,sx,initial,sx,grid,c,second);
        require(arrays(older)==addresses,"RKL Host older-alias replaced an owned allocation");
        for(int i=0;i<total;++i) {
            const bool active=i>=grid.GetIndex(grid.Is(),grid.Js(),grid.Ks())&&i<grid.GetIndex(grid.Ie(),grid.Js(),grid.Ks());
            require(bits(older.rho[i])==bits(seed.rho)&&bits(older.mom_u[i])==bits(seed.mom_u)
                &&bits(older.mom_v[i])==bits(seed.mom_v)&&bits(older.mom_w[i])==bits(seed.mom_w),
                "Host scaled RKL stationary conserved field changed");
            require(bits(older.X(0,i))==bits(.37)&&bits(older.X(1,i))==bits(1.-.37)
                &&bits(older.enuc_rate[i])==bits(n.enuc_rate[i]),"Host RKL stationary Xi/ENUC changed");
            if(active)require(older.eng[i]!=seed.eng,"Host RKL active E RHS was lost");
            else require(bits(older.eng[i])==bits(seed.eng),"Host RKL changed a logical halo/padding energy");
        }
    }
    require(arrays(n)==n_addresses&&arrays(p)==p_addresses,"Host RKL replaced a const source allocation");
    for(int i=0;i<total;++i)for(const auto* source:{&n,&p}) {
        for(auto member:std::array<double FluidVector::*,5>{&FluidVector::rho,&FluidVector::mom_u,
                &FluidVector::mom_v,&FluidVector::mom_w,&FluidVector::eng})
            require(bits(source->get(i).*member)==bits(seed.*member),"Host RKL mutated a const source value");
        require(bits(source->X(0,i))==bits(.37)&&bits(source->X(1,i))==bits(1.-.37)
            &&bits(source->enuc_rate[i])==bits(17.+i),"Host RKL mutated source Xi/ENUC");
    }
    auto alias=n;const auto addresses=arrays(alias);
    Numerics::Diffusion::detail::apply_first_rkl_stage(alias,alias,rhs,sx,grid,.125);
    require(arrays(alias)==addresses,"first Host RKL alias replaced an owned allocation");
    for(int i=0;i<total;++i)require(bits(alias.rho[i])==bits(seed.rho)
        &&bits(alias.X(0,i))==bits(.37)&&bits(alias.X(1,i))==bits(1.-.37),
        "first Host RKL alias changed stationary rho/Xi");
}
/** Reject stale output diagnostics through the actual Host stage wrapper.
 * The burn rate is inherited from the input, independently of RK weights and
 * geometry. Distinct old/output sentinels expose accidental slot provenance;
 * logical halos and padding remain the existing boundary owner's responsibility.
 */
void check_hydro_diagnostic_input() {
    const auto bits=[](double value){return std::bit_cast<std::uint64_t>(value);};
    for(const auto semantics:{GridMetrics::GeometrySemantics::Existing,
            GridMetrics::GeometrySemantics::AxisymmetricRz}) {
        Grid grid(amr::MAX_NG,1.,2.,-1.,1.,0.,1.);
        grid.dim=semantics==GridMetrics::GeometrySemantics::Existing?1:2;
        grid.geometry=grid.dim==1?"cartesian":"cylindrical";
        grid.InitializeTopology(semantics);
        const int extent=grid.GetTotalSize();
        FluidState old,input,output;
        for(auto* state:{&old,&input,&output}) {
            state->Preallocate(extent);state->InitSpecies(0);
            for(int cell=0;cell<extent;++cell)state->set(cell,{2.,0.,0.,0.,8.});
        }
        std::vector<FluidVector> delta(extent);
        const std::vector<double> species_delta;
        for(int cell=0;cell<extent;++cell) {
            old.enuc_rate[cell]=-9000.-cell;
            input.enuc_rate[cell]=cell&1?7000.+cell:-0.;
        }
        for(const auto method:{arch::scheduler::HydroMethod::Euler,
                arch::scheduler::HydroMethod::RK2,arch::scheduler::HydroMethod::RK3}) {
            for(const auto& stage:arch::scheduler::make_hydro_plan(method).stages) {
                std::fill(output.enuc_rate.begin(),output.enuc_rate.end(),12345.);
                TimeIntegration::perform_stage_update(old,input,output,delta,species_delta,
                    grid,stage.old_weight,stage.update_weight,1e-14,1e-14,1e10,semantics);
                for(int cell=0;cell<extent;++cell) {
                    const int k=cell/grid.stride_z;
                    const int j=(cell-k*grid.stride_z)/grid.stride_y;
                    const int i=cell-k*grid.stride_z-j*grid.stride_y;
                    const bool active=i>=grid.Is()&&i<grid.Ie()
                        &&j>=grid.Js()&&j<grid.Je()&&k>=grid.Ks()&&k<grid.Ke();
                    require(bits(output.enuc_rate[cell])==bits(active?input.enuc_rate[cell]:12345.),
                        "Host Hydro diagnostic used stale/weighted output or touched its halo/padding");
                    require(bits(input.enuc_rate[cell])==bits(cell&1?7000.+cell:-0.)
                        &&bits(old.enuc_rate[cell])==bits(-9000.-cell),
                        "Host Hydro diagnostic inheritance mutated an input");
                }
            }
        }
    }
}

/** Independent RK polynomial witnesses through the public shared cell update.
 * Workflow: apply real scheduler weights to L(U)=0; check exact conserved/Xi
 * publication; then use dyadic source data and rational endpoint references.
 * These cell checks do not certify a full evolution or native thermal closure.
 */
void check_rk_conserved_polynomial_reference() {
    const auto bits=[](double x){return std::bit_cast<std::uint64_t>(x);};
    const std::array<double FluidVector::*,5> components{
        &FluidVector::rho,&FluidVector::mom_u,&FluidVector::mom_v,
        &FluidVector::mom_w,&FluidVector::eng};
    const auto exact=[&](const FluidVector& value,const FluidVector& expected,const char* message){
        for(const auto member:components)require(bits(value.*member)==bits(expected.*member),message);
    };
    const double q=0x1.0000000000001p+0;
    const FluidVector seed{q,q,-2.*q,.5*q,16.*q},zero{};
    const std::array<double,2> fraction{.5,.5};
    for(const auto method:{arch::scheduler::HydroMethod::Euler,
            arch::scheduler::HydroMethod::RK2,arch::scheduler::HydroMethod::RK3}) {
        const auto plan=arch::scheduler::make_hydro_plan(method);
        FluidVector current=seed;auto current_fraction=fraction;
        for(const auto& stage:plan.stages) {
            FluidVector output;std::array<double,2> output_fraction{},species_delta{};
            const auto status=TimeIntegration::update_stage_cell(seed,current,zero,
                fraction.data(),current_fraction.data(),species_delta.data(),2,1,
                stage.old_weight,stage.update_weight,1e-30,1e-30,1e300,
                output,output_fraction.data());
            require(status==arch::state::Status::valid,"RK stationary polynomial required a repair/rejection");
            exact(output,seed,"RK stationary polynomial changed a represented conserved component");
            for(int s=0;s<2;++s)require(bits(output_fraction[s])==bits(fraction[s]),
                "RK stationary polynomial changed an exact binary fraction");
            current=output;current_fraction=output_fraction;
        }
    }
    // Euler ignores old: conserved endpoint (4,1,2,3,64)+(2,2,-1,.5,8).
    const FluidVector old_state{4.,1.,-2.,.5,64.},current{6.,-1.,4.,1.5,80.};
    const std::array<double,2> old_x{.25,.75},current_x{.5,.5};
    FluidVector output;std::array<double,2> output_x{};
    const FluidVector euler_input{4.,1.,2.,3.,64.},euler_delta{2.,2.,-1.,.5,8.};
    const std::array<double,2> euler_species{.5,1.5};
    auto status=TimeIntegration::update_stage_cell(old_state,euler_input,euler_delta,
        current_x.data(),old_x.data(),euler_species.data(),2,1,0.,1.,
        1e-30,1e-30,1e300,output,output_x.data());
    require(status==arch::state::Status::valid,"Euler independent nonzero source rejected");
    exact(output,{6.,3.,1.,3.5,72.},"Euler independent dyadic source endpoint differs");
    require(bits(output_x[0])==bits(.25)&&bits(output_x[1])==bits(.75),
        "Euler source did not preserve independent species/mass pairing");
    // Heun endpoint averages old with current+delta. Species masses are
    // (1+4)/2=5/2 and (3+4)/2=7/2, at rho=6: Xi=(5/12,7/12).
    const FluidVector delta{2.,3.,-2.,.5,8.};
    const std::array<double,2> species_delta{1.,1.};
    status=TimeIntegration::update_stage_cell(old_state,current,delta,
        old_x.data(),current_x.data(),species_delta.data(),2,1,.5,.5,
        1e-30,1e-30,1e300,output,output_x.data());
    require(status==arch::state::Status::valid,"RK independent nonzero source rejected");
    exact(output,{6.,1.5,0.,1.25,76.},"RK independent rational conserved endpoint differs");
    require(bits(output_x[0])==bits(5./12.)&&bits(output_x[1])==bits(7./12.),
        "RK independent rational species endpoint differs");
    // A represented convex mean can survive even when curr-old overflows.
    // Native provisional finite/rho checks here are not a point-EOS certificate.
    const FluidVector wide_old{2.,-1e308,0.,0.,8.},wide_current{2.,1e308,0.,0.,8.};
    status=TimeIntegration::update_stage_cell(wide_old,wide_current,zero,
        nullptr,nullptr,nullptr,0,1,.5,.5,1e-30,1e-30,1e300,
        output,nullptr,{},1.,0,true);
    require(status==arch::state::Status::valid,"represented convex endpoint rejected after difference overflow");
    exact(output,{2.,0.,0.,0.,8.},"range-protected convex endpoint differs from exact cancellation");
    const double invalid=std::numeric_limits<double>::quiet_NaN();
    const double infinity=std::numeric_limits<double>::infinity();
    for(const auto weights:std::array<std::array<double,2>,6>{{
            {invalid,.5},{.5,infinity},{-.25,1.25},{1.25,-.25},{.25,.5},{0.,0.}}}) {
        status=TimeIntegration::update_stage_cell(seed,seed,zero,
            nullptr,nullptr,nullptr,0,1,weights[0],weights[1],1e-30,1e-30,1e300,output,nullptr);
        require(!arch::state::accepted(status),"RK malformed weights gained a stationary publication");
    }
    // Finite weights cannot hide a nonfinite required Euler trial.
    const FluidVector huge{2.,0.,0.,0.,1e308},huge_delta{0.,0.,0.,0.,1e308};
    status=TimeIntegration::update_stage_cell(seed,huge,huge_delta,
        nullptr,nullptr,nullptr,0,1,.5,.5,1e-30,1e-30,1e300,output,nullptr);
    require(!arch::state::accepted(status),"RK weighted publication concealed nonfinite curr+delta");
    std::cout<<"RK_CONSERVED_POLYNOMIAL_REFERENCE stationary_methods=3 rational_updates=2 range=1\n";
}


/** Test active complementary species with the real RKL cell adapters.
 * Workflow: gather the closed two-cell operator before any aliased stage write,
 * evolve both cells with the production coefficients, then compare the complete
 * step to independent low-degree Legendre stability polynomials. The operator
 * is L(X)_c=lambda*(X_other-X_c), with no BC, Hydro or normalization shortcut.
 * Its antisymmetric eigenvalue is -2*lambda; the symmetric mass mode is zero.
 * This arithmetic fixture does not qualify the complete DiffusionMode 5tau run.
 */
void check_rkl_active_complementary_reference() {
    namespace rkl=Numerics::Diffusion::detail;
    using Fractions=std::array<std::array<double,2>,2>;
    using Cells=std::array<FluidVector,2>;
    constexpr double lambda=1.;
    constexpr double dt=0x1p-12;
    constexpr int steps=25000;
    constexpr double window=512.*2.*std::numeric_limits<double>::epsilon();
    const long double z=-2.L*static_cast<long double>(lambda)*dt;
    const Fractions initial{{{{.37,.63}},{{.63,.37}}}};
    const std::array<long double,2> initial_mass{
        static_cast<long double>(initial[0][0])+initial[1][0],
        static_cast<long double>(initial[0][1])+initial[1][1]};
    const long double mean=(static_cast<long double>(initial[0][0])+initial[1][0])/2.L;
    const long double initial_amplitude=(static_cast<long double>(initial[1][0])-initial[0][0])/2.L;
    const auto bits=[](double value){return std::bit_cast<std::uint64_t>(value);};
    const Cells fluid_seed{{{1.,0.,0.,0.,4.},{1.,0.,0.,0.,4.}}};
    // Independent exact polynomial coefficients from P2 and P3, not the
    // producer's recurrence weights. dt and lambda are exact binary inputs.
    const auto amplification=[&](bool second,int stages) {
        if(second) return stages==2 ? 1.L+z+z*z/2.L
            : 1.L+z+z*z/2.L+z*z*z/15.L;
        return stages==2 ? 1.L+z+z*z/6.L
            : 1.L+z+5.L*z*z/24.L+5.L*z*z*z/432.L;
    };
    for(bool scaled:{false,true}) for(bool second:{false,true}) for(int stages:{2,3}) {
        const auto order=second?DiffFunction::RKLOrder::Second:DiffFunction::RKLOrder::First;
        const long double decay=amplification(second,stages);
        require(decay>0.L&&decay<1.L,"independent RKL closed-mode polynomial is unstable");
        Cells current=fluid_seed;
        Fractions current_x=initial;
        long double amplitude=initial_amplitude;
        for(int step=0;step<steps;++step) {
            const Cells state_n=current;
            const Fractions species_n=current_x;
            // All genuine operator inputs are gathered from the immutable
            // stage frame. Both species and both cells are computed separately.
            const auto rhs=[&](const Cells& u,const Fractions& x) {
                Fractions result{};
                for(int cell=0;cell<2;++cell) for(int species=0;species<2;++species)
                    result[cell][species]=lambda*(u[1-cell].rho*x[1-cell][species]
                                                -u[cell].rho*x[cell][species]);
                return result;
            };
            const Fractions initial_rhs=rhs(state_n,species_n);
            require(initial_rhs[0][0]!=0.&&initial_rhs[0][1]!=0.,
                "active RKL complementary witness became a stationary test");
            Cells older=state_n,previous=state_n;
            Fractions older_x=species_n,previous_x=species_n;
            for(int stage=1;stage<=stages;++stage) {
                const auto coefficients=DiffFunction::get_rkl_coeffs(order,stage,stages);
                const Fractions previous_rhs=rhs(previous,previous_x);
                Fractions used_previous=previous_rhs,used_initial=initial_rhs;
                if(scaled) for(int cell=0;cell<2;++cell) for(int species=0;species<2;++species) {
                    used_previous[cell][species]*=dt;
                    used_initial[cell][species]*=dt;
                }
                if(stage==1) {
                    for(int cell=0;cell<2;++cell)
                        rkl::apply_first_rkl_stage_cell(state_n[cell],species_n[cell].data(),
                            FluidVector{},used_initial[cell].data(),2,1,
                            scaled?coefficients.tilde_mu:coefficients.tilde_mu*dt,
                            previous[cell],previous_x[cell].data());
                } else {
                    // Output really aliases older. The next stage sees all
                    // outputs only after the two-cell gather and writes finish.
                    for(int cell=0;cell<2;++cell)
                        rkl::apply_recursive_rkl_stage_cell(state_n[cell],species_n[cell].data(),
                            previous[cell],previous_x[cell].data(),older[cell],older_x[cell].data(),
                            FluidVector{},used_previous[cell].data(),FluidVector{},used_initial[cell].data(),
                            2,1,coefficients,second,dt,scaled,older[cell],older_x[cell].data());
                    std::swap(previous,older);
                    std::swap(previous_x,older_x);
                }
                for(int cell=0;cell<2;++cell) {
                    require(arch::state::validate_composition(previous_x[cell].data(),2,1)
                        ==arch::state::Status::valid,"active RKL complementary simplex rejected");
                    for(auto member:std::array<double FluidVector::*,5>{&FluidVector::rho,
                            &FluidVector::mom_u,&FluidVector::mom_v,&FluidVector::mom_w,&FluidVector::eng})
                        require(bits(previous[cell].*member)==bits(fluid_seed[cell].*member),
                            "passive active-species RKL changed stationary fluid");
                }
            }
            current=previous;
            current_x=previous_x;
            amplitude*=decay;
            for(int species=0;species<2;++species) {
                const long double mass=static_cast<long double>(current_x[0][species])+current_x[1][species];
                require(std::abs(mass-initial_mass[species])<=window*std::abs(initial_mass[species]),
                    "active closed RKL changed global species mass");
            }
            for(int cell=0;cell<2;++cell) for(int species=0;species<2;++species) {
                const long double tracer=mean+(cell?amplitude:-amplitude);
                const long double expected=species==0?tracer:initial_mass[0]-tracer;
                require(std::abs(static_cast<long double>(current_x[cell][species])-expected)<=window,
                    "active RKL decay differs from independent stability polynomial");
            }
        }
    }
    std::cout<<"RKL_ACTIVE_COMPLEMENTARY_REFERENCE routes=8 steps=25000 cells=2 species=2 "
        "simplex=core-window decay=independent-polynomial scope=cell-arithmetic-only full_5tau=false\n";
}

}

int main() {
    try { check_hydro_diagnostic_input(); check_rk_conserved_polynomial_reference(); check_rkl_stationary_cell_reference(); check_rkl_stationary_host_alias_reference(); check_rkl_active_complementary_reference(); check_native_weighted_component_underflow(); leaves(); ThermalFiveDecayCases::run(); std::cout << "Low-density analytic leaves passed through rho=1e-100\n"; }
 catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
