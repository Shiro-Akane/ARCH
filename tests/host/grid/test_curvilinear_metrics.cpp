/**
 * @file test_curvilinear_metrics.cpp
 * @brief Check shared geometry and source terms against analytic measures.
 *
 * The cases span Cartesian, cylindrical and spherical coordinates, including
 * thin shells, poles and radial-origin behavior.
 */
#include "physics/boundary/BoundaryDiagnostics.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/DriverUtils.h"
#include "numerics/curvilinear/GeometricSources.h"
#include "numerics/integrator/HydroSolverImpl.h"
#include "numerics/integrator/HydroGeometryBinding.h"
#include "numerics/integrator/TimeIntegratorEuler.h"
#include "numerics/integrator/TimeIntegratorRK2.h"
#include "numerics/integrator/TimeIntegratorRK3.h"
#include "numerics/reconstruction/Reconstruction.h"
#include "numerics/flux/FluxHLLC.h"
#include "math/geometry/CurvilinearMetricCases.h"
#include "math/geometry/ViscousGeometryCases.h"
#include "math/geometry/RadialStokesCases.h"
#include "math/geometry/CurvedStokesCases.h"
#include "numerics/diffusion/DiffFlux.h"
#include "physics/eos/IdealGas.h"
#include "physics/gravity/ExternalGravity.h"
#include "physics/diagnostics/VelocityDiagnostics.h"
#include <iostream>
#include <cfenv>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>
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

struct PressurelessEos {
    double get_pressure(const FluidVector&, const double*) const { return 0.0; }
    double get_sound_speed(const FluidVector&, double, const double*) const { return 1.0; }
};

/** Exercise index limits without allocating enormous synthetic species arrays. */
void test_species_storage_bounds(const IdealGas& eos)
{
    const auto rejects = [](auto action, const char* message) {
        try { action(); }
        catch (const std::invalid_argument&) { return; }
        catch (const std::length_error&) { return; }
        throw std::runtime_error(message);
    };
    constexpr int maximum = std::numeric_limits<int>::max();
    if (FluidState::CheckedSpeciesSize(0, 0) != 0 ||
        FluidState::CheckedSpeciesSize(0, maximum) != 0 ||
        FluidState::CheckedSpeciesSize(maximum, 1) != static_cast<std::size_t>(maximum))
        throw std::runtime_error("species size rejected a representable or empty extent");
    rejects([] { (void)FluidState::CheckedSpeciesSize(-1, 1); }, "negative species count accepted");
    rejects([] { (void)FluidState::CheckedSpeciesSize(1, -1); }, "negative cell count accepted");
    rejects([] { (void)FluidState::CheckedSpeciesSize(2, maximum / 2 + 1); },
            "species product outside the int index range accepted");

    FluidState small; small.Preallocate(8); small.InitSpecies(2);
    small.X(1, 7) = .75;
    const auto fractions = small.mass_fractions;
    rejects([&] { small.InitSpecies(-1); }, "negative species initialization accepted");
    rejects([&] { small.InitSpecies(maximum); }, "overflowing species initialization accepted");
    rejects([&] { small.Preallocate(-1); }, "negative field allocation accepted");
    rejects([&] { small.Preallocate(maximum); }, "overflowing existing-species resize accepted");
    if (small.n_species_ != 2 || small.block_total_size_ != 8 || small.rho.size() != 8 ||
        small.mass_fractions != fractions || small.X(1, 7) != .75)
        throw std::runtime_error("rejected species extent mutated existing storage");
    FluidState empty; empty.InitSpecies(maximum);
    rejects([&] { empty.Preallocate(2); }, "deferred species extent bypassed allocation bounds");
    if (empty.block_total_size_ != 0 || !empty.rho.empty())
        throw std::runtime_error("rejected deferred extent allocated fields");

    Grid grid(amr::MAX_NG, 0., 1., 0., 0., 0., 0.);
    FluidState state; state.Preallocate(grid.GetTotalSize()); state.InitSpecies(1);
    Numerics::HydroSolverImpl<IdealGas, FluxHLLC<PCMReconstruction>> hydro(eos);
    std::vector<FluidVector> delta(1, {1., 2., 3., 4., 5.});
    std::vector<double> species_delta{6.};
    NumericsConfig config{};
    const auto evaluate = [&] {
        hydro.evaluate_patch(nullptr, 0, state, grid, .1, delta, species_delta, nullptr, config);
    };
    state.n_species_ = -1;
    rejects(evaluate, "Hydro accepted a negative species count");
    state.n_species_ = maximum;
    rejects(evaluate, "Hydro accepted an overflowing species extent");
    state.n_species_ = 1; --state.block_total_size_;
    rejects(evaluate, "Hydro accepted a state/Grid stride mismatch");
    ++state.block_total_size_; state.mass_fractions.pop_back();
    rejects(evaluate, "Hydro accepted undersized species storage");
    if (delta.size() != 1 || delta[0].eng != 5. || species_delta != std::vector<double>{6.})
        throw std::runtime_error("rejected Hydro extent wrote derivatives");
    std::cout << "SPECIES_STORAGE_BOUNDS_PASS\n";
}

/** Native geometry is inspectable while unqualified public solvers stay closed. */
void test_axisymmetric_geometry()
{
    using namespace GridMetrics;
    Grid grid(amr::MAX_NG,0.,1.,-8.,8.,0.,0.,1,1,0);
    grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology();
    const auto view=make_geometry_view(grid);
    const auto layout=resolve_coordinate_layout(view.geometry,2);
    if(!layout.valid || layout.basis!=CoordinateLayout::Basis::CylindricalRZPhi ||
       layout.radial!=0 || layout.axial!=1 || layout.azimuth!=2 ||
       layout.resolved_derivatives!=std::array<bool,3>{true,true,false} ||
       grid.dx3!=0. || grid.GetTotalZ()!=1 || grid.Ks()!=0 || grid.Ke()!=1 ||
       grid.GetAxisNames()!=std::vector<std::string>{"r_cy","z_cy"})
        throw std::runtime_error("axisymmetric layout/inactive storage");
    const auto fragment=make_geometry_view(Geometry::Cylindrical,2,{0.,-8.,0.},{grid.dx1,grid.dx2,0.});
    close(CellVolume(fragment,0,0,0),CellVolume(view,grid.Is(),grid.Js(),0),"native/fragment geometry agreement");
    for(const auto rz: {std::array<double,2>{3.,-4.}, {1.e-100,0.}, {0.,-4.}, {0.,0.}}) {
        const auto p=Grid::PhysicalCoordsFromNative(2,"cylindrical",rz[0],rz[1],99.);
        const auto x=PhysicalPosition(Geometry::Cylindrical,2,{rz[0],rz[1],99.});
        if(p.x!=rz[0] || p.y!=0. || p.z!=rz[1] || p.phi!=0. ||
           p.phi_cy!=0. || p.r_cy!=rz[0] || p.z_cy!=rz[1] ||
           x!=std::array<double,3>{rz[0],0.,rz[1]})
            throw std::runtime_error("axisymmetric representative point");
        if(rz[0]==3.)close(p.r,5.,"native spherical radius");
        if(rz[0]==1.e-100)close(p.theta,arch::constants::math::pi/2.,"small native polar angle must not be floored");
    }
    for(int direction=0;direction<2;++direction)for(auto side:{arch::boundary::BoundarySide::Lower,arch::boundary::BoundarySide::Upper}) {
        const auto face=Grid::PhysicalCoordsFromNative(2,"cylindrical",.5,-3.);
        const auto normal=arch::boundary::BoundaryCartesianNormal(grid,face,
            direction==0?arch::boundary::BoundaryAxis::X1:arch::boundary::BoundaryAxis::X2,side);
        const double sign=side==arch::boundary::BoundarySide::Lower?-1.:1.;
        if(normal!=std::array<double,3>{direction==0?sign:0.,0.,direction==1?sign:0.})
            throw std::runtime_error("axisymmetric physical boundary normal");
    }
    if(FaceArea(view,0,grid.Is(),grid.Js(),0,false)!=0. ||
       AngularMomentumFaceMoment(view,0,grid.Is(),grid.Js(),0,false)!=0. ||
       PhysicalSpacing(view,2,grid.Is(),grid.Js())!=0. ||
       std::isfinite(AngularMomentumFaceMoment(view,2,grid.Is(),grid.Js(),0,false)))
        throw std::runtime_error("axis/inactive face moment");
    arch::boundary::HydroBoundaryView walls;walls.reflecting[0]=walls.reflecting[1]=true;
    for(int i=grid.Is();i<grid.Ie();++i) {
        bool lower=true,upper=true;
        if(!walls.advective_faces_open(0,i,grid.Is(),grid.Ie(),lower,upper)||
           lower!=(i!=grid.Is())||upper!=(i!=grid.Ie()-1))
            throw std::runtime_error("shared advective face identity");
    }
    bool lower=false,upper=true;
    if(walls.advective_faces_open(0,grid.Ie(),grid.Is(),grid.Ie(),lower,upper)||lower||!upper)
        throw std::runtime_error("invalid face identity silently became open");
    FluidVector source{};
    TimeIntegration::add_geometric_source_cell(FluidVector{2.,.6,-.4,1.4,20.},nullptr,
        ConstantEos{},view,grid.Is(),grid.Js(),.125,source);
    const double inverse_r=2./grid.dx1;
    close(source.mom_u,.125*(2.*.7*.7+5.)*inverse_r,"radial rotation source");
    if(source.mom_v!=0. || source.mom_w!=0. || source.rho!=0. || source.eng!=0.)
        throw std::runtime_error("torque divergence must not receive a second phi source");
    bool rejected=false;
    if(resolve_public_chart("cylindrical",2)!=Geometry::Cylindrical)
        throw std::runtime_error("native cylindrical chart identity");
    // Native no-gravity Hydro can now request ordinary boundary authority.
    // Public input/backend and checkpoint qualification retain their own gates.
    SpeciesManager species;species.add_species("gas",1.,1.,1.4,3.);
    IdealGas eos(1.4,species);
    Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> hydro(eos);
    amr::AMRControl control(4,2);
    SimConfig config{};config.grid.dim=2;config.grid.geometry="cartesian";
    config.grid.nblockx1=config.grid.nblockx2=1;config.grid.nblockx3=0;
    config.grid.x1_min=0.;config.grid.x1_max=1.;
    config.grid.x2_min=-1.;config.grid.x2_max=1.;
    control.tree->InitRootGrid(config,1);
    for(int id:control.tree->GetActiveBlocks())control.pool->GetBlock(id).grid.geometry="cylindrical";
    struct BoundaryProbe {
        mutable bool called=false;
        int logical_plan(const Grid&)const {called=true;return 0;}
    } boundary;
    rejected=false;
    try{TimeIntegration::require_hydro_geometry_domain(control,boundary,&hydro);}
    catch(const std::invalid_argument&){rejected=true;}
    if(rejected || !boundary.called)throw std::runtime_error("native Hydro preflight boundary");
    auto& block=control.pool->GetBlock(control.tree->GetActiveBlocks().front());
    const FluidVector input{2.,.4,-.6,.8,20.};
    for(int c=0;c<block.grid.GetTotalSize();++c) {
        block.fluid_state.set(c,input);block.fluid_state.X(0,c)=1.;
    }
    std::vector<FluidVector> off(block.grid.GetTotalSize()),on(off.size());
    std::vector<double> off_species(off.size()),on_species(off.size());
    Physical::Gravity::ExternalGravity optional_gravity(.3,-.2,.4);
    hydro.evaluate_patch(nullptr,block.id,block.fluid_state,block.grid,.1,
        off,off_species,nullptr,config.numerics);
    hydro.evaluate_patch(nullptr,block.id,block.fluid_state,block.grid,.1,
        on,on_species,&optional_gravity,config.numerics);
    for(int j=block.grid.Js();j<block.grid.Je();++j)for(int i=block.grid.Is();i<block.grid.Ie();++i) {
        const int c=block.grid.GetIndex(i,j,0);
        close(on[c].mom_u-off[c].mom_u,.1*input.rho*.3,"external radial impulse");
        close(on[c].mom_v-off[c].mom_v,-.1*input.rho*.2,"external axial impulse");
        close(on[c].mom_w-off[c].mom_w,.1*input.rho*.4,"external torque impulse");
        close(on[c].eng-off[c].eng,.1*(input.mom_u*.3-input.mom_v*.2+input.mom_w*.4),"external work");
        if(on[c].rho!=off[c].rho||on_species[c]!=off_species[c])
            throw std::runtime_error("external gravity changed mass/species");
    }
    // Unresolved swirl does not invent a third spatial acoustic direction.
    const FluidVector quiet{1.,0.,0.,0.,20.},swirl{1.,0.,0.,.1,20.005};
    if(evaluate_cfl_cell_dt(quiet,nullptr,ConstantEos{},view,grid.Is(),grid.Js())!=
       evaluate_cfl_cell_dt(swirl,nullptr,ConstantEos{},view,grid.Is(),grid.Js()))
        throw std::runtime_error("unresolved azimuth entered spatial CFL");
    for(double radial:{-3.,0.,3.}) {
        // Independent specific-energy reference: rho=2, e=3, uphi=20.
        const FluidVector fast{2.,2.*radial,0.,40.,6.+radial*radial+400.};
        const double bound=TimeIntegration::cylindrical_rotation_source_dt(fast,view,grid.Is());
        const long double a=400.L*inverse_r,impulse=a*bound;
        if(!std::isfinite(bound)||!(bound>0.)||std::abs(radial)*impulse+.5L*impulse*impulse>1.5L*(1.L+2.e-12L))
            throw std::runtime_error("centrifugal impulse consumed its thermal margin");
    }
    if(TimeIntegration::cylindrical_rotation_source_dt(quiet,view,grid.Is())!=std::numeric_limits<double>::max()||
       !std::isnan(TimeIntegration::cylindrical_rotation_source_dt(FluidVector{1.,0.,0.,20.,1.},view,grid.Is())))
        throw std::runtime_error("rotation timestep zero/invalid contract");
    for(int failure=0;failure<4;++failure) {
        Grid invalid=grid;
        if(failure==0)invalid.x1_min=-1.;
        if(failure==1)invalid.x2_max=invalid.x2_min;
        if(failure==2)invalid.x2_min=std::numeric_limits<double>::quiet_NaN();
        if(failure==3)invalid.x1_max=std::numeric_limits<double>::infinity();
        rejected=false;try{invalid.InitializeTopology();}catch(const std::invalid_argument&){rejected=true;}
        if(!rejected)throw std::runtime_error("invalid native axisymmetric domain accepted");
    }
    std::cout<<"AXISYMMETRIC_NATIVE_GEOMETRY_PASS\n";
}

/** Actual HLLC patch increments must reduce to the retained 1-D operator.
 * Axial contact/acoustic data are constant in r; radial data are constant in z.
 * This catches an invented phi derivative, an axial geometric source, changed
 * component ordering or an accidental V/W weight on ordinary rho/E/rhoX.
 */
void test_axisymmetric_one_dimensional_reductions()
{
    SpeciesManager species;species.add_species("X",1.,1.,1.4,1.);species.add_species("Y",1.,1.,1.4,1.);
    constexpr double gamma=1.4;IdealGas eos(gamma,species);NumericsConfig numerics{};numerics.hll_roe_wave_speed=true;
    Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> pcm(eos);
    Numerics::HydroSolverImpl<IdealGas,FluxHLLC<MusclReconstruction<McLimiter>>> muscl(eos);
    for(bool axial:{false,true})for(double inner:{0.,1.})for(bool acoustic:{false,true})
    for(const Numerics::IHydroSolver* hydro:{static_cast<const Numerics::IHydroSolver*>(&pcm),static_cast<const Numerics::IHydroSolver*>(&muscl)}) {
        Grid plane(amr::MAX_NG,inner,inner+1.,0.,1.,0.,0.,1,1,0);
        plane.dim=2;plane.geometry="cylindrical";plane.InitializeTopology();
        Grid line(amr::MAX_NG,axial?0.:inner,axial?1.:inner+1.,0.,0.,0.,0.,1,0,0);
        line.dim=1;line.geometry=axial?"cartesian":"cylindrical";line.InitializeTopology();
        FluidState states[2];const Grid* grids[]{&line,&plane};
        for(int n=0;n<2;++n) {
            const auto& grid=*grids[n];auto& state=states[n];state.Preallocate(grid.GetTotalSize());state.InitSpecies(2);
            for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
                const double x=n==1&&axial?grid.GetCellCenterY(j):grid.GetCellCenterX(i);
                const double wave=std::cos(2.*std::numbers::pi*x),rho=1.+.1*wave;
                const double velocity=axial?.12:.02*x;
                const double pressure=2.+(acoustic?.05*wave:0.);
                const int cell=grid.GetIndex(i,j,0);
                state.set(cell,{rho,n==1&&axial?0.:rho*velocity,n==1&&axial?rho*velocity:0.,0.,pressure/(gamma-1.)+.5*rho*velocity*velocity});
                state.X(0,cell)=.4+.03*wave;state.X(1,cell)=1.-state.X(0,cell);
            }
        }
        std::vector<FluidVector> delta[2];std::vector<double> composition[2];
        for(int n=0;n<2;++n) {
            delta[n].resize(grids[n]->GetTotalSize());composition[n].resize(2*grids[n]->GetTotalSize());
            hydro->evaluate_patch(nullptr,-1,states[n],*grids[n],.001,delta[n],composition[n],nullptr,numerics);
        }
        for(int j=plane.Js();j<plane.Je();++j)for(int i=plane.Is();i<plane.Ie();++i) {
            const int cell=plane.GetIndex(i,j,0),reference=line.GetIndex(axial?j:i,0,0);
            const auto actual=delta[1][cell],expected=delta[0][reference];
            close(actual.rho,expected.rho,"axisymmetric 1-D density reduction");
            close(actual.eng,expected.eng,"axisymmetric 1-D energy reduction");
            close(axial?actual.mom_v:actual.mom_u,expected.mom_u,"axisymmetric 1-D momentum reduction");
            close(axial?actual.mom_u:actual.mom_v,0.,"axisymmetric inactive momentum source");
            close(actual.mom_w,0.,"axisymmetric unresolved angular derivative");
            for(int sp=0;sp<2;++sp)close(composition[1][sp*plane.GetTotalSize()+cell],composition[0][sp*line.GetTotalSize()+reference],"axisymmetric 1-D rhoX reduction");
        }
    }
    std::cout<<"AXISYMMETRIC_ONE_DIMENSIONAL_REDUCTIONS_PASS\n";
}

/** Independent discrete angular-work identity for the native (r,z) stage.
 * One explicit mass/torque/radial update is compared with the angular kinetic
 * energy carried by the energy flux. The point oracle above stays untouched;
 * this exercises the separate work-adjoint quadrature and its wall closure.
 */
void test_axisymmetric_angular_work()
{
    using namespace GridMetrics;
    for (double lower : {0., 1., 2.5})
    for (double width : {.2, .05}) {
        GeometryView grid{};
        grid.geometry=Geometry::Cylindrical; grid.dim=2;
        grid.dx1=width; grid.dx2=.09; grid.dx3=0.;
        grid.x1_min=lower; grid.x2_min=-.3;
        const int i=0;
        const double volume=CellVolume(grid,i,0,0);
        const double moment=AngularMomentumVolumeMoment(grid,i,0,0);
        const double area_l=FaceArea(grid,0,i,0,0,false);
        const double area_r=FaceArea(grid,0,i,0,0,true);
        const double torque_l=AngularMomentumFaceMoment(grid,0,i,0,0,false);
        const double torque_r=AngularMomentumFaceMoment(grid,0,i,0,0,true);
        const double inverse_radius=InverseRadiusVolumeAverage(grid,i);
        const double open=TimeIntegration::axisymmetric_centrifugal_work_coefficient(
            grid,i,true,true);
        close(open,2.*volume/moment-inverse_radius,"work-adjoint open coefficient");
        if (lower>0.) {
            if (!(open<inverse_radius))
                throw std::runtime_error("open work coefficient exceeds point average");
            const double upper_only=TimeIntegration::axisymmetric_centrifugal_work_coefficient(
                grid,i,false,true);
            const double lower_only=TimeIntegration::axisymmetric_centrifugal_work_coefficient(
                grid,i,true,false);
            if (!(upper_only>0. && upper_only<inverse_radius)
                || !(lower_only>0. && lower_only<inverse_radius))
                throw std::runtime_error("wall work coefficient is not a positive face lever");
            if (std::abs(upper_only+lower_only-open)>2.e-12*open)
                throw std::runtime_error("open coefficient is not the two-face sum");
        }
        const double rho=1.3, radial_velocity=.21, angular_velocity=1.7;
        const double radial_mass_flux=rho*radial_velocity;
        auto work=[&](bool lower_open,bool upper_open,const char* name) {
            const double coefficient=TimeIntegration::axisymmetric_centrifugal_work_coefficient(
                grid,i,lower_open,upper_open);
            if (!std::isfinite(coefficient)) throw std::runtime_error(name);
            const double mass_l=lower_open?area_l*radial_mass_flux:0.;
            const double mass_r=upper_open?area_r*radial_mass_flux:0.;
            const double torque_lower=lower_open?torque_l*radial_mass_flux*angular_velocity:0.;
            const double torque_upper=upper_open?torque_r*radial_mass_flux*angular_velocity:0.;
            // Exact time derivative of K_phi=0.5*V*M_phi^2/rho under the
            // discrete mass and torque updates, independent of the source algebra.
            const double kinetic_rate=volume*angular_velocity
                *(torque_lower-torque_upper)/moment
                -.5*volume*angular_velocity*angular_velocity*(mass_l-mass_r)/volume;
            const double source_work=volume*radial_velocity*rho
                *angular_velocity*angular_velocity*coefficient;
            const double expected=.5*radial_mass_flux*angular_velocity*angular_velocity
                *((lower_open?area_l:0.)-(upper_open?area_r:0.));
            const double scale=std::max({1.,std::abs(source_work),std::abs(expected)});
            if (std::abs(kinetic_rate+source_work-expected)>2.e-12*scale)
                throw std::runtime_error(name);
        };
        work(true,true,"open-cell angular work balance");
        if (lower>0.) {
            work(false,true,"lower-wall angular work balance");
            work(true,false,"upper-wall angular work balance");
        }
        // Zero-pressure one-step check: adiabatic uniform transport must leave
        // internal energy unchanged. The counterfactual point source is retained
        // only to demonstrate that the original quadrature drains thermal energy.
        const double internal=2.4;
        const double energy=rho*(internal+.5*(radial_velocity*radial_velocity
            +angular_velocity*angular_velocity));
        const double radial_flux=rho*radial_velocity*radial_velocity;
        const double energy_flux=energy*radial_velocity;
        const double dt=5.e-7;
        auto thermal=[&](bool lower_open,bool upper_open,bool corrected,const char* name) {
            FluidVector source{};
            TimeIntegration::add_axisymmetric_work_source_cell(
                FluidVector{rho,rho*radial_velocity,0.,rho*angular_velocity,energy},
                nullptr,PressurelessEos{},grid,i,0,dt,lower_open,upper_open,source);
            if (source.rho!=0.||source.mom_v!=0.||source.mom_w!=0.||source.eng!=0.)
                throw std::runtime_error("axisymmetric source added a second phi/energy term");
            const double coefficient=corrected
                ?TimeIntegration::axisymmetric_centrifugal_work_coefficient(
                    grid,i,lower_open,upper_open)
                :inverse_radius;
            if (corrected) {
                if (std::abs(source.mom_u-dt*rho*angular_velocity*angular_velocity*coefficient)
                    >2.e-12*std::max(1.,std::abs(source.mom_u)))
                    throw std::runtime_error("stage source did not use the work coefficient");
            } else {
                // Counterfactual only: the pre-repair point quadrature.
                source.mom_u=dt*rho*angular_velocity*angular_velocity*coefficient;
            }
            const double rho_new=rho+dt*((lower_open?area_l*radial_mass_flux:0.)
                -(upper_open?area_r*radial_mass_flux:0.))/volume;
            const double radial_new=rho*radial_velocity
                +dt*((lower_open?area_l*radial_flux:0.)
                -(upper_open?area_r*radial_flux:0.))/volume
                +dt*rho*angular_velocity*angular_velocity*coefficient;
            const double angular_new=rho*angular_velocity
                +dt*((lower_open?torque_l*radial_mass_flux*angular_velocity:0.)
                -(upper_open?torque_r*radial_mass_flux*angular_velocity:0.))/moment;
            const double energy_new=energy+dt*((lower_open?area_l*energy_flux:0.)
                -(upper_open?area_r*energy_flux:0.))/volume;
            const double internal_new=energy_new/rho_new
                -.5*(radial_new*radial_new+angular_new*angular_new)/(rho_new*rho_new);
            if (!std::isfinite(internal_new)) throw std::runtime_error(name);
            return std::abs(internal_new-internal)/internal;
        };
        if (thermal(true,true,true,"open thermal consistency")>2.e-10)
            throw std::runtime_error("corrected open source drained thermal energy");
        if (lower>0.) {
            if (thermal(false,true,true,"wall thermal consistency")>2.e-10)
                throw std::runtime_error("corrected wall source drained thermal energy");
            if (thermal(false,true,false,"point wall counterexample")<5.e-11)
                throw std::runtime_error("point wall source no longer shows the work defect");
        }
    }
    // Independent Decimal120 moment quotients, rounded once to binary64.
    // Tiny/huge and one-ULP rings expose cancellation in T/W - A/V; ordinary
    // absolute-error checks would silently accept a completely lost coefficient.
    const double connection_oracle[][4]{
        {0, 0.125, 0, 8},
        {1e-100, 0.125, 1.28e-98, 8},
        {1e-100, 1.2689709186578246e-116, 4.9999999999999991e+99, 5.0000000000000001e+99},
        {1, 0.125, 0.45107075088099757, 0.48793711032800219},
        {1, 2.2204460492503131e-16, 0.49999999999999989, 0.5},
        {10000000000, 1250000000, 4.5107075088099754e-11, 4.8793711032800216e-11},
        {10000000000, 1.9073486328125e-06, 4.9999999999999989e-11, 4.9999999999999995e-11},
        {1e+100, 1.2500000000000007e+99, 4.5107075088099753e-101, 4.8793711032800215e-101},
        {1e+100, 1.9426688922257291e+84, 4.9999999999999988e-101, 4.9999999999999995e-101},
    };
    for(const auto& row:connection_oracle) {
        GeometryView grid{};grid.geometry=Geometry::Cylindrical;grid.dim=2;
        grid.x1_min=row[0];grid.dx1=row[1];grid.dx2=1.;
        const double actual[2]{
            TimeIntegration::axisymmetric_centrifugal_work_coefficient(grid,0,true,false),
            TimeIntegration::axisymmetric_centrifugal_work_coefficient(grid,0,false,true)};
        for(int side=0;side<2;++side) {
            const double expected=row[2+side];
            if(!std::isfinite(actual[side])||(expected==0.?actual[side]!=0.:
                std::abs(actual[side]-expected)>2.e-12*std::abs(expected)))
                throw std::runtime_error("work coefficient failed true-relative Decimal120 oracle");
        }
    }
    // Smooth radial profile: interior corrected quadrature keeps midpoint order;
    // the one-cell wall closure error is the expected shrinking O(dr) integral.
    const auto profile=[](double r) {
        return (1.+.3*r+.1*r*r)*(.5+.7*r)*(.5+.7*r);
    };
    const auto primitive=[](double r) {
        return .25*r+.3875*r*r+(0.725/3.)*r*r*r+(0.217/4.)*r*r*r*r
            +(0.049/5.)*r*r*r*r*r;
    };
    double previous_open=0.,previous_wall=0.;
    for (double h : {.2,.1,.05,.025}) {
        GeometryView grid{};
        grid.geometry=Geometry::Cylindrical; grid.dim=2;
        grid.dx1=h; grid.dx2=1.; grid.dx3=0.; grid.x1_min=1.; grid.x2_min=0.;
        const int i=0;
        const double volume=CellVolume(grid,i,0,0);
        const double exact=primitive(1.+h)-primitive(1.);
        const double sampled=profile(1.+.5*h);
        const double open_error=std::abs(volume
            *TimeIntegration::axisymmetric_centrifugal_work_coefficient(grid,i,true,true)
            *sampled-exact);
        const double wall_error=std::abs(volume
            *TimeIntegration::axisymmetric_centrifugal_work_coefficient(grid,i,false,true)
            *sampled-exact);
        if (h<.2) {
            if (!(open_error<previous_open/5.))
                throw std::runtime_error("interior corrected quadrature lost second order");
            const double ratio=wall_error/previous_wall;
            if (!(ratio>0.3&&ratio<0.8))
                throw std::runtime_error("wall closure error is not a shrinking O(dr) cell contribution");
        }
        previous_open=open_error; previous_wall=wall_error;
    }
    // Every retained chart must re-enter the old source arithmetic exactly.
    for (const auto& retained : {
            make_geometry_view(Geometry::Spherical,2,{1.,0.,0.},{.2,.3,0.}),
            make_geometry_view(Geometry::Cylindrical,3,{1.,0.,0.},{.2,.3,.1}),
            make_geometry_view(Geometry::Spherical,3,{1.,0.,0.},{.2,.3,.1})}) {
        FluidVector old_source{},new_source{};
        const FluidVector probe{2.,.6,.4,1.4,20.};
        TimeIntegration::add_geometric_source_cell(
            probe,nullptr,ConstantEos{},retained,0,0,.125,old_source);
        TimeIntegration::add_axisymmetric_work_source_cell(
            probe,nullptr,ConstantEos{},retained,0,0,.125,false,false,new_source);
        if (old_source.rho!=new_source.rho||old_source.mom_u!=new_source.mom_u
            ||old_source.mom_v!=new_source.mom_v||old_source.mom_w!=new_source.mom_w
            ||old_source.eng!=new_source.eng)
            throw std::runtime_error("retained chart source arithmetic changed");
    }
    std::cout<<"AXISYMMETRIC_ANGULAR_WORK_PASS\n";
}

/** Algebraically balanced radial pressure for cylindrical and rphi charts.
 * The fixture keeps the original high-pressure scale: rho=1e7, p=3e24,
 * dt=1e-10 and r=1e8..2e8. A uniform pressure must now cancel exactly before
 * the area products. A nonuniform face pressure is checked against the
 * independent long-double unreduced divergence plus p*(A_R-A_L)/V source.
 */
void test_pressure_balanced_curvilinear_divergence()
{
    using namespace GridMetrics;
    struct FixedPressureEos {
        double pressure;
        double get_pressure(const FluidVector&, const double*) const { return pressure; }
    };
    constexpr double high_pressure=3e24, density=1e7, dt=1e-10;
    constexpr double radial_left=1e8, radial_right=2e8;
    struct Case { Geometry geometry; int dim; };
    for (const Case& chart : {Case{Geometry::Cylindrical,1},
             Case{Geometry::Cylindrical,2},Case{Geometry::Cylindrical,3},
             Case{Geometry::Spherical,2}}) {
        GeometryView grid{};
        grid.geometry=chart.geometry; grid.dim=chart.dim;
        grid.dx1=radial_right-radial_left;
        grid.dx2=chart.dim>=2?.7:0.; grid.dx3=chart.dim==3?.4:0.;
        grid.x1_min=radial_left; grid.x2_min=0.;
        const int i=0;
        const double area_l=FaceArea(grid,0,i,0,0,false);
        const double area_r=FaceArea(grid,0,i,0,0,true);
        const double volume=CellVolume(grid,i,0,0);
        const double pressure_flux_measure=(area_r-area_l)/volume;
        const auto divergence=[&](const FluidVector& lower,const FluidVector& upper,
                                  double cell_pressure,
                                  const std::array<double,2>& lower_species,
                                  const std::array<double,2>& upper_species,
                                  FluidVector& out,std::array<double,2>& species_out) {
            FluidVector lower_copy=lower,upper_copy=upper;
            std::array<double,2> species_delta{};
            out=FluidVector{};species_out.fill(0.);
            TimeIntegration::accumulate_cell_divergence(
                lower_copy,upper_copy,lower_species.data(),upper_species.data(),
                2,1,area_l,area_r,volume,dt,out,species_delta.data(),grid,0,i,0,0,
                cell_pressure);
            species_out=species_delta;
        };

        FluidVector uniform_lower{},uniform_upper{};
        uniform_lower.mom_u=uniform_upper.mom_u=high_pressure;
        FluidVector uniform_delta{};std::array<double,2> uniform_species{};
        const std::array<double,2> no_species{0.,0.};
        divergence(uniform_lower,uniform_upper,high_pressure,no_species,no_species,
                   uniform_delta,uniform_species);
        FluidVector source{};
        const FluidVector state{density,0.,0.,0.,density*10.};
        TimeIntegration::add_axisymmetric_work_source_cell(
            state,nullptr,FixedPressureEos{high_pressure},grid,i,0,dt,true,true,
            source,true);
        if (uniform_delta.mom_u!=0.||uniform_delta.rho!=0.
            ||uniform_delta.eng!=0.||uniform_delta.mom_v!=0.
            ||uniform_delta.mom_w!=0.
            ||source.rho!=0.||source.mom_u!=0.||source.mom_v!=0.
            ||source.mom_w!=0.||source.eng!=0.
            ||uniform_species[0]!=0.||uniform_species[1]!=0.)
            throw std::runtime_error("uniform high pressure produced a radial impulse");

        const double p_l=high_pressure*(1.+2.e-3),p_r=high_pressure*(1.-1.e-3);
        FluidVector lower{1.2,p_l,.4,.1,3.},upper{.8,p_r,.6,.2,4.};
        FluidVector balanced{},unreduced{};
        std::array<double,2> balanced_species{},unreduced_species{};
        const std::array<double,2> lower_species{1.1,2.2},upper_species{1.3,2.1};
        divergence(lower,upper,high_pressure,lower_species,upper_species,
                   balanced,balanced_species);
        divergence(lower,upper,0.,lower_species,upper_species,
                   unreduced,unreduced_species);
        if (balanced.rho!=unreduced.rho||balanced.mom_v!=unreduced.mom_v
            ||balanced.mom_w!=unreduced.mom_w||balanced.eng!=unreduced.eng
            ||balanced_species!=unreduced_species)
            throw std::runtime_error("pressure reduction changed non-radial/species increments");
        FluidVector pressure_source{};
        TimeIntegration::add_axisymmetric_work_source_cell(
            state,nullptr,FixedPressureEos{high_pressure},grid,i,0,dt,true,true,
            pressure_source,true);
        const long double expected=static_cast<long double>(dt)/volume
            *(static_cast<long double>(area_l)*p_l-static_cast<long double>(area_r)*p_r
              +static_cast<long double>(high_pressure)*(area_r-area_l));
        const double actual=balanced.mom_u+pressure_source.mom_u;
        if (!std::isfinite(actual)||std::abs(static_cast<long double>(actual)-expected)
            >2.e-13L*std::max(1.L,std::abs(expected)))
            throw std::runtime_error("reduced pressure gradient differs from unreduced oracle");
        if (actual==0.||pressure_flux_measure==0.)
            throw std::runtime_error("nonuniform pressure gradient was lost");
    }
    std::cout<<"PRESSURE_BALANCED_CURVILINEAR_PASS\n";
}

/** Constant-capacity scalar modes use V and the ordinary axial Laplacian.
 * Equal species masses/gammas make cv constant, so energy and rho*X have
 * independent exact discrete cosine eigenvalues. Rotation remains present.
 */
void test_axisymmetric_scalar_diffusion()
{
    SpeciesManager species;species.add_species("X",1.,1.,1.4,1.);species.add_species("Y",1.,1.,1.4,1.);
    IdealGas eos(1.4,species);SimConfig c{};
    c.physics.diffusion.use_diffusion=c.physics.diffusion.use_thermal_diffusion=c.physics.diffusion.use_species_diffusion=true;
    c.physics.diffusion.alpha_therm=.015;c.physics.diffusion.D_spec=.02;
    Grid g(amr::MAX_NG,0.,1.,0.,1.,0.,0.,1,1,0);g.dim=2;g.geometry="cylindrical";g.InitializeTopology();
    FluidState u,d;u.Preallocate(g.GetTotalSize());u.InitSpecies(2);d.Preallocate(g.GetTotalSize());d.InitSpecies(2);
    for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i) {
        const int cell=g.GetIndex(i,j,0);const double wave=std::cos(2.*std::numbers::pi*g.GetCellCenterY(j));
        const double uphi=.2*GridMetrics::AngularMomentumVolumeMoment(g,i,j,0)/GridMetrics::CellVolume(g,i,j,0);
        u.set(cell,{1.,0.,0.,uphi,5.+.1*wave+.5*uphi*uphi});u.X(0,cell)=.4+.03*wave;u.X(1,cell)=1.-u.X(0,cell);
    }
    DiffFlux::compute_diffusion_operator(u,d,eos,g,c);
    const double eigenvalue=-4.*std::pow(std::sin(std::numbers::pi*g.dx2),2)/(g.dx2*g.dx2);
    for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
        const int cell=g.GetIndex(i,j,0);const double wave=std::cos(2.*std::numbers::pi*g.GetCellCenterY(j));
        close(d.eng[cell],c.physics.diffusion.alpha_therm*eigenvalue*.1*wave,"axial thermal eigenvalue");
        close(d.X(0,cell),c.physics.diffusion.D_spec*eigenvalue*.03*wave,"axial species eigenvalue");
        close(d.X(0,cell)+d.X(1,cell),0.,"species flux closure");
        if(d.mom_u[cell]!=0.||d.mom_v[cell]!=0.||d.mom_w[cell]!=0.||d.rho[cell]!=0.)
            throw std::runtime_error("scalar diffusion contaminated momentum/density");
    }
    const double dt=DiffFlux::adaptive_dt_diff(u,eos,g,c,.8);
    if(!std::isfinite(dt)||!(dt>0.))throw std::runtime_error("scalar diffusion timestep");
    std::cout<<"AXISYMMETRIC_SCALAR_DIFFUSION eigenvalue="<<eigenvalue<<" dt="<<dt<<'\n';
}

/** Actual canonical single-block Cartesian BC -> diffusion -> FE work witnesses.
 * The Grid/BCHandler contract fixes 16 cells per active axis (not a fabricated
 * 8-cell layout). Active data alone are initialized; apply() fills every real
 * ghost/corner. Independent source-index/parity checks precede true operator
 * and dt calls. Integral references use constant Cartesian V/A, not the stress
 * implementation. This is Host closed-boundary qualification, not AMR/RKL.
 */
void test_cartesian_real_diffusion_boundaries()
{
    SpeciesManager species;species.add_species("wall-gas",1.,1.,1.4,3.);
    IdealGas eos(1.4,species);
    constexpr double pi=3.141592653589793238462643383279502884;
    for(int dimension:{1,2,3})for(bool reflecting:{false,true})
    for(bool translation:{false,true}) {
        if(reflecting&&translation)continue; // translation is a periodic null.
        SimConfig config{};config.grid.dim=dimension;config.grid.geometry="cartesian";
        config.grid.x1_min=0.;config.grid.x1_max=1.;config.grid.x2_min=0.;config.grid.x2_max=1.;
        config.grid.x3_min=0.;config.grid.x3_max=1.;
        const std::string token=reflecting?"reflecting":"periodic";
        config.grid.x1l_boundary_type=token;config.grid.x1r_boundary_type=token;
        config.grid.x2l_boundary_type=token;config.grid.x2r_boundary_type=token;
        config.grid.x3l_boundary_type=token;config.grid.x3r_boundary_type=token;
        config.physics.diffusion.use_diffusion=true;config.physics.diffusion.use_viscous_diffusion=true;
        config.physics.diffusion.use_thermal_diffusion=false;config.physics.diffusion.use_species_diffusion=false;
        config.physics.diffusion.nu_visc=ViscousGeometryCases::viscosity;
        Grid grid(amr::MAX_NG,0.,1.,0.,1.,0.,1.);grid.dim=dimension;grid.InitializeTopology();
        FluidState state;state.Preallocate(grid.GetTotalSize());state.InitSpecies(1);
        for(int k=grid.Ks();k<grid.Ke();++k)for(int j=grid.Js();j<grid.Je();++j)
        for(int i=grid.Is();i<grid.Ie();++i) {
            const int cell=grid.GetIndex(i,j,k);
            const double x=grid.GetCellCenterX(i),y=dimension>=2?grid.GetCellCenterY(j):0.,
                z=dimension==3?grid.GetCellCenterZ(k):0.;
            // Positive varying dyadic rho makes the translation's velocity
            // divisions exact; no constant-density exemption is needed.
            const double rho=1.+.25*((i-grid.Is())%2)+.125*((j-grid.Js())%2)+.0625*((k-grid.Ks())%2);
            std::array<double,3> u{.25,-.375,.5};
            if(!translation) {
                if(reflecting) {
                    u[0]=.2*std::sin(pi*x)*(1.+.3*std::cos(pi*y)+.2*std::cos(pi*z));
                    u[1]=dimension>=2?.3*std::sin(pi*y)*(1.+.2*std::cos(pi*x)+.1*std::cos(pi*z)):.3*std::cos(pi*x);
                    u[2]=dimension==3?.4*std::sin(pi*z)*(1.+.15*std::cos(pi*x)+.1*std::cos(pi*y)):
                        .4*std::cos(pi*x)*std::cos(pi*y);
                } else {
                    u[0]=.3*std::sin(2.*pi*x)+.1*std::cos(2.*pi*y);
                    u[1]=.2*std::cos(2.*pi*x)+.25*std::sin(2.*pi*y);
                    u[2]=.15*std::sin(2.*pi*x)+.2*std::cos(2.*pi*y)+.25*std::sin(2.*pi*z);
                }
            }
            state.set(cell,{rho,rho*u[0],rho*u[1],rho*u[2],rho*(30.+.5*(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]))});
            state.X(0,cell)=1.;state.enuc_rate[cell]=0.;
        }
        const FluidState active_seed=state;
        BCHandler boundary(config);boundary.bind(eos,species);
        boundary.configure_stage(.125,arch::boundary::BoundaryPurpose::Diffusion);
        boundary.apply(state,grid);
        const int lower[3]{grid.Is(),grid.Js(),grid.Ks()},upper[3]{grid.Ie(),grid.Je(),grid.Ke()};
        for(int k=0;k<grid.GetTotalZ();++k)for(int j=0;j<grid.GetTotalY();++j)
        for(int i=0;i<grid.GetTotalX();++i) {
            const int position[3]{i,j,k};int source[3]{i,j,k};double signs[3]{1.,1.,1.};
            bool ghost=false;
            for(int axis=0;axis<dimension;++axis) {
                const int extent=upper[axis]-lower[axis];
                if(position[axis]<lower[axis]) {
                    ghost=true;source[axis]=reflecting?2*lower[axis]-position[axis]-1:position[axis]+extent;
                    if(reflecting)signs[axis]=-1.;
                } else if(position[axis]>=upper[axis]) {
                    ghost=true;source[axis]=reflecting?2*upper[axis]-position[axis]-1:position[axis]-extent;
                    if(reflecting)signs[axis]=-1.;
                }
            }
            const int cell=grid.GetIndex(i,j,k),donor=grid.GetIndex(source[0],source[1],source[2]);
            const auto actual=state.get(cell),expected=active_seed.get(donor);
            if(!std::isfinite(actual.rho)||!(actual.rho>0.)||actual.rho!=expected.rho||
               actual.mom_u!=signs[0]*expected.mom_u||actual.mom_v!=signs[1]*expected.mom_v||
               actual.mom_w!=signs[2]*expected.mom_w||actual.eng!=expected.eng||
               state.X(0,cell)!=active_seed.X(0,donor)||state.enuc_rate[cell]!=active_seed.enuc_rate[donor])
                throw std::runtime_error(ghost?"Cartesian actual ghost/corner parity mismatch":"Cartesian boundary overwrote active source");
        }
        state.boundary_flux_capture=std::make_shared<arch::boundary::BoundaryFluxCaptureStorage>();
        for(int axis=0;axis<dimension;++axis) {
            const int a=(axis+1)%3,b=(axis+2)%3;
            const std::size_t count=static_cast<std::size_t>(upper[a]-lower[a])*(upper[b]-lower[b])*7;
            for(int side=0;side<2;++side)state.boundary_flux_capture->stage[2*axis+side].assign(count,0.);
        }
        const FluidState before=state;
        FluidState delta;delta.Preallocate(grid.GetTotalSize());delta.InitSpecies(1);
        const double dt=DiffFlux::adaptive_dt_diff(state,eos,grid,config,1.);
        if(!std::isfinite(dt)||!(dt>0.))throw std::runtime_error("Cartesian real-BC dt rejected");
        DiffFlux::compute_diffusion_operator(state,delta,eos,grid,config);
        // Const operator may update only the explicit observer, never source.
        if(state.rho!=before.rho||state.mom_u!=before.mom_u||state.mom_v!=before.mom_v||
           state.mom_w!=before.mom_w||state.eng!=before.eng||state.mass_fractions!=before.mass_fractions||
           state.enuc_rate!=before.enuc_rate)throw std::runtime_error("Cartesian diffusion mutated its actual source");
        const double h[3]{grid.dx1,grid.dx2,grid.dx3};
        long double volume=1.;for(int axis=0;axis<dimension;++axis)volume*=h[axis];
        long double ke=0.,ke_next=0.,dke=0.,energy=0.,energy_scale=0.,work_scale=0.,boundary_energy=0.,boundary_scale=0.;
        double null_error=0.;
        for(int k=grid.Ks();k<grid.Ke();++k)for(int j=grid.Js();j<grid.Je();++j)
        for(int i=grid.Is();i<grid.Ie();++i) {
            const int cell=grid.GetIndex(i,j,k);const auto u=state.get(cell),d=delta.get(cell);
            if(d.rho!=0.||delta.X(0,cell)!=0.||!std::isfinite(d.eng))throw std::runtime_error("Cartesian viscosity changed mass/species or produced invalid energy");
            const double momentum[3]{u.mom_u,u.mom_v,u.mom_w},increment[3]{d.mom_u,d.mom_v,d.mom_w};
            for(int n=0;n<3;++n) {
                if(!std::isfinite(increment[n]))throw std::runtime_error("Cartesian viscous momentum is nonfinite");
                const long double term=volume*momentum[n]*increment[n]/u.rho;
                dke+=term;work_scale+=std::abs(term);
                ke+=volume*momentum[n]*momentum[n]/(2.L*u.rho);
                const long double next=static_cast<long double>(momentum[n])+dt*increment[n];
                ke_next+=volume*next*next/(2.L*u.rho);
                null_error=std::max(null_error,std::abs(increment[n]));
            }
            energy+=volume*d.eng;energy_scale+=std::abs(volume*d.eng);
            null_error=std::max(null_error,std::abs(d.eng));
        }
        for(int axis=0;axis<dimension;++axis)for(int side=0;side<2;++side) {
            const auto& plane=state.boundary_flux_capture->stage[2*axis+side];
            for(std::size_t offset=0;offset<plane.size();offset+=7) {
                for(int field=0;field<7;++field)if(!std::isfinite(plane[offset+field]))
                    throw std::runtime_error("Cartesian surface observer published nonfinite flux");
                const long double term=(side?1.L:-1.L)*(volume/h[axis])*plane[offset+4];
                boundary_energy+=term;boundary_scale+=std::abs(term);
                if(plane[offset]!=0.||plane[offset+5]!=0.)throw std::runtime_error("Cartesian wall has viscous mass/species flux");
            }
        }
        constexpr long double tolerance=2.e-12L;
        if(dke>tolerance*std::max(1.L,work_scale))throw std::runtime_error("Cartesian actual-BC kinetic derivative is positive");
        if(ke_next-ke>tolerance*std::max(1.L,ke))throw std::runtime_error("Cartesian actual recommended FE dt increases kinetic energy");
        if(std::abs(boundary_energy)>tolerance*std::max(1.L,boundary_scale)||
           std::abs(energy+boundary_energy)>tolerance*std::max(1.L,energy_scale+boundary_scale))
            throw std::runtime_error("Cartesian closed real-boundary energy accounting failed");
        if(translation&&null_error>2.e-12)throw std::runtime_error("Cartesian variable-rho periodic translation is not null");
        std::cout<<"CARTESIAN_REAL_BC_WORK dim="<<dimension<<" boundary="<<token<<" translation="<<translation
            <<" dt="<<dt<<" dke="<<static_cast<double>(dke)<<" energy="<<static_cast<double>(energy)
            <<" boundary_energy="<<static_cast<double>(boundary_energy)<<" fe_delta_ke="<<static_cast<double>(ke_next-ke)<<'\n';
    }
}

} // namespace

// Independent curl oracle: u_r=a*r, u_z=b*r, u_phi=Omega*r+c*z.
// Cyl2 resolves z, not phi: curl=(-c, -b, 2*Omega+c*z/r) in
// (r,phi,z) physical order. Constant pressure/EOS are irrelevant here.
static void test_axisymmetric_velocity_diagnostics()
{
    for(double inner:{0.,1.}) {
        Grid grid;grid.dim=2;grid.geometry="cylindrical";grid.ng=2;
        grid.x1_min=inner;grid.x1_max=inner+1.;grid.x2_min=-1.;grid.x2_max=1.;
        grid.InitializeTopology();
        std::vector<double> ur(grid.GetTotalSize()),uz(ur.size()),uphi(ur.size());
        constexpr double a=.2,b=.3,omega=.4,c=.15;
        for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
            const int q=grid.GetIndex(i,j,0);const double r=grid.GetCellCenterX(i),z=grid.GetCellCenterY(j);
            ur[q]=a*r;uz[q]=b*r;uphi[q]=omega*r+c*z;
        }
        for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
            const double r=grid.GetCellCenterX(i),z=grid.GetCellCenterY(j);
            const auto d=VelocityDiagnostics::evaluate(grid,ur,uz,uphi,i,j,0);
            const double axial=2*omega+c*z/r;
            if(std::abs(d.divergence-2*a)>2e-12 || std::abs(d.vorticity-std::sqrt(c*c+b*b+axial*axial))>2e-12)
                throw std::runtime_error("Cyl2 diagnostic r/z/phi mapping changed");
        }
    }
}

int main(int argc,char** argv)
{
    using namespace GridMetrics;
    test_axisymmetric_geometry();
    test_axisymmetric_velocity_diagnostics();
    test_axisymmetric_one_dimensional_reductions();
    test_axisymmetric_angular_work();
    test_pressure_balanced_curvilinear_divergence();
    test_axisymmetric_scalar_diffusion();
    const double conditioning = CurvilinearMetricCases::conditioning_error();
    if (!std::isfinite(conditioning) || conditioning > 2.e-12)
        throw std::runtime_error("independent thin-shell/polar measures");
    std::cout << "INDEPENDENT_METRIC_MAX_RELATIVE_ERROR=" << conditioning << '\n';
    // Same existing arithmetic metric gate; no new production science budget.
    const double pi = arch::constants::math::pi;
    for (Geometry geometry : {Geometry::Cartesian, Geometry::Cylindrical, Geometry::Spherical})
    for (int dimension : {1, 2, 3})
    for (double radius : {0.0, 1.0, 10.0})
    for (double theta_left : {0.0, pi/3, pi/2, 5*pi/6}) {
        // Native geometry can be checked before its public evolution is qualified.
        GeometryView grid{};
        grid.geometry = geometry; grid.dim = dimension;
        grid.dx1 = .25; grid.dx2 = pi/6; grid.dx3 = .2;
        grid.x1_min = radius; grid.x2_min = theta_left;
        const double theta = theta_left + pi/12;
        const double r = radius + .125;
        double width_y = grid.dx2, width_z = grid.dx3;
        if (geometry != Geometry::Cartesian) {
            if (geometry == Geometry::Spherical) width_y *= r;
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
    test_species_storage_bounds(eos);
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
    ViscousGeometryCases::newtonian_constitutive();
    ViscousGeometryCases::newtonian_covariant_gradient();
    ViscousGeometryCases::newtonian_paired_faces();
    test_cartesian_real_diffusion_boundaries();
    ViscousGeometryCases::convergence("cpu", evaluate);
    ViscousGeometryCases::radial_origin("cpu", evaluate);
    ViscousGeometryCases::density_stability("cpu", evaluate);
    RadialStokesCases::density_energy("cpu",ViscousGeometryCases::viscosity,evaluate);
    CurvedStokesCases::density_energy("cpu",ViscousGeometryCases::viscosity,evaluate);
    CurvedStokesCases::cylindrical_axis_energy("cpu",ViscousGeometryCases::viscosity,evaluate);
    CurvedStokesCases::axisymmetric_angular("cpu",ViscousGeometryCases::viscosity,evaluate);
    CurvedStokesCases::axisymmetric_convergence("cpu",ViscousGeometryCases::viscosity,evaluate);
}
