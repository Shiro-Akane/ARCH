// Scale invariance and independent analytic references; no tolerance has units.
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
void leaves() {
    native_rz_stage_prechecks();
    native_rz_diffusion_thermodynamics();
    native_rz_coarsening_thermal_reference();
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
int main() { try { leaves(); std::cout << "Low-density analytic leaves passed through rho=1e-100\n"; }
 catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
