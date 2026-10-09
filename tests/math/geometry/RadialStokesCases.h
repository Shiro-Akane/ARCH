// Independent radial Stokes edge energy and extreme-density witnesses.
// Both existing Host/CUDA geometry owners supply their real operator callback.
#pragma once

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "data/FluidState.h"
#include "grid/Grid.h"

namespace RadialStokesCases {

/** Compare physical kinetic energy and boundary work to independent edge forms.
 * Expected values use long-double polynomial measures and closed quadratic
 * strains; production stress, geometry source and timestep helpers are never
 * used to construct them. The original 2e-12 identity window is retained.
 * Zero ghost velocity is a prescribed external extension, with its actual
 * face power included. It is not silently interpreted as a no-work wall.
 */
template<class Evaluate>
void density_energy(const char* backend,double nu,Evaluate evaluate)
{
    for(const char* name:{"cylindrical","spherical"})
    for(double density_scale:{1.,1.e-20})
    for(int component=0;component<3;++component)
    for(int mode=0;mode<3;++mode) {
        Grid grid(amr::MAX_NG,0.,1.,0.,1.,0.,1.);
        grid.dim=1;grid.geometry=name;grid.InitializeTopology();
        const bool cylinder=std::string_view(name)=="cylindrical";
        const int measure=cylinder?1:2;
        const long double h=grid.dx1,viscosity=nu;
        FluidState state;state.Preallocate(grid.GetTotalSize());state.InitSpecies(1);
        std::vector<long double> u(grid.GetTotalX(),0.L),rho(grid.GetTotalX(),0.L);
        for(int i=0;i<grid.GetTotalX();++i) {
            const int n=i-grid.Is();
            // The three-cell spike is the independently found anti-dissipative
            // counterexample to a centre-stress geometric source. Remaining
            // actual cells keep the outer density rather than truncating Grid.
            const long double density=mode==0?(n==1?100.L:.01L):(n%2?100.L:1.L);
            rho[i]=density*density_scale;
            if(i>=grid.Is()&&i<grid.Ie())
                u[i]=mode==0?(n==0?.1L:n==1?.5L:n==2?.8L:0.L)
                    :mode==1?(n%2?-1.L:1.L):static_cast<long double>(grid.GetCellCenterX(i));
            const double density_value=static_cast<double>(rho[i]),v=static_cast<double>(u[i]);
            FluidVector value{density_value,0.,0.,0.,density_value*(30.+.5*v*v)};
            if(component==0)value.mom_u=density_value*v;
            else if(component==1)value.mom_v=density_value*v;
            else value.mom_w=density_value*v;
            state.set(i,value);state.X(0,i)=1.;
        }
        const auto actual=evaluate(state,grid);
        if(!std::isfinite(actual.raw_dt)||!(actual.raw_dt>0.))
            throw std::runtime_error("radial Stokes timestep is not positive finite");
        long double D=0.,right_power=0.;
        for(int e=grid.Is()+1;e<=grid.Ie();++e) {
            const long double radius=(e-grid.Is())*h;
            const long double low=(e-grid.Is()-.5L)*h,high=(e-grid.Is()+.5L)*h;
            const long double mu=viscosity*(rho[e-1]+rho[e])/2.L;
            long double tau=0.,face_velocity=0.;
            if(cylinder&&component==1) {
                const long double difference=u[e]-u[e-1];
                tau=mu*difference/h;face_velocity=(u[e]+u[e-1])/2.L;
                D+=mu*radius*difference*difference/h;
            } else {
                const long double qlow=u[e-1]/low,qhigh=u[e]/high;
                const long double mean=(qlow+qhigh)/2.L,difference=qhigh-qlow;
                const long double shear=radius*difference/h;
                face_velocity=radius*mean;
                if(cylinder&&component==0) {
                    tau=mu*(2.L*mean+4.L*shear)/3.L;
                    D+=4.L*mu*radius*h*(mean*mean+mean*shear+shear*shear)/3.L;
                } else {
                    const long double factor=component==0?4.L/3.L:1.L;
                    tau=factor*mu*shear;
                    D+=factor*mu*std::pow(radius,measure+2)*difference*difference/h;
                }
            }
            if(e==grid.Ie())right_power=std::pow(radius,measure)*tau*face_velocity;
        }
        long double Kdot=0.,before=0.,after=0.,Eflux=0.,force_square=0.;
        for(int i=grid.Is();i<grid.Ie();++i) {
            const long double a=(i-grid.Is())*h,b=a+h;
            const long double V=(std::pow(b,measure+1)-std::pow(a,measure+1))/(measure+1);
            const auto& value=actual.derivative[i];
            const long double force=component==0?value.mom_u:component==1?value.mom_v:value.mom_w;
            Kdot+=V*u[i]*force;before+=rho[i]*V*u[i]*u[i]/2.L;
            force_square+=V*force*force/rho[i];Eflux+=V*value.eng;
            const long double trial=u[i]+actual.raw_dt*force/rho[i];
            after+=rho[i]*V*trial*trial/2.L;
            if(value.rho!=0.)throw std::runtime_error("radial Stokes changed mass");
        }
        const long double identity_scale=D+std::abs(Kdot);
        const long double energy_scale=std::abs(Eflux)+std::abs(right_power)+D;
        const long double exact_change=actual.raw_dt*Kdot
            +static_cast<long double>(actual.raw_dt)*actual.raw_dt*force_square/2.L;
        if(!(D>=0.)||std::abs(Kdot+D)>2.e-12L*identity_scale
           ||std::abs(Eflux-right_power)>2.e-12L*energy_scale
           ||after-before>2.e-12L*before
           ||std::abs(after-before-exact_change)>2.e-12L*(before+std::abs(exact_change)))
            throw std::runtime_error("radial Stokes edge dissipation/work/FE identity");
        std::cout<<"RADIAL_STOKES_DENSITY_ENERGY backend="<<backend<<" geometry="<<name
            <<" scale="<<density_scale<<" component="<<component<<" mode="<<mode
            <<" Kdot="<<static_cast<double>(Kdot)<<" D="<<static_cast<double>(D)
            <<" FE_change="<<static_cast<double>(after-before)<<" raw_dt="<<actual.raw_dt<<std::endl;
    }
}
} // namespace RadialStokesCases
