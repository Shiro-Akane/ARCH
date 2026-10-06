#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "math/geometry/RzMetricCases.h"
#include "numerics/diffusion/DiffFlux.h"
#include "numerics/diffusion/RzViscousStress.h"
#include "numerics/state/RzNativeClosure.h"
#include "physics/eos/IdealGas.h"

namespace RzViscousCases {
inline void check(double actual,long double expected,const char* name)
{
    if(!std::isfinite(actual)||std::abs(actual-expected)>2.e-12L*std::max(1.L,std::abs(expected)))
        throw std::runtime_error(name);
}

/** Independent monomial antiderivative for native radial measures. */
inline long double integral(long double lower,long double upper,int power)
{
    return (std::pow(upper,power+1)-std::pow(lower,power+1))/(power+1);
}

/** Independent analytic rho(r)=D*(a+b*r/R+c*(r/R)^2) and native means.
 * Native rho/mr/mz/E use |r| dr, while mphi uses r^2 dr. No production
 * reconstruction, moment cache, effective momentum or EOS constructs these
 * inputs. The reflected even-density cases reverse radial momentum only.
 */
struct NativeClosureReference {
    long double density_scale=1.L,radius_scale=1.L;
    long double constant=1.L,linear=0.L,quadratic=0.L;
    long double omega=1.L,radial=0.L,axial=0.L,internal=1.L/64.L;
    bool reflect_radial=false;

    long double density_moment(long double lower,long double upper,int power) const
    {
        const long double left=lower/radius_scale,right=upper/radius_scale;
        return density_scale*std::pow(radius_scale,power+1)*(
            constant*integral(left,right,power)
            +linear*integral(left,right,power+1)
            +quadratic*integral(left,right,power+2));
    }

    FluidVector native(long double lower,long double upper) const
    {
        const long double sign=upper<=0.?-1.L:1.L;
        const long double volume=sign*integral(lower,upper,1);
        const long double mass=sign*density_moment(lower,upper,1);
        const long double inertia=density_moment(lower,upper,3);
        const long double rho=mass/volume;
        const long double ur=reflect_radial&&sign<0.?-radial:radial;
        const long double energy=rho*(internal+(ur*ur+axial*axial)/2.L)
            +omega*omega*std::abs(inertia)/(2.L*volume);
        return {static_cast<double>(rho),static_cast<double>(rho*ur),
            static_cast<double>(rho*axial),static_cast<double>(
                omega*inertia/integral(lower,upper,2)),static_cast<double>(energy)};
    }
};

/** Apply the existing 2e-12 check to dimensionless values for scale tests.
 * Rescaling keeps tiny nonzero quantities meaningful without changing the
 * shared threshold; exact zero references retain the original absolute check.
 */
inline void closure_check(double actual,long double expected,const char* name)
{
    if(expected==0.L) {check(actual,expected,name);return;}
    check(static_cast<double>(static_cast<long double>(actual)/std::abs(expected)),
        expected/std::abs(expected),name);
}

/** Independently check native thermodynamics, physical points and derivatives.
 * Real Grid/FluidState stencils generate the density cache. Long-double
 * antiderivatives own all expected means; Gauss integration of returned points
 * checks mathematical V/W conservation with the existing floating tolerance,
 * rather than claiming bitwise equality or whole-model scientific acceptance.
 */
inline void native_thermodynamic_closure()
{
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    constexpr std::array<long double,4> nodes{
        -.8611363115940525752239464888928095L,
        -.3399810435848562648026657591032447L,
         .3399810435848562648026657591032447L,
         .8611363115940525752239464888928095L};
    constexpr std::array<long double,4> weights{
        .3478548451374538573730639492219994L,
        .6521451548625461426269360507780006L,
        .6521451548625461426269360507780006L,
        .3478548451374538573730639492219994L};
    SpeciesManager species;species.add_species("gas",1.,1.,1.4,3.);
    IdealGas eos(1.4,species);
    const double composition[]{1.};
    int checked=0;
    for(int mode=0;mode<6;++mode) {
        NativeClosureReference reference;
        if(mode!=0) {reference.constant=7.L/8.L;reference.quadratic=1.L/4.L;}
        if(mode==2) {reference.radial=3.L/4.L;reference.axial=-5.L/8.L;
            reference.reflect_radial=true;}
        if(mode==3) {
            // Non-axis [1,2]: positive rho=(2-r)^2+1/64 yields kappa>1.
            // Its real three-cell stencil stays on the positive side of r=0.
            reference.constant=4.L+1.L/64.L;reference.linear=-4.L;
            reference.quadratic=1.L;
        }
        if(mode==4) {
            reference.radius_scale=std::ldexp(1.L,-100);
            reference.density_scale=std::ldexp(1.L,-300);
            reference.omega=std::ldexp(1.L,200);
            reference.internal=std::ldexp(1.L,190);
        }
        if(mode==5) {
            reference.radius_scale=std::ldexp(1.L,100);
            reference.density_scale=std::ldexp(1.L,300);
            reference.omega=std::ldexp(1.L,-200);
            reference.internal=std::ldexp(1.L,-190);
        }
        const double radius=static_cast<double>(reference.radius_scale);
        const double inner=mode==3?radius:0.;
        Grid grid(amr::MAX_NG,inner,inner+amr::BLOCK_NX*radius,-.5,.5,0.,1.);
        grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(rz);
        closure_check(grid.GetFacePosL(grid.Is()),inner,"RZ closure fixture lower cell bound changed");
        closure_check(grid.GetFacePosR(grid.Is()),static_cast<long double>(inner)+radius,
            "RZ closure fixture first-cell width changed");
        const auto geometry=GridMetrics::make_geometry_view(grid,rz);
        FluidState state;state.Preallocate(grid.GetTotalSize());state.InitSpecies(1);
        for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
            const int index=grid.GetIndex(i,j,0);
            state.set(index,reference.native(grid.GetFacePosL(i),grid.GetFacePosR(i)));
            state.X(0,index)=1.;
        }
        const auto read=[&](int index){return state.get(index);};
        const int j=grid.Js()+1;
        for(int reflected=0;reflected<(mode==2?2:1);++reflected) {
            const int i=grid.Is()-reflected,index=grid.GetIndex(i,j,0);
            const auto native=state.get(index);
            const auto cell=RzThermodynamics::make_cell(read,index,geometry,i);
            if(!cell.valid())throw std::runtime_error("RZ native analytic closure rejected");
            if(mode<2&&arch::state::recover(native).status!=arch::state::Status::unresolved_energy)
                throw std::runtime_error("RZ low-energy witness no longer isolates raw mixed-measure recovery");
            const long double lo=grid.GetFacePosL(i),hi=grid.GetFacePosR(i);
            const long double sign=hi<=0.?-1.L:1.L;
            const long double ur=reference.reflect_radial&&sign<0.?-reference.radial:reference.radial;
            closure_check(cell.omega,reference.omega,"RZ closure Omega differs from J/I reference");
            closure_check(cell.internal,reference.internal,"RZ closure lost low internal energy");
            closure_check(cell.radial_velocity,ur,"RZ closure radial parity mismatch");
            closure_check(cell.axial_velocity,reference.axial,"RZ closure axial velocity mismatch");
            closure_check(eos.get_pressure(cell.effective_mean,composition),
                .4L*native.rho*reference.internal,"RZ representative ideal pressure mismatch");
            if(mode==3&&!(std::abs(cell.effective_mean.mom_w)>std::abs(native.mom_w)))
                throw std::runtime_error("RZ legitimate kappa>1 was clipped or rejected");
            if(reflected&&!(cell.density.capacity<0.))
                throw std::runtime_error("RZ reflected closure lost signed inertia parity");

            std::array<long double,5> sums{};
            const long double half=(hi-lo)/2.L,midpoint=lo+half;
            for(unsigned n=0;n<nodes.size();++n) {
                const double r=static_cast<double>(midpoint+half*nodes[n]);
                const long double x=static_cast<long double>(r)/reference.radius_scale;
                const long double rho=reference.density_scale*(reference.constant
                    +reference.linear*x+reference.quadratic*x*x);
                const long double gradient=reference.density_scale/reference.radius_scale
                    *(reference.linear+2.L*reference.quadratic*x);
                const long double velocity=reference.omega*r;
                const auto point=RzThermodynamics::base_point(cell,r);
                const auto point_kinematics=arch::state::recover(point);
                if(point_kinematics.status!=arch::state::Status::valid)
                    throw std::runtime_error("RZ resolved baseline point failed original energy gate");
                closure_check(point.rho,rho,"RZ baseline density differs from analytic polynomial");
                closure_check(point.mom_w,rho*velocity,"RZ baseline physical swirl mismatch");
                closure_check(eos.get_pressure(point,composition),.4L*rho*reference.internal,
                    "RZ physical point ideal pressure mismatch");
                const std::array<double,5> values{point.rho,point.mom_u,point.mom_v,
                    point.mom_w,point.eng};
                for(unsigned field=0;field<values.size();++field)
                    sums[field]+=weights[n]*values[field]*(field==3?
                        static_cast<long double>(r)*r:std::abs(static_cast<long double>(r)));

                const auto derivative=RzThermodynamics::base_derivative(cell,r);
                const std::array<double,5> actual{derivative.rho,derivative.mom_u,
                    derivative.mom_v,derivative.mom_w,derivative.eng};
                const std::array<long double,5> expected{gradient,gradient*ur,
                    gradient*reference.axial,reference.omega*(gradient*r+rho),
                    gradient*(reference.internal+(ur*ur+reference.axial*reference.axial)/2.L)
                        +reference.omega*reference.omega*(gradient*r*r+2.L*rho*r)/2.L};
                for(unsigned field=0;field<actual.size();++field)
                    closure_check(actual[field],expected[field],"RZ baseline derivative formula mismatch");
            }
            const std::array<double,5> native_values{native.rho,native.mom_u,native.mom_v,
                native.mom_w,native.eng};
            for(unsigned field=0;field<native_values.size();++field) {
                const long double measure=field==3?integral(lo,hi,2):sign*integral(lo,hi,1);
                closure_check(static_cast<double>(half*sums[field]/measure),native_values[field],
                    "RZ baseline changed an independently integrated native mean");
            }

            auto invalid_geometry=geometry;invalid_geometry.dx1=0.;
            if(RzThermodynamics::make_cell(read,index,invalid_geometry,i).valid())
                throw std::runtime_error("RZ closure accepted zero-width geometry");
            invalid_geometry=geometry;invalid_geometry.geometry=GridMetrics::Geometry::Cartesian;
            if(RzThermodynamics::make_cell(read,index,invalid_geometry,i).valid())
                throw std::runtime_error("RZ closure accepted a different computation geometry");
            auto invalid_density=cell.density;invalid_density.capacity=0.;
            if(RzThermodynamics::from_density(native,invalid_density).valid())
                throw std::runtime_error("RZ closure accepted invalid density capacity");
            for(int field=0;field<5;++field) {
                auto invalid_native=native;
                const double bad=std::numeric_limits<double>::quiet_NaN();
                if(field==0)invalid_native.rho=bad;
                if(field==1)invalid_native.mom_u=bad;
                if(field==2)invalid_native.mom_v=bad;
                if(field==3)invalid_native.mom_w=bad;
                if(field==4)invalid_native.eng=bad;
                const auto rejected=RzThermodynamics::from_density(invalid_native,cell.density);
                if(rejected.valid()||rejected.status!=arch::state::Status::nonfinite)
                    throw std::runtime_error("RZ closure accepted a nonfinite native field");
            }
            for(double rho:{0.,-1.}) {
                auto invalid_native=native;invalid_native.rho=rho;
                const auto rejected=RzThermodynamics::from_density(invalid_native,cell.density);
                if(rejected.valid()||rejected.status!=arch::state::Status::nonpositive_density)
                    throw std::runtime_error("RZ closure accepted nonpositive native rho");
            }
            auto invalid_native=native;invalid_native.eng=0.;
            if(RzThermodynamics::from_density(invalid_native,cell.density).status
                    !=arch::state::Status::unresolved_energy)
                throw std::runtime_error("RZ closure accepted an unresolved thermal remainder");
            arch::state::Bounds bounds;bounds.density=2.*native.rho;
            if(RzThermodynamics::from_density(native,cell.density,bounds).status
                    !=arch::state::Status::invalid_thermodynamics)
                throw std::runtime_error("RZ closure bypassed configured density bound");
            bounds={};bounds.internal_min=static_cast<double>(2.L*reference.internal);
            if(RzThermodynamics::from_density(native,cell.density,bounds).status
                    !=arch::state::Status::invalid_thermodynamics)
                throw std::runtime_error("RZ closure bypassed configured internal-energy lower bound");
            bounds={};bounds.internal_max=static_cast<double>(reference.internal/2.L);
            if(RzThermodynamics::from_density(native,cell.density,bounds).status
                    !=arch::state::Status::energy_ceiling)
                throw std::runtime_error("RZ closure bypassed configured internal-energy ceiling");
            ++checked;
        }
    }

    // A representative-mean gate is not a certificate for every physical
    // point. Freeze a cancellation-band case without changing the 8eps rule.
    NativeClosureReference unresolved;unresolved.internal=std::ldexp(11.L,-54);
    Grid grid(amr::MAX_NG,0.,amr::BLOCK_NX,-.5,.5,0.,1.);
    grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(rz);
    FluidState state;state.Preallocate(grid.GetTotalSize());state.InitSpecies(1);
    for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i)
        state.set(grid.GetIndex(i,j,0),unresolved.native(grid.GetFacePosL(i),grid.GetFacePosR(i)));
    const auto geometry=GridMetrics::make_geometry_view(grid,rz);
    const auto read=[&](int index){return state.get(index);};
    const int i=grid.Is(),index=grid.GetIndex(i,grid.Js()+1,0);
    const auto cell=RzThermodynamics::make_cell(read,index,geometry,i);
    if(!cell.valid())throw std::runtime_error("RZ cancellation counterexample mean no longer resolved");
    const double outer_radius=static_cast<double>(.5L+.5L*nodes.back());
    if(arch::state::recover(RzThermodynamics::base_point(cell,outer_radius)).status
            !=arch::state::Status::unresolved_energy)
        throw std::runtime_error("RZ mean validity incorrectly promoted to actual-point validity");

    // Trusted analytic cache exercises only the pure derivative leaf. It is
    // deliberately not a density-producer, ghost, topology or EOS certificate.
    RzDensity::Cell trusted;
    trusted.density={1.e307,0.,1.e308};trusted.origin=.5;trusted.spacing=1.;
    trusted.lower=0.;trusted.upper=1.;trusted.valid=true;
    const long double c0=trusted.density.constant,c2=trusted.density.quadratic;
    const long double capacity=c0/4.L+7.L*c2/240.L;
    trusted.capacity=static_cast<double>(capacity);
    trusted.mean_s=static_cast<double>((c0/6.L+c2/42.L)/capacity);
    trusted.density_scale=static_cast<double>(c0+c2/4.L);trusted.radius_scale=1.;
    trusted.weighted_two=2./3.;
    trusted.weighted_three=static_cast<double>(2.L*capacity/trusted.density_scale);
    const long double rho_mean=c0+c2/12.L;
    const FluidVector native{static_cast<double>(rho_mean),0.,0.,0.,
        static_cast<double>(rho_mean*1.e-100L)};
    const auto extreme=RzThermodynamics::from_density(native,trusted);
    if(!extreme.valid())throw std::runtime_error("RZ trusted derivative-overflow cache rejected");
    const auto derivative=RzThermodynamics::base_derivative(extreme,.5);
    for(double value:{derivative.rho,derivative.mom_u,derivative.mom_v,
            derivative.mom_w,derivative.eng})
        if(!std::isfinite(value)||value!=0.)
            throw std::runtime_error("RZ derivative overflowed a representable zero center gradient");
    std::cout<<"RZ_NATIVE_THERMODYNAMIC_CLOSURE fixtures="<<checked
        <<" point_gate_counterexample=1 trusted_derivative_counterexample=1 PASS\n";
}

/** Apply the shared leaf with independently integrated torque/area divergence.
 * The common 2*pi cancels. Momentum uses W=dz*int r^2 dr, whereas energy
 * uses V=dz*int r dr. This checks both normalizations before EOS dispatch.
 */
inline std::array<double,2> leaf_rhs(const FluidState& state,const Grid& grid,
    int i,int j,double nu)
{
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    const auto geometry=GridMetrics::make_geometry_view(grid,rz);
    const auto read=[&](int index){return state.get(index);};
    const int index=grid.GetIndex(i,j,0);
    const auto center=RzViscousStress::angular_cell(read,index,geometry,i);
    const auto low=RzViscousStress::angular_cell(read,index-1,geometry,i-1);
    const auto high=RzViscousStress::angular_cell(read,index+1,geometry,i+1);
    const auto down=RzViscousStress::angular_cell(read,
        grid.GetIndex(i,j-1,0),geometry,i);
    const auto up=RzViscousStress::angular_cell(read,
        grid.GetIndex(i,j+1,0),geometry,i);
    const long double lower=grid.GetFacePosL(i),upper=grid.GetFacePosR(i);
    const auto radial_low=RzViscousStress::azimuthal_face(low,center,0,
        geometry.dx1,nu,static_cast<double>(lower));
    const auto radial_high=RzViscousStress::azimuthal_face(center,high,0,
        geometry.dx1,nu,static_cast<double>(upper));
    const auto axial_low=RzViscousStress::azimuthal_face(down,center,1,
        geometry.dx2,nu,0.);
    const auto axial_high=RzViscousStress::azimuthal_face(center,up,1,
        geometry.dx2,nu,0.);
    if(!radial_low.valid||!radial_high.valid||!axial_low.valid||!axial_high.valid)
        throw std::runtime_error("RZ analytic leaf rejected a native face");
    if(lower==0.&&(radial_low.momentum!=0.||radial_low.energy!=0.||
        !(low.capacity<0.)||RzViscousStress::face_row_rate(center,low,0,
            geometry.dx1,nu,0.)!=0.))
        throw std::runtime_error("RZ reflected axis failed the exact zero traction/bound limit");
    const long double volume=integral(lower,upper,1);
    const long double torque_measure=integral(lower,upper,2);
    const long double torque=-(radial_high.momentum*upper*upper-
        radial_low.momentum*lower*lower)/torque_measure-
        (axial_high.momentum-axial_low.momentum)/geometry.dx2;
    const long double work=-(radial_high.energy*upper-radial_low.energy*lower)/volume-
        (axial_high.energy-axial_low.energy)/geometry.dx2;
    return {static_cast<double>(torque),static_cast<double>(work)};
}

/** Four-cell closed graph: actual flux conserves J and dissipates C-weighted
 * angular energy. Independent constant-density C and s moments also verify
 * each row rate against K/C. This is a frozen graph witness, not a claim about
 * nonlinear whole-tensor diffusion, RKL evolution or physical boundary traces.
 */
inline void closed_graph()
{
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    Grid grid(amr::MAX_NG,1.,2.,-.5,.5,0.,1.);
    grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(rz);
    const auto geometry=GridMetrics::make_geometry_view(grid,rz);
    FluidState state;state.Preallocate(grid.GetTotalSize());state.InitSpecies(1);
    for(int index=0;index<grid.GetTotalSize();++index)
        state.set(index,{1.,0.,0.,0.,1000.});
    constexpr int count=4;
    constexpr double nu=.01;
    const std::array<double,count> omega{2.,-1.,.5,4.};
    std::array<RzViscousStress::AngularCell,count> cells;
    std::array<long double,count> capacity{},abscissa{},change{};
    const int first=grid.Is()+2,j=grid.Js()+2;
    for(int n=0;n<count;++n) {
        const int i=first+n,index=grid.GetIndex(i,j,0);
        const long double lower=grid.GetFacePosL(i),upper=grid.GetFacePosR(i);
        capacity[n]=integral(lower,upper,3);
        abscissa[n]=integral(lower,upper,5)/capacity[n];
        state.set(index,{1.,0.,0.,static_cast<double>(
            omega[n]*capacity[n]/integral(lower,upper,2)),1000.});
    }
    const auto read=[&](int index){return state.get(index);};
    for(int n=0;n<count;++n) {
        cells[n]=RzViscousStress::angular_cell(read,
            grid.GetIndex(first+n,j,0),geometry,first+n);
        if(!cells[n].valid)throw std::runtime_error("RZ closed graph invalid cell");
        check(cells[n].capacity,capacity[n],"RZ graph capacity disagrees with native integral");
        check(cells[n].mean_s,abscissa[n],"RZ graph abscissa disagrees with native integral");
        check(cells[n].omega,omega[n],"RZ graph angular momentum normalization mismatch");
    }
    long double expected_dissipation=0.;
    for(int n=0;n+1<count;++n) {
        const double radius=grid.GetFacePosR(first+n);
        const long double delta=omega[n+1]-omega[n];
        const long double coefficient=2.L*nu*std::pow(static_cast<long double>(radius),4)/
            (abscissa[n+1]-abscissa[n]);
        const auto flux=RzViscousStress::azimuthal_face(cells[n],cells[n+1],0,
            geometry.dx1,nu,radius);
        if(!flux.valid)throw std::runtime_error("RZ closed graph invalid connection");
        const long double outward_torque=flux.momentum*radius*radius;
        change[n]-=outward_torque;change[n+1]+=outward_torque;
        expected_dissipation-=coefficient*delta*delta;
        const double left_rate=RzViscousStress::face_row_rate(cells[n],cells[n+1],0,
            geometry.dx1,nu,radius);
        const double right_rate=RzViscousStress::face_row_rate(cells[n+1],cells[n],0,
            geometry.dx1,nu,radius);
        check(left_rate,coefficient/capacity[n],"RZ left row rate differs from independent K/C");
        check(right_rate,coefficient/capacity[n+1],"RZ right row rate differs from independent K/C");
        check(cells[n].capacity*left_rate,cells[n+1].capacity*right_rate,
            "RZ graph link is not symmetric in the capacity measure");
    }
    long double total=0.,dissipation=0.;
    for(int n=0;n<count;++n) {
        total+=change[n];dissipation+=omega[n]*change[n];
    }
    check(static_cast<double>(total),0.L,"RZ closed graph changed total angular momentum");
    check(static_cast<double>(dissipation),expected_dissipation,
        "RZ closed graph violates weighted dissipativity");
    if(!(dissipation<0.))throw std::runtime_error("RZ graph anti-diffuses angular energy");

    // Capacities depend only on density; the diffusion caller owns thermal
    // admissibility. Corrupting an unrelated field must not redefine C or s.
    const int index=grid.GetIndex(first,j,0);
    auto unrelated=state.get(index);
    unrelated.mom_u=std::numeric_limits<double>::quiet_NaN();
    unrelated.mom_v=std::numeric_limits<double>::infinity();
    unrelated.eng=std::numeric_limits<double>::quiet_NaN();
    state.set(index,unrelated);
    const auto unchanged=RzViscousStress::angular_cell(read,index,geometry,first);
    if(!unchanged.valid||unchanged.capacity!=cells[0].capacity||
       unchanged.mean_s!=cells[0].mean_s||unchanged.omega!=cells[0].omega)
        throw std::runtime_error("RZ density graph depends on unrelated thermodynamic fields");
    auto invalid=cells[0];invalid.density={-1.,0.,8.};
    if(RzViscousStress::azimuthal_face(invalid,cells[1],0,geometry.dx1,nu,
            grid.GetFacePosR(first)).valid)
        throw std::runtime_error("RZ graph accepted a density with a negative interior minimum");
    invalid=cells[0];invalid.capacity=0.;
    if(std::isfinite(RzViscousStress::face_row_rate(invalid,cells[1],0,
            geometry.dx1,nu,grid.GetFacePosR(first)))||
       RzViscousStress::azimuthal_face(cells[0],cells[1],0,0.,nu,
            grid.GetFacePosR(first)).valid||
       RzViscousStress::azimuthal_face(cells[0],cells[1],0,geometry.dx1,-nu,
            grid.GetFacePosR(first)).valid||
       RzViscousStress::azimuthal_face(cells[0],cells[1],0,geometry.dx1,nu,0.).valid)
        throw std::runtime_error("RZ graph fabricated a bound or flux for an invalid link");

    // Positive means with a sharp central trough require whole-polynomial
    // limiting, including its interior vertex. Check the preserved V mean
    // by antiderivatives, without using the reconstruction's moment routine.
    state.set(index-1,{100.,0.,0.,0.,1000.});
    state.set(index,{1.,0.,0.,0.,1000.});
    state.set(index+1,{100.,0.,0.,0.,1000.});
    const auto limited=RzViscousStress::angular_cell(read,index,geometry,first);
    if(!limited.valid)throw std::runtime_error("RZ positive density means could not form a graph");
    const auto& polynomial=limited.density;
    const double low_t=(limited.lower-limited.origin)/limited.spacing;
    const double high_t=(limited.upper-limited.origin)/limited.spacing;
    double minimum=std::min(polynomial.at(low_t),polynomial.at(high_t));
    if(polynomial.quadratic!=0.) {
        const double vertex=-polynomial.linear/(2.*polynomial.quadratic);
        if(vertex>low_t&&vertex<high_t)minimum=std::min(minimum,polynomial.at(vertex));
    }
    if(!(minimum>0.))throw std::runtime_error("RZ density limiter left a nonpositive interior");
    const long double lower=limited.lower,upper=limited.upper,origin=limited.origin;
    const long double radial_volume=integral(lower,upper,1);
    const long double mean_t=(integral(lower,upper,2)-origin*radial_volume)/
        (limited.spacing*radial_volume);
    const long double mean_t2=(integral(lower,upper,3)-2.L*origin*integral(lower,upper,2)+
        origin*origin*radial_volume)/(limited.spacing*limited.spacing*radial_volume);
    check(static_cast<double>(polynomial.constant+polynomial.linear*mean_t+
        polynomial.quadratic*mean_t2),1.L,"RZ density limiter changed the authoritative V mean");
    std::cout<<"RZ_FROZEN_ANGULAR_GRAPH cells="<<count<<" PASS\n";
}
/** True V/W means for rigid rotation, cubic swirl and axial azimuthal shear.
 * Independent antiderivatives initialize the entire analytic ghost extension.
 * Other velocities vanish, isolating the approved azimuthal migration.
 */
inline void azimuthal_operator()
{
    closed_graph();
    SpeciesManager species;species.add_species("gas",1.,1.,1.4,3.);
    IdealGas eos(1.4,species);
    SimConfig config;config.physics.diffusion.use_diffusion=true;
    config.physics.diffusion.use_viscous_diffusion=true;
    constexpr double nu=.01;config.physics.diffusion.nu_visc=nu;
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    int fixtures=0;
    for(double inner:{0.,1.})for(int mode:{0,1,2})for(int density_degree:{0,1,2}) {
        // A linear density has no smooth even axis extension.
        if(inner==0.&&density_degree==1)continue;
        if(mode!=0&&density_degree!=0)continue;
        Grid grid(amr::MAX_NG,inner,inner+1.,-.5,.5,0.,1.);
        grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(rz);
        FluidState state,delta;
        state.Preallocate(grid.GetTotalSize());state.InitSpecies(1);
        delta.Preallocate(grid.GetTotalSize());delta.InitSpecies(1);
        const long double linear=density_degree==1?.1L:0.L;
        const long double quadratic=density_degree==2?.02L:0.L;
        constexpr long double omega=2.L,cubic=.25L,axial=.125L;
        for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
            long double lo=grid.GetFacePosL(i),hi=grid.GetFacePosR(i),sign=1.;
            if(hi<=0.) {const auto old=lo;lo=-hi;hi=-old;sign=-1.;}
            const auto moment=[&](int p,int w){return RzMetricCases::mean_power(lo,hi,p,w);};
            const long double rho=1.L+linear*moment(1,1)+quadratic*moment(2,1);
            const long double z=grid.GetCellCenterY(j);
            long double momentum=omega*(moment(1,2)+linear*moment(2,2)+quadratic*moment(3,2));
            if(mode==1)momentum+=cubic*moment(3,2);
            if(mode==2)momentum+=axial*z*moment(1,2);
            const int cell=grid.GetIndex(i,j,0);
            state.set(cell,{static_cast<double>(rho),0.,0.,static_cast<double>(sign*momentum),1000.});
            state.X(0,cell)=1.;
        }
        DiffFlux::compute_diffusion_operator(state,delta,eos,grid,config,rz);
        double maximum=0.;
        for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
            const int cell=grid.GetIndex(i,j,0);
            const long double lo=grid.GetFacePosL(i),hi=grid.GetFacePosR(i);
            const auto moment=[&](int p,int w){return RzMetricCases::mean_power(lo,hi,p,w);};
            long double torque=0.,work=0.;
            if(mode==1) {
                // L_phi=8*nu*a*r; div(u*tau)=8*nu*a*Omega*r²+12*nu*a²*r⁴.
                torque=8.L*nu*cubic*moment(1,2);
                work=8.L*nu*cubic*omega*moment(2,1)+12.L*nu*cubic*cubic*moment(4,1);
            } else if(mode==2)work=nu*axial*axial*moment(2,1);
            const auto leaf=leaf_rhs(state,grid,i,j,nu);
            check(leaf[0],torque,"RZ shared leaf disagrees with independent angular force");
            check(leaf[1],work,"RZ shared leaf disagrees with independent stress work");
            check(delta.mom_w[cell],torque,"RZ symmetric azimuthal shear/torque mismatch");
            check(delta.eng[cell],work,"RZ stress work used a torque measure or duplicate heat");
            check(delta.mom_u[cell],0.L,"RZ azimuthal migration changed radial operator");
            check(delta.mom_v[cell],0.L,"RZ azimuthal migration changed axial operator");
            if(delta.rho[cell]!=0.||delta.X(0,cell)!=0.)throw std::runtime_error("RZ viscosity changed mass/species");
            maximum=std::max(maximum,std::abs(delta.mom_w[cell]-static_cast<double>(torque)));
        }
        std::cout<<"RZ_SYMMETRIC_VISCOSITY inner="<<inner<<" mode="<<mode
            <<" density_degree="<<density_degree<<" maximum_torque_error="<<maximum<<'\n';
        ++fixtures;
    }
    std::cout<<"RZ_SYMMETRIC_VISCOSITY fixtures="<<fixtures<<" PASS\n";
}
} // namespace RzViscousCases
