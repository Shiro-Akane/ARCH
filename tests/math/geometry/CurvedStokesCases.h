// Independent multidimensional Stokes quadratic, boundary-work and FE checks.
#pragma once

#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "data/FluidState.h"
#include "grid/Grid.h"

namespace CurvedStokesCases {
using Vector=std::array<long double,3>;
using Tensor=std::array<long double,9>;
struct Cell {
    Vector velocity{},spacing{};
    Tensor partial{},stress{};
    long double volume=0.,mu=0.,dissipation=0.;
};

/** Independent physical volume, including the fixed virtual extension. */
inline long double volume(const Grid& grid,int i,int j)
{
    const long double a=grid.GetFacePosL(i),b=grid.GetFacePosR(i);
    if(grid.geometry=="spherical"&&grid.dim==3) {
        const long double theta=grid.GetCellCenterY(j),half=grid.dx2/2.L;
        return (b*b*b-a*a*a)/3.L*(std::cos(theta-half)-std::cos(theta+half))*grid.dx3;
    }
    const long double azimuth=grid.dim==3?grid.dx3:1.L;
    return (b*b-a*a)/2.L*grid.dx2*azimuth;
}

/** Build the independent point gradient and its Stokes/variance quadratic.
 * Missing normal columns are real zero. All coefficients below use explicit
 * physical connections and long-double algebra, without any production
 * stress, gradient, source, row or surface-flux helper.
 */
inline Cell reference(const FluidState& state,const Grid& grid,int i,int j,int k,double nu)
{
    Cell result{};const int index=grid.GetIndex(i,j,k);
    result.volume=volume(grid,i,j);result.mu=static_cast<long double>(state.rho[index])*nu;
    const long double r=grid.GetCellCenterX(i),theta=grid.GetCellCenterY(j);
    result.velocity={static_cast<long double>(state.mom_u[index])/state.rho[index],
        static_cast<long double>(state.mom_v[index])/state.rho[index],
        static_cast<long double>(state.mom_w[index])/state.rho[index]};
    result.spacing={grid.dx1,grid.geometry=="cylindrical"?grid.dx2:r*grid.dx2,
        r*(grid.geometry=="spherical"?std::sin(theta):1.L)*grid.dx3};
    const int strides[3]{1,grid.stride_y,grid.stride_z};
    for(int axis=0;axis<grid.dim;++axis) {
        const int low=index-strides[axis],high=index+strides[axis];
        const auto left=state.get(low),right=state.get(high);
        const double lm[3]{left.mom_u,left.mom_v,left.mom_w};
        const double rm[3]{right.mom_u,right.mom_v,right.mom_w};
        for(int a=0;a<3;++a) {
            const long double lower=static_cast<long double>(lm[a])/left.rho;
            const long double upper=static_cast<long double>(rm[a])/right.rho;
            const long double plus=(upper-result.velocity[a])/result.spacing[axis];
            const long double minus=(result.velocity[a]-lower)/result.spacing[axis];
            result.partial[3*a+axis]=(plus+minus)/2.L;
            const long double variance=(plus-minus)/2.L;
            result.dissipation+=result.mu*(a==axis?4.L/3.L:1.L)*variance*variance;
        }
    }
    Tensor g=result.partial;
    const auto& u=result.velocity;
    if(grid.geometry=="cylindrical") {g[2]-=u[2]/r;g[8]+=u[0]/r;}
    else if(grid.dim==2) {g[1]-=u[1]/r;g[4]+=u[0]/r;}
    else {
        const long double cot=std::cos(theta)/std::sin(theta);
        g[1]-=u[1]/r;g[4]+=u[0]/r;g[2]-=u[2]/r;
        g[5]-=cot*u[2]/r;g[8]+=u[0]/r+cot*u[1]/r;
    }
    const long double trace=g[0]+g[4]+g[8];
    for(int a=0;a<3;++a)for(int b=0;b<3;++b) {
        result.stress[3*a+b]=result.mu*(g[3*a+b]+g[3*b+a]-(a==b?2.L*trace/3.L:0.L));
        result.dissipation+=result.stress[3*a+b]*g[3*a+b];
    }
    result.dissipation*=result.volume;return result;
}

/** The actual ordinary multidimensional operator must dissipate physical K.
 * Include the fixed zero-velocity ghost extension in D; these physical outer
 * faces have measurable work, so total gas energy need not remain constant.
 * Same-endpoint FE energy, complete surface work, density-invariance and the
 * independent positive Stokes quadratic use the original 2e-12 window.
 */
template<class Evaluate>
void density_energy(const char* backend,double nu,Evaluate evaluate)
{
    for(const char* name:{"cylindrical","spherical"})for(int dim:{2,3}) {
        if(std::string_view(name)=="cylindrical"&&dim==2)continue;
        for(double density_scale:{1.,1.e-20}) {
            Grid grid(amr::MAX_NG,1.,2.,.5,1.5,.2,.7);
            grid.dim=dim;grid.geometry=name;
            grid.InitializeTopology();
            FluidState state;state.Preallocate(grid.GetTotalSize());state.InitSpecies(1);
            for(int k=0;k<grid.GetTotalZ();++k)for(int j=0;j<grid.GetTotalY();++j)
            for(int i=0;i<grid.GetTotalX();++i) {
                const int index=grid.GetIndex(i,j,k),n=i-grid.Is();
                const double rho=(n==1?100.:.01)*density_scale;
                const bool active=i>=grid.Is()&&i<grid.Ie()&&j>=grid.Js()&&j<grid.Je()&&k>=grid.Ks()&&k<grid.Ke();
                const double u=active?(n==0?.1:n==1?.5:n==2?.8:0.):0.;
                const double v=active?((j-grid.Js())%2?.2:-.3):0.;
                const double w=active?((k-grid.Ks()+n)%2?.4:-.1):0.;
                state.set(index,{rho,rho*u,rho*v,rho*w,rho*(30.+.5*(u*u+v*v+w*w))});
                state.X(0,index)=1.;
            }
            const auto actual=evaluate(state,grid);
            if(!std::isfinite(actual.raw_dt)||!(actual.raw_dt>0.))
                throw std::runtime_error("curved Stokes recommendation is not positive finite");
            std::vector<Cell> cells(grid.GetTotalSize());long double D=0.;
            for(int k=dim==3?grid.Ks()-1:0;k<(dim==3?grid.Ke()+1:1);++k)
            for(int j=grid.Js()-1;j<grid.Je()+1;++j)for(int i=grid.Is()-1;i<grid.Ie()+1;++i) {
                auto& cell=cells[grid.GetIndex(i,j,k)];cell=reference(state,grid,i,j,k,nu);D+=cell.dissipation;
            }
            long double Kdot=0.,before=0.,after=0.,Eflux=0.,force_square=0.,surface_work=0.;
            for(int k=grid.Ks();k<grid.Ke();++k)for(int j=grid.Js();j<grid.Je();++j)
            for(int i=grid.Is();i<grid.Ie();++i) {
                const int index=grid.GetIndex(i,j,k);const auto& c=cells[index];const auto& value=actual.derivative[index];
                const long double rho=state.rho[index];const long double f[3]{value.mom_u,value.mom_v,value.mom_w};
                for(int a=0;a<3;++a) {
                    Kdot+=c.volume*c.velocity[a]*f[a];before+=rho*c.volume*c.velocity[a]*c.velocity[a]/2.L;
                    force_square+=c.volume*f[a]*f[a]/rho;
                    const long double trial=c.velocity[a]+actual.raw_dt*f[a]/rho;
                    after+=rho*c.volume*trial*trial/2.L;
                }
                Eflux+=c.volume*value.eng;
                if(value.rho!=0.)throw std::runtime_error("curved Stokes changed density");
                const int coord[3]{i,j,k},low[3]{grid.Is(),grid.Js(),grid.Ks()},high[3]{grid.Ie(),grid.Je(),grid.Ke()};
                const int strides[3]{1,grid.stride_y,grid.stride_z};
                for(int axis=0;axis<dim;++axis)for(int side=0;side<2;++side)
                    if(coord[axis]==(side?high[axis]-1:low[axis])) {
                        const int left=side?index:index-strides[axis],right=left+strides[axis];
                        const auto& l=cells[left];const auto& r=cells[right];
                        for(int a=0;a<3;++a) {
                            const long double difference=r.velocity[a]-l.velocity[a],factor=a==axis?4.L/3.L:1.L;
                            const long double tl=l.stress[3*a+axis]+l.mu*factor*(difference/l.spacing[axis]-l.partial[3*a+axis]);
                            const long double tr=r.stress[3*a+axis]+r.mu*factor*(difference/r.spacing[axis]-r.partial[3*a+axis]);
                            const long double traction=l.volume*tl/(2.L*l.spacing[axis])+r.volume*tr/(2.L*r.spacing[axis]);
                            surface_work+=(side?1.L:-1.L)*(l.velocity[a]+r.velocity[a])*traction/2.L;
                        }
                    }
            }
            const long double scale=D+std::abs(Kdot),energy_scale=D+std::abs(surface_work)+std::abs(Eflux);
            const long double change=actual.raw_dt*Kdot+static_cast<long double>(actual.raw_dt)*actual.raw_dt*force_square/2.L;
            if(!(D>=0.)||std::abs(Kdot+D)>2.e-12L*scale
               ||std::abs(Eflux-surface_work)>2.e-12L*energy_scale
               ||after-before>2.e-12L*before
               ||std::abs(after-before-change)>2.e-12L*(before+std::abs(change)))
                throw std::runtime_error("curved Stokes quadratic/work/FE identity failed");
            std::cout<<"CURVED_STOKES_DENSITY_ENERGY backend="<<backend<<" geometry="<<name<<" dim="<<dim
                <<" scale="<<density_scale<<" Kdot="<<static_cast<double>(Kdot)<<" D="<<static_cast<double>(D)
                <<" boundary_work="<<static_cast<double>(surface_work)<<" raw_dt="<<actual.raw_dt<<std::endl;
        }
    }
}

/** Actual zero-area cylindrical axis with reflected odd radial velocity.
 * This is an ordinary cylindrical 3D slab.
 * z/phi copies are constant through their ghosts. Only the first three radial
 * velocities are nonzero; rho=(.01,100,.01,...) and inner rho is reflected.
 * An independent rational assembly at h=nu=1 gives the principal integrated
 * force matrix A={{-10003/100,10001/150,0},
 * {10001/150,-130009/450,10001/75},{0,10001/75,-50031/500}}.
 * Its leading minors alternate sign, and the two exact Kdot values below are
 * negative. Scaling nu, density and the actual z/phi widths gives the physical
 * slab result; no production gradient/source/row constructs this oracle.
 */
template<class Evaluate>
void cylindrical_axis_energy(const char* backend,double nu,Evaluate evaluate)
{
    for(double density_scale:{1.,1.e-20})for(int mode=0;mode<2;++mode) {
        Grid grid(amr::MAX_NG,0.,1.,0.,1.,0.,1.);
        grid.dim=3;grid.geometry="cylindrical";
        grid.InitializeTopology();
        FluidState state;state.Preallocate(grid.GetTotalSize());state.InitSpecies(1);
        for(int k=0;k<grid.GetTotalZ();++k)for(int j=0;j<grid.GetTotalY();++j)
        for(int i=0;i<grid.GetTotalX();++i) {
            const int n=i-grid.Is(),physical=n<0?-n-1:n;
            const double rho=(physical==1?100.:.01)*density_scale;
            const double seed=physical<3?(mode==0?(physical==0?.1:physical==1?.5:.8)
                :(physical==1?-1.:1.)):0.;
            const double u=n<0?-seed:seed;const int index=grid.GetIndex(i,j,k);
            state.set(index,{rho,rho*u,0.,0.,rho*(30.+.5*u*u)});state.X(0,index)=1.;
        }
        const auto actual=evaluate(state,grid);
        if(!std::isfinite(actual.raw_dt)||!(actual.raw_dt>0.))
            throw std::runtime_error("axis Stokes recommendation is not positive finite");
        long double Kdot=0.,before=0.,after=0.,Eflux=0.;
        for(int k=grid.Ks();k<grid.Ke();++k)for(int j=grid.Js();j<grid.Je();++j)
        for(int i=grid.Is();i<grid.Ie();++i) {
            const int index=grid.GetIndex(i,j,k);const auto& d=actual.derivative[index];
            const long double V=volume(grid,i,j),rho=state.rho[index],u=state.mom_u[index]/rho;
            Kdot+=V*u*d.mom_u;before+=rho*V*u*u/2.L;Eflux+=V*d.eng;
            // Match the independent energy sums' long-double arithmetic from
            // the first multiply, rather than widening an already rounded product.
            const long double trial=u+static_cast<long double>(actual.raw_dt)*d.mom_u/rho;
            after+=rho*V*trial*trial/2.L;
            if(d.rho!=0.||d.mom_v!=0.||d.mom_w!=0.)
                throw std::runtime_error("axisymmetric radial diffusion changed mass/transverse momentum");
        }
        const long double expected=(mode==0?-10765141.L/450000.L:-1000171.L/1125.L)
            *nu*density_scale*(grid.x2_max-grid.x2_min)
            *(grid.x3_max-grid.x3_min);
        if(!(Kdot<0.)||std::abs(Kdot-expected)>2.e-12L*std::abs(expected)
           ||std::abs(Eflux)>2.e-12L*std::abs(expected)
           ||after-before>2.e-12L*before)
            throw std::runtime_error("actual cylindrical axis kinetic/work/FE identity failed");
        std::cout<<"CYLINDRICAL_AXIS_STOKES_ENERGY backend="<<backend<<" scale="<<density_scale
            <<" mode="<<mode<<" Kdot="<<static_cast<double>(Kdot)<<" expected="<<static_cast<double>(expected)
            <<" raw_dt="<<actual.raw_dt<<std::endl;
    }
}
/** Independent PWC angular capacities, torques and physical kinetic work.
 * The reference integrates polynomials directly in long double; it does not
 * call the production moment/stress/divergence helpers. Outer zero-velocity
 * ghosts exert a measurable torque/work, so their flux belongs in the balance.
 */
template<class Evaluate>
void axisymmetric_angular(const char* backend,double nu,Evaluate evaluate)
{
    for(double inner:{0.,1.})for(int mode:{0,1}) {
        Grid grid(amr::MAX_NG,inner,inner+1.,0.,1.,0.,0.,1,1,0);
        grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology();
        FluidState state;state.Preallocate(grid.GetTotalSize());state.InitSpecies(1);
        auto measures=[&](int i) {
            const long double a=grid.GetFacePosL(i),b=grid.GetFacePosR(i);
            return std::array<long double,2>{(b*b-a*a)*grid.dx2/2.L,
                (b*b*b-a*a*a)*grid.dx2/3.L};
        };
        for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
            const auto m=measures(i);const bool active=i>=grid.Is()&&i<grid.Ie()&&j>=grid.Js()&&j<grid.Je();
            // Mode zero is exactly the declared PWC rigid-rotation projection.
            const double omega=mode==0?.2:active?.2*std::sin((i-grid.Is()+.5)*std::numbers::pi/amr::BLOCK_NX)
                *std::sin((j-grid.Js()+.5)*std::numbers::pi/amr::BLOCK_NY):0.;
            const double uphi=omega*static_cast<double>(m[1]/m[0]);
            const int c=grid.GetIndex(i,j,0);state.set(c,{1.,0.,0.,uphi,10.+.5*uphi*uphi});state.X(0,c)=1.;
        }
        const auto actual=evaluate(state,grid);
        long double jdot=0.,jnorm=0.,kdot=0.,kinetic=0.,after=0.,energy=0.,dissipation=0.,torque_out=0.,power_in=0.;
        double largest=0.;
        for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
            const int c=grid.GetIndex(i,j,0);const auto m=measures(i);const auto u=state.get(c);const auto d=actual.derivative[c];
            jdot+=d.mom_w*m[1];jnorm+=std::abs(u.mom_w)*m[1];
            // Use the same long-double polynomial work as the kinetic sum below.
            kdot+=static_cast<long double>(u.mom_w)*d.mom_w*m[0];
            kinetic+=.5L*u.mom_w*u.mom_w*m[0];energy+=d.eng*m[0];
            const long double trial=u.mom_w+.8L*actual.raw_dt*d.mom_w;after+=.5L*trial*trial*m[0];
            largest=std::max(largest,std::abs(d.mom_w));
            if(d.rho!=0.||d.mom_u!=0.||d.mom_v!=0.)
                throw std::runtime_error("angular diffusion contaminated meridional fields");
        }
        for(int dir=0;dir<2;++dir)for(int j=grid.Js();j<grid.Je()+(dir==1);++j)
            for(int i=grid.Is();i<grid.Ie()+(dir==0);++i) {
                const int il=i-(dir==0),jl=j-(dir==1);const auto ml=measures(il),mr=measures(i);
                const long double radius=grid.GetFacePosL(i),a=grid.GetFacePosL(i),b=grid.GetFacePosR(i);
                const long double area=dir==0?radius*grid.dx2:(b*b-a*a)/2.L;
                if(area==0.)continue;
                const long double t=dir==0?radius*radius*grid.dx2:(b*b*b-a*a*a)/3.L;
                const long double omega_l=state.get(grid.GetIndex(il,jl,0)).mom_w/(ml[1]/ml[0]);
                const long double omega_r=state.get(grid.GetIndex(i,j,0)).mom_w/(mr[1]/mr[0]);
                const long double delta=omega_r-omega_l,C=nu*(t/area)*t/(dir==0?grid.dx1:grid.dx2),torque=C*delta;
                dissipation+=C*delta*delta;
                const bool low=dir==0?i==grid.Is():j==grid.Js(),high=dir==0?i==grid.Ie():j==grid.Je();
                // Physical diffusive flux is -tau: low traction removes and
                // high traction supplies angular momentum to the active domain.
                if(low||high) {const int sign=low?-1:1;torque_out-=sign*torque;power_in+=sign*.5L*(omega_l+omega_r)*torque;}
            }
        std::cout<<"AXISYMMETRIC_ANGULAR_RAW inner="<<inner<<" mode="<<mode
            <<" Jdot="<<static_cast<double>(jdot)<<" torque_out="<<static_cast<double>(torque_out)
            <<" Kdot="<<static_cast<double>(kdot)<<" D="<<static_cast<double>(dissipation)
            <<" E="<<static_cast<double>(energy)<<" power="<<static_cast<double>(power_in)
            <<" K="<<static_cast<double>(kinetic)<<" K_after="<<static_cast<double>(after)<<std::endl;
        if(!std::isfinite(actual.raw_dt)||!(actual.raw_dt>0.)||
           std::abs(jdot+torque_out)>1.e-12L*(jnorm+std::abs(torque_out))||
           std::abs(kdot+dissipation)>2.e-12L*(mode==0?kinetic:dissipation)||
           std::abs(energy-power_in)>2.e-12L*(mode==0?kinetic:dissipation)||
           after-kinetic>2.e-12L*kinetic|| (mode==0&&largest>2.e-12))
            throw std::runtime_error("axisymmetric angular torque/work/FE identity");
        std::cout<<"AXISYMMETRIC_ANGULAR backend="<<backend<<" inner="<<inner<<" mode="<<mode
            <<" J_error="<<static_cast<double>(std::abs(jdot+torque_out)/(jnorm+std::abs(torque_out)))
            <<" Kdot="<<static_cast<double>(kdot)<<" dissipation="<<static_cast<double>(dissipation)
            <<" E_dot="<<static_cast<double>(energy)<<" boundary_power="<<static_cast<double>(power_in)
            <<" raw_dt="<<actual.raw_dt<<std::endl;
    }
}
/** Refine the same radial domain with ordinary block-local grids and exact
 * analytic ghost/projection data. For u_phi=a*r+b*r^3, constant rho=1 and
 * viscosity nu, the physical Stokes derivative is 8*nu*b*r. Its cell average
 * is 8*nu*b*ell. This tests PDE consistency separately from torque identity.
 */
template<class Evaluate>
void axisymmetric_convergence(const char* backend,double nu,Evaluate evaluate)
{
    for(double inner:{0.,1.}) {
        double previous=0.;
        for(int patches:{1,2,4}) {
            long double error=0.,norm=0.;
            for(int patch=0;patch<patches;++patch) {
                Grid grid(amr::MAX_NG,inner+double(patch)/patches,inner+double(patch+1)/patches,
                    0.,1.,0.,0.,1,1,0);grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology();
                FluidState state;state.Preallocate(grid.GetTotalSize());state.InitSpecies(1);
                for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
                    const long double a=grid.GetFacePosL(i),b=grid.GetFacePosR(i),Q=(b*b-a*a)/2.L;
                    const double uphi=static_cast<double>((.1L*(b*b*b-a*a*a)/3.L+
                        .2L*(std::pow(b,5)-std::pow(a,5))/5.L)/Q);
                    const int c=grid.GetIndex(i,j,0);state.set(c,{1.,0.,0.,uphi,10.+.5*uphi*uphi});state.X(0,c)=1.;
                }
                const auto actual=evaluate(state,grid);
                for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
                    const long double a=grid.GetFacePosL(i),b=grid.GetFacePosR(i),Q=(b*b-a*a)/2.L;
                    const long double lever=(b*b*b-a*a*a)/(3.L*Q),exact=8.L*nu*.2L*lever,V=Q*grid.dx2;
                    error+=std::abs(actual.derivative[grid.GetIndex(i,j,0)].mom_w-exact)*V;norm+=std::abs(exact)*V;
                }
            }
            const double relative=static_cast<double>(error/norm);
            if(!std::isfinite(relative)||!(relative>0.)||(previous>0.&&relative>previous*.3))
                throw std::runtime_error("axisymmetric manufactured radial shear convergence");
            std::cout<<"AXISYMMETRIC_CONVERGENCE backend="<<backend<<" inner="<<inner
                <<" radial_cells="<<patches*amr::BLOCK_NX<<" relative_L1="<<relative<<std::endl;
            previous=relative;
        }
    }
}
} // namespace CurvedStokesCases
