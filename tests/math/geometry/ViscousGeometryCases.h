// Independent three-dimensional Stokes witnesses in each actual component chart.
// Test data only: no call to production gradient, connection or flux operators.
#pragma once

#include "data/FluidState.h"
#include "grid/GridGeometryView.h"
#include "grid/Grid.h"
#include "numerics/diffusion/NewtonianViscousStress.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace ViscousGeometryCases {

inline constexpr double viscosity = .03;

struct Sample {
    FluidVector state;
    FluidVector derivative;
};

struct Evaluation {
    std::vector<FluidVector> derivative;
    double raw_dt;
};

inline Sample sample(GridMetrics::Geometry geometry, int dimension,
                     double r, double q2, double q3, bool uniform,
                     double density_slope)
{
    Sample result{};
    if (dimension == 1) {
        // v=r^3 e_r, rho=1+a*r; physical dimension is the radial measure's
        // Cartesian/cylindrical/spherical embedding. Tangential flow is zero.
        const int embedded = geometry == GridMetrics::Geometry::Cartesian ? 1
                           : geometry == GridMetrics::Geometry::Cylindrical ? 2 : 3;
        const double rho = 1. + density_slope*r;
        const double velocity = r*r*r;
        result.state = {rho, rho*velocity, 0., 0., rho*(30.+.5*velocity*velocity)};
        // Cyl: tau_rr=(10/3)*mu*r^2, tau_phiphi=-(2/3)*mu*r^2.
        // Sph: tau_rr=(8/3)*mu*r^2, tau_tt=tau_pp=-(4/3)*mu*r^2.
        // div(tau)_r includes -tau_phiphi/r or -(tau_tt+tau_pp)/r;
        // total energy is div(u*tau_rr), not an additional heating source.
        const long double radius=r,density=rho,a=density_slope,nu=viscosity;
        if(embedded==2) {
            result.derivative.mom_u=static_cast<double>(nu*(32.L*density*radius/3.L+10.L*a*radius*radius/3.L));
            result.derivative.eng=static_cast<double>(nu*(20.L*density*std::pow(radius,4)+10.L*a*std::pow(radius,5)/3.L));
        } else {
            result.derivative.mom_u=static_cast<double>(nu*(40.L*density*radius/3.L+8.L*a*radius*radius/3.L));
            result.derivative.eng=static_cast<double>(nu*(56.L*density*std::pow(radius,4)/3.L+8.L*a*std::pow(radius,5)/3.L));
        }
        if (geometry == GridMetrics::Geometry::Cartesian) {
            // u=x^3, tau_xx=(4/3)*mu*u'=4*mu*x^2:
            // div(tau)_x=nu*(8*rho*x+4*a*x^2),
            // div(u*tau)=nu*(20*rho*x^4+4*a*x^5).
            const long double x=r,a=density_slope,density=rho,nu=viscosity;
            result.derivative.mom_u=static_cast<double>(nu*(8.L*density*x+4.L*a*x*x));
            result.derivative.eng=static_cast<double>(nu*(20.L*density*x*x*x*x+4.L*a*x*x*x*x*x));
        }
        return result;
    }

    // v=q e_x, q=1 or x^2+y^2(+z^2); rho=1+a*x, mu=nu*rho.
    // Full Stokes gives Fx=nu*((2D+2/3)*rho+(8/3)*a*x),
    // Fy=2*nu*a*y, Fz=2*nu*a*z, with the same traction's energy work.
    // A uniform Cartesian vector gives zero momentum AND work-flux divergence.
    double x = r, squared_radius = r*r;
    std::array<double,3> cartesian_x_projection{1.,0.,0.};
    if (geometry == GridMetrics::Geometry::Cartesian) {
        squared_radius += q2*q2;
        if (dimension == 3) squared_radius += q3*q3;
    } else if (dimension == 2 && geometry == GridMetrics::Geometry::Spherical) {
        x = r*std::cos(q2);
        cartesian_x_projection = {std::cos(q2),-std::sin(q2),0.};
    } else if (geometry == GridMetrics::Geometry::Cylindrical) {
        x = r*std::cos(q3);
        squared_radius += q2*q2;
        cartesian_x_projection = {std::cos(q3),0.,-std::sin(q3)};
    } else {
        x = r*std::sin(q2)*std::cos(q3);
        cartesian_x_projection = {std::sin(q2)*std::cos(q3),
                                 std::cos(q2)*std::cos(q3),-std::sin(q3)};
    }
    const double rho = 1.+density_slope*x;
    const double q = uniform ? 1. : squared_radius;
    const auto& b = cartesian_x_projection;
    result.state = {rho,rho*q*b[0],rho*q*b[1],rho*q*b[2],rho*(30.+.5*q*q)};
    if (!uniform) {
        // Rotate the exact Cartesian force, including its transverse part.
        // In a curved frame the position vector is r*e_r (+z*e_z in cyl).
        const std::array<long double,3> position=geometry==GridMetrics::Geometry::Cartesian
            ?std::array<long double,3>{r,q2,dimension==3?q3:0.}
            :std::array<long double,3>{r,geometry==GridMetrics::Geometry::Cylindrical?q2:0.,0.};
        const long double nu=viscosity,a=density_slope,density=rho,X=x,Q=q;
        const long double parallel=(2.L*dimension+2.L/3.L)*density+2.L*a*X/3.L;
        result.derivative.mom_u=static_cast<double>(nu*(parallel*b[0]+2.L*a*position[0]));
        result.derivative.mom_v=static_cast<double>(nu*(parallel*b[1]+2.L*a*position[1]));
        result.derivative.mom_w=static_cast<double>(nu*(parallel*b[2]+2.L*a*position[2]));
        result.derivative.eng=static_cast<double>(nu*(density*((2.L*dimension+14.L/3.L)*Q
            +4.L*X*X/3.L)+8.L*a*X*Q/3.L));
        if (geometry == GridMetrics::Geometry::Cartesian) {
            // u=Q e_x, Q=x^2+y^2(+z^2), mu=nu*(1+a*x), full 3D trace.
            // tau_xx=(8/3)*mu*x, tau_xj=2*mu*x_j, tau_jj=-(4/3)*mu*x.
            // F_x=nu*((2*D+2/3)*rho+(8/3)*a*x), F_j=2*nu*a*x_j.
            // div(tau*u)=nu*(rho*((2*D+14/3)*Q+(4/3)*x^2)+(8/3)*a*x*Q).
            // These closed polynomial derivatives do not call production math.
            const long double nu=viscosity,a=density_slope,density=rho,X=x,Q=q;
            result.derivative.mom_u=static_cast<double>(nu*((2.L*dimension+2.L/3.L)*density+8.L*a*X/3.L));
            result.derivative.mom_v=static_cast<double>(2.L*nu*a*q2);
            result.derivative.mom_w=dimension==3?static_cast<double>(2.L*nu*a*q3):0.;
            result.derivative.eng=static_cast<double>(nu*(density*((2.L*dimension+14.L/3.L)*Q+4.L*X*X/3.L)+8.L*a*X*Q/3.L));
        }
    }
    return result;
}

inline double error(const FluidVector& actual, const FluidVector& expected)
{
    const double a[]{actual.rho,actual.mom_u,actual.mom_v,actual.mom_w,actual.eng};
    const double e[]{expected.rho,expected.mom_u,expected.mom_v,expected.mom_w,expected.eng};
    double maximum = 0.;
    for (int field=0; field<5; ++field) {
        if (!std::isfinite(a[field])) return std::numeric_limits<double>::infinity();
        maximum = std::max(maximum,std::abs(a[field]-e[field])/std::max(1.,std::abs(e[field])));
    }
    return maximum;
}

// Both executors consume identical input fields and independently derived
// derivatives. Only evaluate(state, grid) is backend-specific.
template <typename Evaluate>
void convergence(const char* backend, Evaluate evaluate)
{
    for (const char* name : {"cartesian", "cylindrical", "spherical"})
    for (int dimension : {1, 2, 3})
    for (double density_slope : {0., .1})
    for (bool uniform : {false, true}) {
        // The former cylindrical polar problem is retired. Genuine RZ tensor
        // and angular-moment witnesses belong to the existing RzViscousCases owner.
        if (dimension==2 && std::string_view(name)=="cylindrical") continue;
        if (dimension == 1 && uniform) continue;
        double previous = 0.;
        for (double spacing : {.05, .025, .0125}) {
            const double x = 2. - (amr::BLOCK_NX/2 + .5)*spacing;
            const double y = .9 - (amr::BLOCK_NY/2 + .5)*spacing;
            const double z = .7 - (amr::BLOCK_NZ/2 + .5)*spacing;
            Grid grid(amr::MAX_NG, x, x+amr::BLOCK_NX*spacing,
                y, y+amr::BLOCK_NY*spacing, z, z+amr::BLOCK_NZ*spacing);
            grid.dim = dimension; grid.geometry = name; grid.InitializeTopology();
            const auto geometry = GridMetrics::geometry_from_name(name);
            FluidState state;
            state.Preallocate(grid.GetTotalSize()); state.InitSpecies(1);
            for (int k=0; k<grid.GetTotalZ(); ++k)
            for (int j=0; j<grid.GetTotalY(); ++j)
            for (int i=0; i<grid.GetTotalX(); ++i) {
                const int cell = grid.GetIndex(i,j,k);
                state.set(cell, sample(geometry, dimension, grid.GetCellCenterX(i),
                    grid.GetCellCenterY(j), grid.GetCellCenterZ(k), uniform, density_slope).state);
                state.X(0,cell) = 1.;
            }
            const int i = grid.Is()+amr::BLOCK_NX/2;
            const int j = dimension >= 2 ? grid.Js()+amr::BLOCK_NY/2 : 0;
            const int k = dimension == 3 ? grid.Ks()+amr::BLOCK_NZ/2 : 0;
            const auto expected = sample(geometry, dimension, grid.GetCellCenterX(i),
                grid.GetCellCenterY(j), grid.GetCellCenterZ(k), uniform, density_slope);
            const double current = error(evaluate(state, grid).derivative[grid.GetIndex(i,j,k)], expected.derivative);
            std::cout << "VISCOUS_SPATIAL_CONVERGENCE backend=" << backend << " geometry=" << name
                      << " dim=" << dimension << " uniform=" << uniform << " density_slope=" << density_slope
                      << " h=" << spacing << " error=" << current << std::endl;
            if (!std::isfinite(current) || (previous > 1.e-10 && previous < 3.5*current))
                throw std::runtime_error("viscous momentum/work flux lost second-order consistency");
            previous = current;
        }
        if (previous > 1.e-4) throw std::runtime_error("viscous analytic derivative budget exceeded");
    }
}

// v=r e_r has zero force. Spherical 1D/3D is isotropic homology with tau=0;
// cylindrical/polar in-plane homology has div(tau*u)=(4/3)*nu because the
// third strain is zero. Include the first active cell touching r=0.
// Then assemble the actual 1D momentum operator from unit columns. A forward
// Euler matrix with nonnegative entries and row sums <=1 is a contraction in
// the max norm; this check does not reuse the production timestep derivation.
struct Contraction {
    double minimum_entry = 1.;
    double maximum_row_sum = 0.;
    double raw_dt = 0.;
};

template <typename Evaluate>
Contraction momentum_contraction(FluidState state, const Grid& grid, Evaluate evaluate)
{
    Contraction result{};
    result.raw_dt = evaluate(state, grid).raw_dt;
    if (!(result.raw_dt > 0.) || !std::isfinite(result.raw_dt))
        throw std::runtime_error("invalid diffusion stability limit");
    std::vector<double> row_sums(amr::BLOCK_NX, 0.);
    for (int column=0; column<amr::BLOCK_NX; ++column) {
        for (int i=0; i<grid.GetTotalX(); ++i) {
            const double rho = state.rho[i];
            const double velocity = i == grid.Is()+column ? 1. : 0.;
            state.set(i, {rho,rho*velocity,0.,0.,rho*(30.+.5*velocity*velocity)});
        }
        const auto basis = evaluate(state, grid);
        for (int row=0; row<amr::BLOCK_NX; ++row) {
            const int cell = grid.Is()+row;
            const double entry = (row == column ? 1. : 0.)
                + result.raw_dt*basis.derivative[cell].mom_u/state.rho[cell];
            result.minimum_entry = std::min(result.minimum_entry, entry);
            row_sums[row] += entry;
        }
    }
    result.maximum_row_sum = *std::max_element(row_sums.begin(), row_sums.end());
    if (!(result.minimum_entry >= -2.e-12 && result.maximum_row_sum <= 1.+2.e-12)) {
        std::cerr<<"VISCOUS_CONTRACTION_FAILURE geometry="<<grid.geometry
            <<" h="<<grid.dx1<<" raw_dt="<<result.raw_dt
            <<" rho_first="<<state.rho[grid.Is()]<<" rho_next="<<state.rho[grid.Is()+1]
            <<" minimum_entry="<<result.minimum_entry
            <<" maximum_row_sum="<<result.maximum_row_sum<<std::endl;
        throw std::runtime_error("forward Euler is not a velocity max-norm contraction");
    }
    return result;
}

template <typename Evaluate>
void radial_origin(const char* backend, Evaluate evaluate)
{
    for (const char* name : {"cylindrical", "spherical"})
    for (int dimension : {1, 2, 3})
    for (double spacing : {.025, .0125, .00625}) {
        if (dimension==2 && std::string_view(name)=="cylindrical") continue;
        Grid grid(amr::MAX_NG, 0., amr::BLOCK_NX*spacing, .5, 1., .2, .7);
        grid.dim = dimension; grid.geometry = name; grid.InitializeTopology();
        FluidState state;
        state.Preallocate(grid.GetTotalSize()); state.InitSpecies(1);
        for (int k=0; k<grid.GetTotalZ(); ++k)
        for (int j=0; j<grid.GetTotalY(); ++j)
        for (int i=0; i<grid.GetTotalX(); ++i) {
            const int cell = grid.GetIndex(i,j,k);
            const double r = grid.GetCellCenterX(i);
            state.set(cell, {1., r, 0., 0., 30.+.5*r*r});
            state.X(0,cell) = 1.;
        }
        const auto measured = evaluate(state, grid);
        const int j = dimension >= 2 ? grid.Js()+amr::BLOCK_NY/2 : 0;
        const int k = dimension == 3 ? grid.Ks()+amr::BLOCK_NZ/2 : 0;
        const bool isotropic = grid.geometry == "spherical" && dimension != 2;
        const double null_error = error(measured.derivative[grid.GetIndex(grid.Is(),j,k)],
                                       {0.,0.,0.,0.,isotropic?0.:(4./3.)*viscosity});
        std::cout << "VISCOUS_ORIGIN backend=" << backend << " geometry=" << name
                  << " dim=" << dimension << " h=" << spacing << " error=" << null_error << '\n';
        if (!(null_error <= 2.e-11)) throw std::runtime_error("radial linear field fails origin balance");
        if (dimension != 1) continue;

        const auto stability = momentum_contraction(state, grid, evaluate);
        std::cout << "VISCOUS_RADIAL_STABILITY backend=" << backend << " geometry=" << name
                  << " h=" << spacing << " minimum_entry=" << stability.minimum_entry
                  << " maximum_row_sum=" << stability.maximum_row_sum << '\n';
    }
}


/** Actual 1D Cartesian operator: test the mass-weighted energy, not a scalar
 * vector max-norm assumption. The original density/h/nu and recommended dt
 * remain. Prescribed zero ghost velocity is an external extension, not a
 * certified wall: include its physical face-power exchange explicitly.
 * Independent face algebra is tau=(4/3)*mu*du/dx longitudinal, mu*du/dx
 * transverse. Kdot=-sum_edges(mu*a*jump^2/h), Eflux=P_right-P_left;
 * K_FE-K=dt*Kdot+dt^2/2*sum_cells(V*F^2/rho).
 */
template <typename Evaluate>
void cartesian_density_energy(const char* backend,FluidState state,const Grid& grid,
                              double contrast,Evaluate evaluate)
{
    const long double h=grid.dx1,nu=viscosity;
    const double raw_dt=evaluate(state,grid).raw_dt;
    const long double old_scalar_dt=h*h/(nu*(contrast+1.L));
    const long double stokes_dt=3.L*old_scalar_dt/4.L;
    if(!std::isfinite(raw_dt)||!(raw_dt>0.)
       ||std::abs(static_cast<long double>(raw_dt)-stokes_dt)>2.e-12L*stokes_dt)
        throw std::runtime_error("Cartesian actual Stokes recommended timestep differs from independent row");
    long double maximum_energy_change=0.,maximum_identity_error=0.;
    for(int component=0;component<3;++component)for(int mode=0;mode<3;++mode) {
        const long double factor=component==0?4.L/3.L:1.L;
        std::vector<long double> velocity(grid.GetTotalX(),0.L);
        for(int i=grid.Is();i<grid.Ie();++i) {
            const int n=i-grid.Is();
            velocity[i]=mode==0?(n%2?1.L:-1.L):mode==1?1.L:static_cast<long double>(n+1)/amr::BLOCK_NX;
        }
        for(int i=0;i<grid.GetTotalX();++i) {
            const double rho=state.rho[i],v=static_cast<double>(velocity[i]);
            FluidVector value{rho,0.,0.,0.,rho*(30.+.5*v*v)};
            if(component==0)value.mom_u=rho*v;else if(component==1)value.mom_v=rho*v;else value.mom_w=rho*v;
            state.set(i,value);
        }
        const auto measured=evaluate(state,grid);
        if(measured.raw_dt!=raw_dt)throw std::runtime_error("Cartesian kinetic probe changed frozen recommended timestep");
        long double D=0.,Kdot=0.,Kbefore=0.,Kafter=0.,Eflux=0.,force_square=0.;
        long double left_power=0.,right_power=0.;
        for(int right=grid.Is();right<=grid.Ie();++right) {
            const int left=right-1;
            const long double mu=nu*(static_cast<long double>(state.rho[left])+state.rho[right])/2.L;
            const long double jump=velocity[right]-velocity[left];
            D+=factor*mu*jump*jump/h;
            const long double power=(velocity[left]+velocity[right])*factor*mu*jump/(2.L*h);
            if(right==grid.Is())left_power=power;
            if(right==grid.Ie())right_power=power;
        }
        for(int i=grid.Is();i<grid.Ie();++i) {
            const long double rho=state.rho[i];
            const long double mu_lower=nu*(rho+state.rho[i-1])/2.L,mu_upper=nu*(rho+state.rho[i+1])/2.L;
            const long double F=factor*(mu_upper*(velocity[i+1]-velocity[i])-mu_lower*(velocity[i]-velocity[i-1]))/(h*h);
            const auto& actual=measured.derivative[i];
            const long double actualF=component==0?actual.mom_u:component==1?actual.mom_v:actual.mom_w;
            if(std::abs(actualF-F)>2.e-12L*(std::abs(F)+factor*(mu_lower+mu_upper)/(h*h)))
                throw std::runtime_error("Cartesian longitudinal/transverse force independent edge identity");
            Kdot+=h*velocity[i]*actualF;force_square+=h*actualF*actualF/rho;
            Kbefore+=rho*h*velocity[i]*velocity[i]/2.L;
            const long double accepted_trial=velocity[i]+raw_dt*actualF/rho;
            Kafter+=rho*h*accepted_trial*accepted_trial/2.L;
            Eflux+=h*actual.eng;
            if(actual.rho!=0.)throw std::runtime_error("Cartesian viscous operator changed density");
        }
        const long double boundary_power=right_power-left_power;
        const long double identity_scale=D+std::abs(Kdot),energy_scale=D+std::abs(Eflux)+std::abs(boundary_power);
        const long double change=Kafter-Kbefore;
        const long double exact_change=raw_dt*Kdot+static_cast<long double>(raw_dt)*raw_dt*force_square/2.L;
        if(!(D>=0.)||std::abs(Kdot+D)>2.e-12L*identity_scale
           ||std::abs(Eflux-boundary_power)>2.e-12L*energy_scale
           ||std::abs(change-exact_change)>2.e-12L*(Kbefore+std::abs(exact_change))
           ||change>2.e-12L*Kbefore||Eflux-Kdot < -2.e-12L*energy_scale)
            throw std::runtime_error("Cartesian mass-energy dissipativity/FE/boundary-work identity");
        maximum_energy_change=std::max(maximum_energy_change,change);
        maximum_identity_error=std::max(maximum_identity_error,std::abs(Kdot+D));
    }
    std::cout<<"VISCOUS_CARTESIAN_DENSITY_ENERGY backend="<<backend<<" contrast="<<contrast
        <<" raw_dt="<<raw_dt<<" previous_scalar_dt_reference="<<static_cast<double>(old_scalar_dt)
        <<" stokes_dt_reference="<<static_cast<double>(stokes_dt)<<" max_energy_change="<<static_cast<double>(maximum_energy_change)
        <<" max_identity_error="<<static_cast<double>(maximum_identity_error)<<" boundary=prescribed_zero_ghost_extension\n";
}

template <typename Evaluate>
void density_stability(const char* backend, Evaluate evaluate)
{
    for (const char* name : {"cartesian", "cylindrical", "spherical"})
    for (double contrast : {1., 10., 100.}) {
        Grid grid(amr::MAX_NG, 0., 1., 0., 1., 0., 1.);
        grid.dim = 1; grid.geometry = name; grid.InitializeTopology();
        FluidState state;
        state.Preallocate(grid.GetTotalSize()); state.InitSpecies(1);
        for (int cell=0; cell<grid.GetTotalSize(); ++cell) {
            const double rho = cell % 2 ? contrast : 1.;
            state.set(cell, {rho,0.,0.,0.,30.*rho});
            state.X(0,cell) = 1.;
        }
        if (grid.geometry == "cartesian") {
            cartesian_density_energy(backend,state,grid,contrast,evaluate);
            continue;
        }
        const auto stability = momentum_contraction(state, grid, evaluate);
        std::cout << "VISCOUS_DENSITY_STABILITY backend=" << backend << " geometry=" << name
                  << " contrast=" << contrast << " minimum_entry=" << stability.minimum_entry
                  << " maximum_row_sum=" << stability.maximum_row_sum << '\n';
    }
}

/** Independent point-law inputs/closed analytical expectations, not a PDE oracle.
 * In RZ the physical component order is (r,z,phi). Gradients below include
 * the true connections G_rphi=-u_phi/r and G_phiphi=u_r/r through their exact
 * regular polynomial values, including r=0; the production law divides by no
 * radius. mu=rho*nu varies with z in the shear/swirl examples. Cartesian 1D
 * retains the same three-dimensional Stokes trace subtraction.
 */
struct NewtonianPointCase {
    NewtonianViscousStress::VelocityGradient gradient{};
    NewtonianViscousStress::Vector velocity{};
    NewtonianViscousStress::Tensor expected_stress{};
    std::array<double,3> expected_power{};
    double density=1.,nu=0.,expected_dissipation=0.;
    bool exactly_stress_free=false;
};

/** Hand-derived closed examples; long-double expressions own expected values.
 * No production stress, gradient, connection or flux function constructs any
 * expected tensor, power or dissipation. These only qualify a point law.
 */
inline std::vector<NewtonianPointCase> newtonian_point_cases()
{
    std::vector<NewtonianPointCase> cases;
    for(long double z:{-.75L,.75L})for(long double r:{0.L,.5L,1.5L,-.5L}) {
        const long double a=.5L,b=-.25L,nu=.125L;
        const long double rho=1.L+z/4.L,mu=rho*nu;
        NewtonianPointCase homology{};
        homology.density=static_cast<double>(rho);homology.nu=static_cast<double>(nu);
        homology.gradient={static_cast<double>(a),0.,0.,0.,static_cast<double>(a),0.,0.,0.,static_cast<double>(a)};
        homology.velocity={static_cast<double>(a*r),static_cast<double>(a*z),0.};
        homology.exactly_stress_free=true;cases.push_back(homology);
        auto anisotropic=homology;anisotropic.exactly_stress_free=false;
        anisotropic.gradient[4]=static_cast<double>(b);anisotropic.velocity[1]=static_cast<double>(b*z);
        const long double d=a-b,rr=2.L*mu*d/3.L,zz=-4.L*mu*d/3.L;
        anisotropic.expected_stress[0]=anisotropic.expected_stress[8]=static_cast<double>(rr);
        anisotropic.expected_stress[4]=static_cast<double>(zz);
        anisotropic.expected_power={static_cast<double>(a*r*rr),static_cast<double>(b*z*zz),0.};
        anisotropic.expected_dissipation=static_cast<double>(4.L*mu*d*d/3.L);cases.push_back(anisotropic);
        NewtonianPointCase shear{};shear.density=static_cast<double>(rho);shear.nu=static_cast<double>(nu);
        shear.gradient[3]=static_cast<double>(a*r);shear.velocity[1]=static_cast<double>(a*r*r/2.L);
        shear.expected_stress[1]=shear.expected_stress[3]=static_cast<double>(mu*a*r);
        shear.expected_power[0]=static_cast<double>(mu*a*a*r*r*r/2.L);
        shear.expected_dissipation=static_cast<double>(mu*a*a*r*r);cases.push_back(shear);
        NewtonianPointCase rotation{};rotation.density=static_cast<double>(rho);rotation.nu=static_cast<double>(nu);
        rotation.gradient[2]=-2.;rotation.gradient[6]=2.;rotation.velocity[2]=static_cast<double>(2.L*r);
        rotation.exactly_stress_free=true;cases.push_back(rotation);
        NewtonianPointCase swirl{};swirl.density=static_cast<double>(rho);swirl.nu=static_cast<double>(nu);
        const long double c=.25L,tau=2.L*mu*c*r*r;
        swirl.gradient[2]=static_cast<double>(-c*r*r);swirl.gradient[6]=static_cast<double>(3.L*c*r*r);
        swirl.velocity[2]=static_cast<double>(c*r*r*r);
        swirl.expected_stress[2]=swirl.expected_stress[6]=static_cast<double>(tau);
        swirl.expected_power[0]=static_cast<double>(2.L*mu*c*c*r*r*r*r*r);
        swirl.expected_dissipation=static_cast<double>(4.L*mu*c*c*r*r*r*r);cases.push_back(swirl);
        NewtonianPointCase cartesian{};cartesian.density=static_cast<double>(rho);cartesian.nu=static_cast<double>(nu);
        cartesian.gradient[0]=static_cast<double>(a);cartesian.velocity[0]=static_cast<double>(a*r);
        cartesian.expected_stress[0]=static_cast<double>(4.L*mu*a/3.L);
        cartesian.expected_stress[4]=cartesian.expected_stress[8]=static_cast<double>(-2.L*mu*a/3.L);
        cartesian.expected_power[0]=static_cast<double>(4.L*mu*a*a*r/3.L);
        cartesian.expected_dissipation=static_cast<double>(4.L*mu*a*a/3.L);cases.push_back(cartesian);
        homology.nu=0.;cases.push_back(homology);
    }
    // A finite enormous coefficient must not destroy the exact zero law.
    auto huge=cases[0];huge.density=1.;huge.nu=std::numeric_limits<double>::max();cases.push_back(huge);
    return cases;
}

/** Execute the same shared point law against independent expectations.
 * The single frozen 64-epsilon rounding window compares components/power/Q;
 * exact homology/rotation/zero-mu cases additionally require exact numerical zero.
 * Backend executors supply runtime-uploaded identical case values.
 */
ARCH_INLINE double newtonian_point_error(const NewtonianPointCase& input)
{
    using namespace NewtonianViscousStress;
    const double invalid=std::numeric_limits<double>::infinity();
    Tensor tensor{};double q=0.,maximum=0.;
    if(!stress(input.gradient,input.density*input.nu,tensor)
        ||!dissipation(tensor,input.gradient,q))return invalid;
    for(int field=0;field<9;++field) {
        if(input.exactly_stress_free&&tensor[field]!=0.)return invalid;
        maximum=std::max(maximum,std::abs(tensor[field]-input.expected_stress[field])
            /std::max(1.,std::abs(input.expected_stress[field])));
    }
    maximum=std::max(maximum,std::abs(q-input.expected_dissipation)
        /std::max(1.,std::abs(input.expected_dissipation)));
    for(int direction=0;direction<3;++direction) {
        Vector face{};double work=0.;
        if(!traction(tensor,direction,face)||!power(input.velocity,face,work))return invalid;
        for(int component=0;component<3;++component)
            maximum=std::max(maximum,std::abs(face[component]-input.expected_stress[3*component+direction])
                /std::max(1.,std::abs(input.expected_stress[3*component+direction])));
        maximum=std::max(maximum,std::abs(work-input.expected_power[direction])
            /std::max(1.,std::abs(input.expected_power[direction])));
    }
    return maximum;
}

/** Exact failure/no-publication witnesses, shared by host and device fixtures.
 * Invalid coefficient/gradient, tensor/velocity/direction and unrepresentable
 * tensor or contraction must not replace caller sentinels. No epsilon cutoff,
 * floor, stress clipping or fabricated coefficient is used in these negatives.
 */
ARCH_INLINE bool newtonian_point_guards(const NewtonianPointCase& input)
{
    using namespace NewtonianViscousStress;
    const double nan=std::numeric_limits<double>::quiet_NaN();
    Tensor sentinel{};sentinel.fill(42.);Tensor output=sentinel;
    if(stress(input.gradient,-1.,output)||output!=sentinel)return false;
    if(stress(input.gradient,nan,output)||output!=sentinel)return false;
    if(stress(input.gradient,std::numeric_limits<double>::infinity(),output)||output!=sentinel)return false;
    for(int component=0;component<9;++component) {
        auto bad=input.gradient;bad[component]=nan;
        if(stress(bad,0.,output)||output!=sentinel)return false;
    }
    VelocityGradient huge{};huge[0]=std::numeric_limits<double>::max();huge[4]=-huge[0];
    if(stress(huge,1.,output)||output!=sentinel)return false;
    // mu==0 needs no finite differences/sums, but still requires finite inputs.
    if(!stress(huge,0.,output))return false;
    for(double component:output)if(component!=0.)return false;
    const Vector vector_sentinel{41.,43.,47.};Vector face=vector_sentinel;
    if(traction(sentinel,-1,face)||face!=vector_sentinel)return false;
    if(traction(sentinel,3,face)||face!=vector_sentinel)return false;
    auto invalid_tensor=sentinel;invalid_tensor[0]=nan;
    if(traction(invalid_tensor,0,face)||face!=vector_sentinel)return false;
    double scalar=53.;auto invalid_velocity=input.velocity;invalid_velocity[0]=nan;
    if(power(invalid_velocity,Vector{},scalar)||scalar!=53.)return false;
    if(power(Vector{std::numeric_limits<double>::max(),0.,0.},Vector{2.,0.,0.},scalar)||scalar!=53.)return false;
    if(dissipation(invalid_tensor,input.gradient,scalar)||scalar!=53.)return false;
    Tensor negative{};negative[0]=-1.;VelocityGradient positive{};positive[0]=1.;
    if(dissipation(negative,positive,scalar)||scalar!=53.)return false;
    Tensor enormous{};enormous[0]=std::numeric_limits<double>::max();positive[0]=2.;
    if(dissipation(enormous,positive,scalar)||scalar!=53.)return false;
    return true;
}

/** Qualify the analytical point law on this backend, without a PDE claim. */
inline void newtonian_constitutive()
{
    double maximum=0.;const auto cases=newtonian_point_cases();
    for(const auto& input:cases) {
        maximum=std::max(maximum,newtonian_point_error(input));
        if(!newtonian_point_guards(input))throw std::runtime_error("Newtonian point-law failure publication contract");
    }
    if(!std::isfinite(maximum)||maximum>64.*std::numeric_limits<double>::epsilon())
        throw std::runtime_error("Newtonian analytical point-law 64-epsilon window");
    std::cout<<"NEWTONIAN_CONSTITUTIVE_POINT cases="<<cases.size()<<" max_error="<<maximum<<'\n';
}

/** Analytical physical fields, expressed in several orthonormal frames.
 * Expected gradients come from affine dilation, rigid rotation and a constant
 * Cartesian vector; they are independent of the connection helper's indexing.
 * These small values are plain shared-test data, not an axis/pole or PDE proof.
 */
struct CovariantPointCase {
    NewtonianViscousStress::Frame frame=NewtonianViscousStress::Frame::Cartesian;
    NewtonianViscousStress::Vector velocity{};
    NewtonianViscousStress::VelocityGradient partials{},expected_gradient{};
    NewtonianViscousStress::Tensor expected_stress{};
    double radius=2.,theta=.7,mu=.125;
};

/** Build physical directional partials before basis-connection terms.
 * Dilation has G=aI and zero Stokes stress in every frame. Rigid rotation's
 * gradient is its skew Cartesian generator projected into the local frame.
 * Translation has identically zero full gradient although spherical component
 * derivatives are nonzero. Trigonometric inputs retain the original rounding
 * window rather than requiring an artificial bitwise cancellation.
 */
inline std::vector<CovariantPointCase> covariant_point_cases()
{
    using namespace NewtonianViscousStress;
    std::vector<CovariantPointCase> cases;
    CovariantPointCase affine;affine.radius=-2.;affine.theta=-.3;
    affine.velocity={1.,2.,3.};affine.partials={1.,0.,0.,0.,2.,0.,0.,0.,3.};
    affine.expected_gradient=affine.partials;affine.expected_stress={-.25,0.,0.,0.,0.,0.,0.,0.,.25};
    cases.push_back(affine);
    CovariantPointCase dilation;dilation.frame=Frame::CylindricalRZPhi;
    dilation.velocity={1.,-.5,0.};dilation.partials[0]=.5;dilation.partials[4]=.5;
    dilation.expected_gradient={.5,0.,0.,0.,.5,0.,0.,0.,.5};cases.push_back(dilation);
    dilation.frame=Frame::CylindricalRPhiZ;dilation.velocity={1.,0.,-.5};
    dilation.partials={.5,0.,0.,0.,0.,0.,0.,0.,.5};cases.push_back(dilation);
    CovariantPointCase rotation;rotation.frame=Frame::CylindricalRZPhi;
    rotation.velocity={0.,0.,1.5};rotation.partials[6]=.75;
    rotation.expected_gradient={0.,0.,-.75,0.,0.,0.,.75,0.,0.};cases.push_back(rotation);
    rotation.frame=Frame::CylindricalRPhiZ;rotation.velocity={0.,1.5,0.};
    rotation.partials={0.,0.,0.,.75,0.,0.,0.,0.,0.};
    rotation.expected_gradient={0.,-.75,0.,.75,0.,0.,0.,0.,0.};cases.push_back(rotation);
    dilation.frame=Frame::SphericalRThetaPhi;dilation.velocity={1.,0.,0.};
    dilation.partials={.5,0.,0.,0.,0.,0.,0.,0.,0.};cases.push_back(dilation);
    CovariantPointCase sphere_rotation;sphere_rotation.frame=Frame::SphericalRThetaPhi;
    const double s=std::sin(sphere_rotation.theta),c=std::cos(sphere_rotation.theta);
    const double omega=.75;
    sphere_rotation.velocity={0.,0.,omega*sphere_rotation.radius*s};
    sphere_rotation.partials[6]=omega*s;sphere_rotation.partials[7]=omega*c;
    sphere_rotation.expected_gradient={0.,0.,-omega*s,0.,0.,-omega*c,omega*s,omega*c,0.};
    cases.push_back(sphere_rotation);
    CovariantPointCase translation;translation.frame=Frame::SphericalRThetaPhi;
    const double speed=.5;
    translation.velocity={speed*c,-speed*s,0.};
    translation.partials[1]=-speed*s/translation.radius;
    translation.partials[4]=-speed*c/translation.radius;cases.push_back(translation);
    return cases;
}

/** Shared CPU/device numerical comparison against the physical-field data.
 * Workflow: form the full gradient, apply the unchanged constitutive law, and
 * compare every component to the independent analytic tensor. No EOS, field
 * update, runtime geometry permission or allocation is performed here.
 */
ARCH_INLINE double covariant_point_error(const CovariantPointCase& input)
{
    using namespace NewtonianViscousStress;
    VelocityGradient gradient{};Tensor tensor{};double maximum=0.;
    if(!physical_covariant_gradient(input.frame,input.velocity,input.partials,input.radius,input.theta,gradient)
        ||!stress(gradient,input.mu,tensor))return std::numeric_limits<double>::infinity();
    for(int component=0;component<9;++component) {
        maximum=std::max(maximum,std::abs(gradient[component]-input.expected_gradient[component])
            /std::max(1.,std::abs(input.expected_gradient[component])));
        maximum=std::max(maximum,std::abs(tensor[component]-input.expected_stress[component])
            /std::max(1.,std::abs(input.expected_stress[component])));
    }
    return maximum;
}

/** Failure is atomic even when output aliases the original partials.
 * Geometry endpoints, nonfinite inputs and unrepresentable connection/result
 * values are strict invalid data, not regular axis or spherical-pole limits.
 */
ARCH_INLINE bool covariant_point_guards()
{
    using namespace NewtonianViscousStress;
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const double inf=std::numeric_limits<double>::infinity();
    const double huge=std::numeric_limits<double>::max();
    VelocityGradient sentinel{};sentinel.fill(42.);VelocityGradient output=sentinel;
    const VelocityGradient zero{};const Vector velocity{};
    if(physical_covariant_gradient(static_cast<Frame>(255),velocity,zero,2.,.7,output)||output!=sentinel)return false;
    for(int component=0;component<9;++component) {
        auto bad=zero;bad[component]=nan;
        if(physical_covariant_gradient(Frame::Cartesian,velocity,bad,-2.,.7,output)||output!=sentinel)return false;
    }
    for(int component=0;component<3;++component) {
        auto bad=velocity;bad[component]=inf;
        if(physical_covariant_gradient(Frame::Cartesian,bad,zero,-2.,.7,output)||output!=sentinel)return false;
    }
    if(physical_covariant_gradient(Frame::Cartesian,velocity,zero,nan,.7,output)||output!=sentinel)return false;
    if(physical_covariant_gradient(Frame::Cartesian,velocity,zero,-2.,inf,output)||output!=sentinel)return false;
    const Frame curved_frames[3]={Frame::CylindricalRZPhi,Frame::CylindricalRPhiZ,Frame::SphericalRThetaPhi};
    for(const auto frame:curved_frames) {
        if(physical_covariant_gradient(frame,velocity,zero,0.,.7,output)||output!=sentinel)return false;
        if(physical_covariant_gradient(frame,velocity,zero,-2.,.7,output)||output!=sentinel)return false;
    }
    if(physical_covariant_gradient(Frame::SphericalRThetaPhi,velocity,zero,2.,0.,output)||output!=sentinel)return false;
    if(physical_covariant_gradient(Frame::SphericalRThetaPhi,velocity,zero,2.,3.14159265358979323846,output)||output!=sentinel)return false;
    if(physical_covariant_gradient(Frame::CylindricalRZPhi,Vector{0.,0.,huge},zero,.5,.7,output)||output!=sentinel)return false;
    // Required nonzero unrepresentable connections must not disappear.
    const double tiny=std::numeric_limits<double>::denorm_min();
    if(physical_covariant_gradient(Frame::CylindricalRZPhi,Vector{tiny,0.,0.},zero,huge,.7,output)||output!=sentinel)return false;
    if(physical_covariant_gradient(Frame::SphericalRThetaPhi,Vector{0.,0.,tiny},zero,1.,1.5,output)||output!=sentinel)return false;
    auto alias=zero;alias[8]=huge;const auto before=alias;
    if(physical_covariant_gradient(Frame::CylindricalRZPhi,Vector{huge,0.,0.},alias,1.,.7,alias)||alias!=before)return false;
    alias={1.,0.,0.,0.,2.,0.,0.,0.,3.};const auto expected=alias;
    if(!physical_covariant_gradient(Frame::Cartesian,velocity,alias,-2.,-.3,alias)||alias!=expected)return false;
    return true;
}

/** Host owner of eight analytic point cases; no complete viscous PDE claim. */
inline void newtonian_covariant_gradient()
{
    double maximum=0.;const auto cases=covariant_point_cases();
    for(const auto& input:cases)maximum=std::max(maximum,covariant_point_error(input));
    if(!covariant_point_guards())throw std::runtime_error("Covariant point-gradient atomic failure contract");
    if(!std::isfinite(maximum)||maximum>64.*std::numeric_limits<double>::epsilon())
        throw std::runtime_error("Covariant analytical point-gradient 64-epsilon window");
    std::cout<<"NEWTONIAN_COVARIANT_POINT cases="<<cases.size()<<" max_error="<<maximum<<'\n';
}


/** Analytical physical face inputs. The expected vectors are hand-derived
 * from affine dilation/rotation, 2D three-component shear and unequal mu;
 * no production tensor/gradient/face function constructs the references.
 */
struct PairedTractionCase {
    int direction=0;double spacing=1.,mu_left=1.,mu_right=1.;
    NewtonianViscousStress::Vector left{},right{},expected{};
    NewtonianViscousStress::VelocityGradient left_gradient{},right_gradient{};
    bool exact_zero=false;
};
inline std::vector<PairedTractionCase> paired_traction_cases()
{
    std::vector<PairedTractionCase> cases;
    // Unequal coefficients must average mu*G, not average mu times average G.
    PairedTractionCase unequal{};unequal.spacing=2.;unequal.mu_left=2.;unequal.mu_right=6.;
    unequal.left={1.,2.,3.};unequal.right={5.,8.,13.};
    unequal.left_gradient={1.,7.,-1.,0.,2.,0.,0.,0.,3.};
    unequal.right_gradient={4.,11.,-3.,0.,5.,0.,0.,0.,6.};
    unequal.expected={static_cast<double>(-44.L/3.L),52.,10.};cases.push_back(unequal);
    PairedTractionCase homology{};homology.mu_left=homology.mu_right=1.5;
    homology.right={.5,0.,0.};homology.left_gradient=homology.right_gradient={.5,0.,0.,0.,.5,0.,0.,0.,.5};
    homology.exact_zero=true;cases.push_back(homology);
    PairedTractionCase rotation{};rotation.right={0.,2.,0.};
    rotation.left_gradient=rotation.right_gradient={0.,-2.,0.,2.,0.,0.,0.,0.,0.};
    rotation.exact_zero=true;cases.push_back(rotation);
    PairedTractionCase shear{};shear.mu_left=2.;shear.mu_right=6.;shear.right={0.,3.,2.};
    shear.left_gradient= shear.right_gradient={0.,0.,0.,3.,0.,0.,2.,0.,0.};
    shear.expected={0.,12.,8.};cases.push_back(shear);
    PairedTractionCase trace{};trace.direction=1;trace.mu_left=trace.mu_right=3.;trace.right={0.,2.,0.};
    trace.left_gradient=trace.right_gradient={1.,0.,0.,0.,2.,0.,0.,0.,3.};
    trace.exact_zero=true;cases.push_back(trace);
    PairedTractionCase stretch{};stretch.direction=2;stretch.mu_left=stretch.mu_right=2.;stretch.right={0.,0.,1.};
    stretch.left_gradient=stretch.right_gradient={0.,0.,0.,0.,0.,0.,0.,0.,1.};
    stretch.expected={0.,0.,static_cast<double>(8.L/3.L)};cases.push_back(stretch);
    return cases;
}
/** Run only the real shared face leaf; finite sentinels remain unchanged on
 * failure. Expected zero components require exact zero rather than max(1,q).
 */
ARCH_INLINE double paired_traction_error(const PairedTractionCase& input)
{
    using namespace NewtonianViscousStress;Vector output{};
    if(!cartesian_paired_traction(input.direction,input.spacing,input.left,input.right,
        input.left_gradient,input.right_gradient,input.mu_left,input.mu_right,output))
        return std::numeric_limits<double>::infinity();
    double error=0.;
    for(int n=0;n<3;++n) {
        if(input.expected[n]==0.) {if(output[n]!=0.)return std::numeric_limits<double>::infinity();}
        else error=std::max(error,std::abs((output[n]-input.expected[n])/input.expected[n]));
    }
    return error;
}
/** Strict input validation precedes zero-mu. Invalid controls, NaN/Inf inputs,
 * overflow and aliased output cannot partially replace the caller's vector.
 */
ARCH_INLINE bool paired_traction_guards()
{
    using namespace NewtonianViscousStress;
    const double nan=std::numeric_limits<double>::quiet_NaN(),inf=std::numeric_limits<double>::infinity();
    const double huge=std::numeric_limits<double>::max();
    const Vector sentinel{41.,43.,47.};Vector output=sentinel;VelocityGradient zero{};
    // Independent binary64 dyadic extremes, not sampled production goldens.
    // A positive physical coefficient never becomes the disabled zero branch.
    const double tiny=std::numeric_limits<double>::denorm_min();
    double coefficient=41.;
    for(double rho:{0.,-1.,nan,inf})
        if(dynamic_viscosity(rho,0.,coefficient)||coefficient!=41.)return false;
    for(double nu:{-1.,nan,inf})
        if(dynamic_viscosity(1.,nu,coefficient)||coefficient!=41.)return false;
    if(dynamic_viscosity(tiny,.5,coefficient)||coefficient!=41.)return false;
    if(dynamic_viscosity(huge,2.,coefficient)||coefficient!=41.)return false;
    if(!dynamic_viscosity(tiny,0.,coefficient)||coefficient!=0.)return false;
    if(!dynamic_viscosity(tiny,1.,coefficient)||coefficient!=tiny)return false;
    if(!nonnegative_coefficient_mean(tiny,tiny,coefficient)||coefficient!=tiny)return false;
    if(!nonnegative_coefficient_mean(tiny,2.*tiny,coefficient)||coefficient!=2.*tiny)return false;
    if(!nonnegative_coefficient_mean(3.*tiny,3.*tiny,coefficient)||coefficient!=3.*tiny)return false;
    if(!nonnegative_coefficient_mean(huge,tiny,coefficient)||coefficient!=.5*huge)return false;
    coefficient=41.;
    if(nonnegative_coefficient_mean(tiny,0.,coefficient)||coefficient!=41.)return false;
    for(double bad:{-1.,nan,inf})
        if(nonnegative_coefficient_mean(bad,1.,coefficient)||coefficient!=41.)return false;
    if(!cartesian_paired_traction(0,1.,Vector{},Vector{0.,std::ldexp(1.,1020),0.},
        zero,zero,tiny,tiny,output)||output!=Vector{0.,std::ldexp(1.,-54),0.})return false;
    output=sentinel;
    if(cartesian_paired_traction(0,1.,Vector{},Vector{},zero,zero,tiny,0.,output)||output!=sentinel)return false;
    for(int direction:{-1,3})if(cartesian_paired_traction(direction,1.,Vector{},Vector{},zero,zero,0.,0.,output)||output!=sentinel)return false;
    for(double spacing:{0.,-1.,nan,inf})if(cartesian_paired_traction(0,spacing,Vector{},Vector{},zero,zero,0.,0.,output)||output!=sentinel)return false;
    for(double mu:{-1.,nan,inf})for(int side=0;side<2;++side)
        if(cartesian_paired_traction(0,1.,Vector{},Vector{},zero,zero,side?0.:mu,side?mu:0.,output)||output!=sentinel)return false;
    for(double bad:{nan,inf}) {
        for(int n=0;n<9;++n)for(int side=0;side<2;++side) {
            auto gradient=zero;gradient[n]=bad;
            if(cartesian_paired_traction(0,1.,Vector{},Vector{},side?zero:gradient,side?gradient:zero,0.,0.,output)||output!=sentinel)return false;
        }
        for(int n=0;n<3;++n)for(int side=0;side<2;++side) {
            Vector velocity{};velocity[n]=bad;
            if(cartesian_paired_traction(0,1.,side?Vector{}:velocity,side?velocity:Vector{},zero,zero,0.,0.,output)||output!=sentinel)return false;
        }
    }
    auto enormous=zero;enormous.fill(huge);
    if(cartesian_paired_traction(0,1.,Vector{-huge,0.,0.},Vector{huge,0.,0.},zero,zero,1.,1.,output)||output!=sentinel)return false;
    if(cartesian_paired_traction(0,1.,Vector{},Vector{},enormous,enormous,1.,1.,output)||output!=sentinel)return false;
    // Finite enormous operands remain legal when both actual coefficients vanish.
    if(!cartesian_paired_traction(0,1.,Vector{-huge,0.,0.},Vector{huge,0.,0.},enormous,enormous,0.,0.,output)||output!=Vector{})return false;
    Vector alias{-huge,0.,0.};const auto original=alias;
    if(cartesian_paired_traction(0,1.,alias,Vector{huge,0.,0.},zero,zero,1.,1.,alias)||alias!=original)return false;
    alias={0.,0.,0.};
    if(!cartesian_paired_traction(0,1.,alias,Vector{0.,0.,2.},zero,zero,2.,2.,alias)||alias!=Vector{0.,0.,4.})return false;
    return true;
}

/** Exact public periodic witness, indexed x*3+y (not image row order).
 * h=nu=1, positive rho, all three physical velocity components retained.
 * Fixed-size values can be uploaded unchanged to the real device executor.
 */
struct PairedPeriodicCase {
    std::array<double,9> rho{};
    std::array<NewtonianViscousStress::Vector,9> velocity{};
};
inline PairedPeriodicCase paired_periodic_case()
{
    PairedPeriodicCase input{};input.rho.fill(.001);input.rho[0]=1.;
    constexpr double u[9]{0.,-15.,15.,0.,-34.,34.,0.,-34.,34.};
    constexpr double v[9]{0.,0.,0.,15.,34.,34.,-15.,-34.,-34.};
    for(int n=0;n<9;++n)input.velocity[n]={u[n],v[n],0.};return input;
}
ARCH_INLINE int paired_periodic_index(int x,int y)
{return ((x+3)%3)*3+(y+3)%3;}
/** Evaluate actual corrected tractions once per unique periodic face, and
 * independently gather conservative impulses and the SAME face work. Then
 * sum u dot div(tau) to verify discrete integration by parts: Kdot=-D,
 * total Eflux=0, thermal conversion D>=0. This is a spatial face-law witness,
 * not finite-step/RKL positivity or a whole diffusion/Runtime qualification.
 */
ARCH_INLINE bool paired_periodic_work(const PairedPeriodicCase& input,double* output)
{
    using namespace NewtonianViscousStress;
    std::array<VelocityGradient,9> gradients{};std::array<Vector,9> divergence{};
    std::array<double,9> energy{};
    for(int x=0;x<3;++x)for(int y=0;y<3;++y) {
        const int cell=paired_periodic_index(x,y);
        for(int component=0;component<3;++component) {
            gradients[cell][3*component]=.5*(input.velocity[paired_periodic_index(x+1,y)][component]-input.velocity[paired_periodic_index(x-1,y)][component]);
            gradients[cell][3*component+1]=.5*(input.velocity[paired_periodic_index(x,y+1)][component]-input.velocity[paired_periodic_index(x,y-1)][component]);
        }
    }
    double work=0.,naive_work=0.,work_scale=0.;
    for(int x=0;x<3;++x)for(int y=0;y<3;++y)for(int direction=0;direction<2;++direction) {
        const int left=paired_periodic_index(x,y),right=paired_periodic_index(x+(direction==0),y+(direction==1));
        Vector traction_value{};
        if(!cartesian_paired_traction(direction,1.,input.velocity[left],input.velocity[right],gradients[left],gradients[right],input.rho[left],input.rho[right],traction_value))return false;
        const double mu_face=.5*(input.rho[left]+input.rho[right]);
        Vector face_velocity{},naive{};
        // This old law is a diagnostic negative control only. Its exact public
        // work is -250661/1000, independently established with rational sums.
        for(int n=0;n<3;++n) {
            const double jump=input.velocity[right][n]-input.velocity[left][n];
            face_velocity[n]=.5*(input.velocity[left][n]+input.velocity[right][n]);
            if(n==direction) {
                double trace=0.;for(int k=0;k<3;++k)if(k!=direction)trace+=.5*(gradients[left][3*k+k]+gradients[right][3*k+k]);
                naive[n]=mu_face*((4./3.)*jump-(2./3.)*trace);
            } else naive[n]=mu_face*(jump+.5*(gradients[left][3*direction+n]+gradients[right][3*direction+n]));
            const double term=jump*traction_value[n];work+=term;work_scale+=std::abs(term);naive_work+=jump*naive[n];
            divergence[left][n]+=traction_value[n];divergence[right][n]-=traction_value[n];
        }
        double face_power=0.;if(!power(face_velocity,traction_value,face_power))return false;
        energy[left]+=face_power;energy[right]-=face_power;
    }
    double kinetic=0.,kinetic_scale=0.,total_energy=0.,energy_scale=0.;
    for(int cell=0;cell<9;++cell) {
        total_energy+=energy[cell];energy_scale+=std::abs(energy[cell]);
        for(int n=0;n<3;++n) {const double term=input.velocity[cell][n]*divergence[cell][n];kinetic+=term;kinetic_scale+=std::abs(term);}
    }
    const double window=64.*std::numeric_limits<double>::epsilon();
    if(!std::isfinite(work)||work<0.||!std::isfinite(work_scale)
       ||!std::isfinite(kinetic)||!std::isfinite(kinetic_scale)
       ||!std::isfinite(total_energy)||!std::isfinite(energy_scale)
       ||!std::isfinite(naive_work)||!(naive_work<0.)
       ||std::abs(work-static_cast<double>(17027.L/500.L))>window*work_scale
       ||std::abs(naive_work-static_cast<double>(-250661.L/1000.L))>window*work_scale
       ||std::abs(kinetic+work)>window*(work_scale+kinetic_scale)
       ||std::abs(total_energy)>window*energy_scale)return false;
    output[0]=work;output[1]=naive_work;output[2]=kinetic;output[3]=total_energy;return true;
}
/** Host entry for the same shared scalar and periodic array witnesses. */
inline void newtonian_paired_faces()
{
    const auto cases=paired_traction_cases();double error=0.;
    for(const auto& input:cases)error=std::max(error,paired_traction_error(input));
    double work[4]{};
    if(!std::isfinite(error)||error>64.*std::numeric_limits<double>::epsilon()
       ||!paired_traction_guards()||!paired_periodic_work(paired_periodic_case(),work))
        throw std::runtime_error("Product-weighted paired Cartesian face/work contract");
    std::cout<<"NEWTONIAN_PAIRED_CARTESIAN_FACE cases="<<cases.size()<<" max_error="<<error
        <<" corrected_D="<<work[0]<<" naive_D="<<work[1]<<" Kdot="<<work[2]<<" Eflux="<<work[3]<<'\n';
}

} // namespace ViscousGeometryCases
