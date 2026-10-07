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
#include "numerics/diffusion/DiffFlux.h"
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
    // density reader; the existing legacy diffusion regressions remain below.
    auto existing=geometry;existing.semantics=GridMetrics::GeometrySemantics::Existing;
    int reads=0;const auto counting_read=[&](int index){++reads;return field.get(index);};
    const auto legacy=DiffFlux::diffusion_thermal_input(raw,counting_read,existing,cell,i);
    require(legacy.valid&&reads==0&&legacy.state.rho==raw.rho&&legacy.state.mom_w==raw.mom_w
        &&legacy.state.eng==raw.eng,"legacy diffusion thermal convention changed");
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
    auto ordinary=geometry;ordinary.semantics=GridMetrics::GeometrySemantics::Existing;
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

void leaves() {
    native_rz_angular_velocity_underflow();
    native_rz_stage_prechecks();
    native_rz_diffusion_thermodynamics();
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

int main() {
    try { check_native_weighted_component_underflow(); leaves(); std::cout << "Low-density analytic leaves passed through rho=1e-100\n"; }
 catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
