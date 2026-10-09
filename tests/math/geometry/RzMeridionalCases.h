// Independent native V/W polynomial means and full meridional Stokes RHS.
#pragma once

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

#include "math/geometry/RzMetricCases.h"
#include "numerics/diffusion/DiffFlux.h"
#include "physics/eos/IdealGas.h"

namespace RzMeridionalCases {

/** A true continuum field, integrated independently before entering native U.
 * rho=1+a*r+b*z, ur=A*r^3, uz=B*z^3, uphi=Omega*r.
 * mr,mz,E use r dr dz; mphi uses r^2 dr dz. The rotational strain vanishes,
 * so the meridional/trace reference is independent of the angular graph.
 */
struct Polynomial {
    long double A=.2L,B=.15L,Omega=.25L,a=0.,b=0.,nu=.03L;

    /** Exact one-dimensional antiderivatives, with separate V and W weights. */
    FluidVector means(const Grid& grid,int i,int j) const
    {
        const long double r0=grid.GetFacePosL(i),r1=grid.GetFacePosR(i);
        const long double z0=grid.x2_min+(j-grid.ng)*grid.dx2,z1=z0+grid.dx2;
        const auto R=[&](int p,int weight=1){return RzMetricCases::mean_power(r0,r1,p,weight);};
        const auto Z=[&](int p){return RzMetricCases::mean_power(z0,z1,p,0);};
        const long double rho=1.L+a*R(1)+b*Z(1);
        const long double radial=A*(R(3)+a*R(4)+b*R(3)*Z(1));
        const long double axial=B*(Z(3)+a*R(1)*Z(3)+b*Z(4));
        const long double angular=Omega*(R(1,2)+a*R(2,2)+b*R(1,2)*Z(1));
        const long double E=100.L*rho
            +A*A*(R(6)+a*R(7)+b*R(6)*Z(1))/2.L
            +B*B*(Z(6)+a*R(1)*Z(6)+b*Z(7))/2.L
            +Omega*Omega*(R(2)+a*R(3)+b*R(2)*Z(1))/2.L;
        return {static_cast<double>(rho),static_cast<double>(radial),static_cast<double>(axial),
            static_cast<double>(angular),static_cast<double>(E)};
    }

    /** True V-average of div(tau) and div(tau*u), from closed polynomials.
     * tau_rr=mu*((10/3)A*r^2-2B*z^2),
     * tau_zz=mu*(4B*z^2-(8/3)A*r^2),
     * tau_pp=mu*(-(2/3)A*r^2-2B*z^2), tau_rz=0.
     * Q=mu*((28/3)A^2*r^4+12B^2*z^4-16AB*r^2*z^2).
     * RHS_E=ur*RHS_r+uz*RHS_z+Q is a reference identity only;
     * production total energy must use the paired face work once.
     */
    FluidVector rhs(const Grid& grid,int i,int j) const
    {
        const long double r0=grid.GetFacePosL(i),r1=grid.GetFacePosR(i);
        const long double z0=grid.x2_min+(j-grid.ng)*grid.dx2,z1=z0+grid.dx2;
        const auto R=[&](int p){return RzMetricCases::mean_power(r0,r1,p,1);};
        const auto Z=[&](int p){return RzMetricCases::mean_power(z0,z1,p,0);};
        const auto rho=[&](int rp,int zp){return R(rp)*Z(zp)+a*R(rp+1)*Z(zp)+b*R(rp)*Z(zp+1);};
        const long double fr=nu*(32.L*A*rho(1,0)/3.L
            +a*(10.L*A*R(2)/3.L-2.L*B*Z(2)));
        const long double fz=nu*(8.L*B*rho(0,1)
            +b*(4.L*B*Z(2)-8.L*A*R(2)/3.L));
        const long double E=nu*(20.L*A*A*rho(4,0)+20.L*B*B*rho(0,4)
            -16.L*A*B*rho(2,2)+a*(10.L*A*A*R(5)/3.L-2.L*A*B*R(3)*Z(2))
            +b*(4.L*B*B*Z(5)-8.L*A*B*R(2)*Z(3)/3.L));
        return {0.,static_cast<double>(fr),static_cast<double>(fz),0.,static_cast<double>(E)};
    }
};

/** Independent periodic axial field with optional closed-wall radial motion.
 * rho is constant; uz=B*cos(kz*z), ur=A*sin(kr*(r-r0))*sin(kz*z).
 * Every input is a true V mean, including kinetic energy. Angular momentum is
 * zero. These exact antiderivatives do not call the production moment owner.
 */
struct PeriodicMode {
    long double density=1.,radial_amplitude=0.,axial_amplitude=.05L;
    long double internal=12.,radial_lower=1.,radial_upper=3.,axial_length=2.;

    /** sin(x)/x with its exact removable zero limit; no small-value cutoff. */
    static long double sinc(long double x) {return x==0.?1.L:std::sin(x)/x;}

    FluidVector means(const Grid& grid,int i,int j) const
    {
        constexpr long double pi=std::numbers::pi_v<long double>;
        const long double l=grid.GetFacePosL(i),h=grid.GetFacePosR(i);
        const long double zl=grid.GetAxialFacePosL(j),zh=grid.GetAxialFacePosR(j);
        const long double kr=pi/(radial_upper-radial_lower),kz=2.L*pi/axial_length;
        const long double z=(zl+zh)/2.L,dz=zh-zl;
        const auto sine_integral=[&](long double r) {
            const long double x=kr*(r-radial_lower);
            return -r*std::cos(x)/kr+std::sin(x)/(kr*kr);
        };
        const auto cosine2_integral=[&](long double r) {
            const long double k=2.L*kr,x=k*(r-radial_lower);
            return r*std::sin(x)/k+std::cos(x)/(k*k);
        };
        const long double v=(h-l)*(h+l)/2.L;
        const long double sr=(sine_integral(h)-sine_integral(l))/v;
        const long double sr2=.5L-.5L*(cosine2_integral(h)-cosine2_integral(l))/v;
        const long double sz=sinc(kz*dz/2.L)*std::sin(kz*z);
        const long double cz=sinc(kz*dz/2.L)*std::cos(kz*z);
        const long double sz2=.5L-.5L*sinc(kz*dz)*std::cos(2.L*kz*z);
        const long double cz2=1.L-sz2;
        return {double(density),double(density*radial_amplitude*sr*sz),
            double(density*axial_amplitude*cz),0.,double(density*(internal
                +.5L*radial_amplitude*radial_amplitude*sr2*sz2
                +.5L*axial_amplitude*axial_amplitude*cz2))};
    }

    /** Exact eigenvalue for ur=0 on the uniform z stencil: 4/3 is 3D Stokes. */
    long double axial_eigenvalue(long double spacing,long double viscosity) const
    {
        const long double k=2.L*std::numbers::pi_v<long double>/axial_length;
        const long double sine=std::sin(k*spacing/2.L);
        return -(16.L/3.L)*viscosity*sine*sine/(spacing*spacing);
    }
};

/** Mathematical Legendre recurrence, independent of production RKL coefficients. */
inline long double legendre(int degree,long double x)
{
    long double older=1.L,current=x;
    for(int n=2;n<=degree;++n) {
        const long double next=((2.L*n-1.L)*x*current-(n-1.L)*older)/n;
        older=current;current=next;
    }
    return degree==0?older:current;
}

/** Frozen RKL endpoint polynomial; z is the actual eigenvalue times macro dt.
 * This reference grants neither intermediate thermal positivity nor CF modes.
 */
inline long double amplification(bool second,int stages,long double z)
{
    if(stages<1||(second&&stages<2))throw std::runtime_error("invalid RKL reference degree");
    const long double n=static_cast<long double>(stages)*(stages+1);
    if(!second)return legendre(stages,1.L+2.L*z/n);
    const long double b=(n-2.L)/(2.L*n);
    return 1.L-b+b*legendre(stages,1.L+4.L*z/(n-2.L));
}

/** Actual native PDE at the original annular centre and resolution sequence.
 * Independent true means precede every operator call. Space order >=log2(3.5)
 * and the original finest 1e-4 absolute/relative budget stay unchanged. This
 * spatial witness is not a CF, nonlinear RKL or runtime publication grant.
 */
template<class Evaluate>
void convergence(const char* backend,Evaluate evaluate)
{
    SpeciesManager species;species.add_species("gas",1.,1.,1.4,3.);
    IdealGas eos(1.4,species);SimConfig config;
    config.physics.diffusion.use_diffusion=true;config.physics.diffusion.use_viscous_diffusion=true;
    config.physics.diffusion.nu_visc=.03;
    constexpr auto native=GridMetrics::GeometrySemantics::AxisymmetricRz;
    for(int mode=0;mode<3;++mode) {
        Polynomial polynomial;if(mode)polynomial.a=.1L;if(mode==2)polynomial.b=.05L;
        double previous=0.;
        for(double h:{.05,.025,.0125}) {
            const double r=2.-(amr::BLOCK_NX/2+.5)*h,z=.7-(amr::BLOCK_NY/2+.5)*h;
            Grid grid(amr::MAX_NG,r,r+amr::BLOCK_NX*h,z,z+amr::BLOCK_NY*h,0.,1.);
            grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(native);
            FluidState state,delta;state.Preallocate(grid.GetTotalSize());state.InitSpecies(1);
            delta.Preallocate(grid.GetTotalSize());delta.InitSpecies(1);
            for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
                const int cell=grid.GetIndex(i,j);state.set(cell,polynomial.means(grid,i,j));state.X(0,cell)=1.;
            }
            evaluate(state,delta,eos,grid,config,native);
            const int i=grid.Is()+amr::BLOCK_NX/2,j=grid.Js()+amr::BLOCK_NY/2,cell=grid.GetIndex(i,j);
            const auto expected=polynomial.rhs(grid,i,j),actual=delta.get(cell);
            const double values[]{actual.mom_u,actual.mom_v,actual.mom_w,actual.eng};
            const double refs[]{expected.mom_u,expected.mom_v,expected.mom_w,expected.eng};
            double error=0.;
            for(int f=0;f<4;++f) {
                if(!std::isfinite(values[f]))throw std::runtime_error("native Stokes produced nonfinite RHS");
                error=std::max(error,std::abs(values[f]-refs[f])/std::max(1.,std::abs(refs[f])));
            }
            if(actual.rho!=0.||delta.X(0,cell)!=0.)throw std::runtime_error("native Stokes changed mass/species");
            if(previous>1.e-10&&previous<3.5*error)
                throw std::runtime_error("native full Stokes lost second-order consistency");
            std::cout<<"NATIVE_MERIDIONAL_STOKES_CONVERGENCE backend="<<backend<<" mode="<<mode
                <<" h="<<h<<" error="<<error<<std::endl;previous=error;
        }
        if(previous>1.e-4)throw std::runtime_error("native full Stokes analytic budget exceeded");
    }
}
} // namespace RzMeridionalCases
