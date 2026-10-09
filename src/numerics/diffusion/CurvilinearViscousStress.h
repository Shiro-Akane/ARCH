/**
 * @file CurvilinearViscousStress.h
 * @brief Metric stencils for the existing shared three-dimensional Stokes law.
 *
 * Workflow:
 * 1. Borrow ordinary-chart point velocities and actual orthonormal spacings.
 *    Real centred columns and their two one-sided normal differences define
 *    a positive quadratic strain form. No evolved field is introduced.
 * 2. Add the frame connection with NewtonianViscousStress, form the same
 *    tau=mu*(G+G^T-(2/3)*tr(G)*I), and return its physical normal traction.
 *    The cylindrical (r,z,phi), polar (r,phi,z), and spherical (r,theta,phi)
 *    frames retain their distinct ordering, including unresolved directions.
 * 3. Multidimensional traction and cell connections are the adjoint of that
 *    same strain form in actual cell volumes. Radial reductions use their
 *    common edge trace/lever arm. Energy is the identical traction's work
 *    divergence, with no independent irreversible-heating source.
 * 4. Bound the absolute rows of these actual stencils for the timestep owner.
 *    This algebraic bound alone is not a dissipativity or RKL certificate.
 *
 * Native RZ V/W means keep their separate density/torque owner; they cannot
 * be interpreted as these ordinary point samples. Both backends borrow this
 * allocation-free leaf. Coefficients, EOS, BC, AMR/reflux and acceptance remain
 * with their existing owners. Invalid inputs never publish a partial result.
 */
#pragma once

#include <array>
#include <cmath>
#include <numbers>

#include "core/ArchPortability.h"
#include "grid/GridMetrics.h"
#include "numerics/diffusion/NewtonianViscousStress.h"

namespace CurvilinearViscousStress {
using Vector=NewtonianViscousStress::Vector;
using Gradient=NewtonianViscousStress::VelocityGradient;
using Tensor=NewtonianViscousStress::Tensor;
using Frame=NewtonianViscousStress::Frame;

/** Shared radial edge, including the work-conjugate physical face velocity. */
struct RadialFace {
    Tensor stress{};
    Vector work_velocity{};
    bool valid=false;
};

/** Select the actual component frame; polar 2D has no spherical theta basis. */
ARCH_INLINE Frame frame(const GridMetrics::GeometryView& grid)
{
    if(grid.geometry==GridMetrics::Geometry::Cylindrical)return Frame::CylindricalRZPhi;
    return grid.dim==2?Frame::CylindricalRPhiZ:Frame::SphericalRThetaPhi;
}

/** Validate one ordinary sample before any division or zero-mu shortcut. */
template<class StateReader>
ARCH_INLINE bool velocity(const StateReader& read,int cell,
    const GridMetrics::GeometryView& grid,Vector& output)
{
    if(cell<0||cell>=grid.total_size)return false;
    const auto state=read(cell);
    if(!std::isfinite(state.rho)||!(state.rho>0.))return false;
    const Vector candidate{state.mom_u/state.rho,state.mom_v/state.rho,state.mom_w/state.rho};
    for(double value:candidate)if(!std::isfinite(value))return false;
    output=candidate;return true;
}

/** True physical spacings at the requested point, with no singularity floor. */
ARCH_INLINE bool spacings(const GridMetrics::GeometryView& grid,
    double radius,double theta,std::array<double,3>& output)
{
    if(grid.semantics!=GridMetrics::GeometrySemantics::Existing||grid.dim<1||grid.dim>3||
       grid.geometry==GridMetrics::Geometry::Cartesian||
       grid.geometry==GridMetrics::Geometry::Unsupported||
       (grid.geometry==GridMetrics::Geometry::Cylindrical&&grid.dim==2)||
       !std::isfinite(radius)||!(radius>0.))return false;
    std::array<double,3> candidate{};
    for(int axis=0;axis<grid.dim;++axis) {
        candidate[axis]=GridMetrics::PhysicalSpacing(grid.geometry,grid.dim,axis,
            grid.dx1,grid.dx2,grid.dx3,radius,theta);
        // Reflected spherical virtual cells can have a signed phi metric.
        // Real physical cells retain positive lengths; a pole remains invalid.
        if(!std::isfinite(candidate[axis])||candidate[axis]==0.||
           (candidate[axis]<0.&&!(grid.geometry==GridMetrics::Geometry::Spherical&&
               grid.dim==3&&axis==2)))return false;
    }
    output=candidate;return true;
}

/** Fold only spherical virtual coordinates before the existing point helper.
 * For theta outside (0,pi), the identical Cartesian point uses a reflected
 * theta and phi+pi, with S=diag(1,-1,-1). Transform u'=S*u and P'=S*P*S,
 * call the single physical connection owner, then unfold G=S*G'*S.
 * This is a ghost chart transform, not a new stress law or a pole floor.
 */
ARCH_INLINE bool covariant_gradient(const GridMetrics::GeometryView& grid,
    Vector u,Gradient partial,double radius,double theta,Gradient& output)
{
    const auto basis=frame(grid);
    bool reflected=false;
    if(basis==Frame::SphericalRThetaPhi&&grid.dim==3) {
        constexpr double pi=std::numbers::pi_v<double>;
        if(!std::isfinite(theta))return false;
        theta=std::fmod(theta,2.*pi);if(theta<0.)theta+=2.*pi;
        if(theta>pi) {theta=2.*pi-theta;reflected=true;}
        if(reflected) {
            u[1]=-u[1];u[2]=-u[2];
            for(int a=0;a<3;++a)for(int b=0;b<3;++b)
                if((a==0)!=(b==0))partial[3*a+b]=-partial[3*a+b];
        }
    }
    Gradient candidate{};
    if(!NewtonianViscousStress::physical_covariant_gradient(basis,u,partial,radius,theta,candidate))return false;
    if(reflected)for(int a=0;a<3;++a)for(int b=0;b<3;++b)
        if((a==0)!=(b==0))candidate[3*a+b]=-candidate[3*a+b];
    output=candidate;return true;
}

/** Gather real centred partials, optionally excluding the paired face normal.
 * G_ik=(u_i,+-u_i,-)/(2*h_k). Missing physical coordinate partials stay zero;
 * basis derivatives are added separately, including in radial reductions.
 */
template<class StateReader>
ARCH_INLINE bool partials(const StateReader& read,int cell,
    const GridMetrics::GeometryView& grid,const std::array<double,3>& h,
    int excluded,Gradient& output)
{
    if(grid.ng<1||grid.stride_y<=0||grid.stride_z<=0||grid.total_size<=0||
       cell<0||cell>=grid.total_size)return false;
    const int extents[3]{grid.stride_y,grid.stride_z/grid.stride_y,
        grid.total_size/grid.stride_z};
    const int coordinate[3]{cell%grid.stride_y,
        (cell%grid.stride_z)/grid.stride_y,cell/grid.stride_z};
    const int strides[3]{1,grid.stride_y,grid.stride_z};
    Gradient candidate{};
    for(int axis=0;axis<grid.dim;++axis)if(axis!=excluded) {
        if(coordinate[axis]<=0||coordinate[axis]>=extents[axis]-1)return false;
        Vector low{},high{};
        if(!velocity(read,cell-strides[axis],grid,low)||
           !velocity(read,cell+strides[axis],grid,high))return false;
        for(int component=0;component<3;++component) {
            const double value=.5*((high[component]-low[component])/h[axis]);
            if(!std::isfinite(value))return false;
            candidate[3*component+axis]=value;
        }
    }
    output=candidate;return true;
}

/** Actual face coordinates; normal angular faces use their own angle. */
ARCH_INLINE void face_point(const GridMetrics::GeometryView& grid,
    int direction,int i,int j,double& radius,double& theta)
{
    radius=direction==0?grid.GetFacePosL(i):grid.GetCellCenterX(i);
    theta=grid.SourceTheta(j);
    if(direction==1&&grid.geometry==GridMetrics::Geometry::Spherical&&grid.dim==3)
        theta=grid.x2_min+(j-grid.ng)*grid.dx2;
}

/** Pair the actual cell-volume products before selecting face traction.
 * D_i=V_i*(tau(G_i):G_i+mu_i*sum a_ij*(dplus_ij-dminus_ij)^2/4),
 * a_jj=4/3 and a_ij=1 otherwise. The latter is the variance of the SAME
 * Stokes law over independent left/right derivative columns, not a new
 * physical viscosity. Effective column tau_e=tau_i+mu_i*a*(d_e-dbar_i).
 * Its adjoint face traction is sum_i V_i*tau_e/(2*h_i*A_e). This retains
 * the full three-dimensional trace, symmetric variable-mu kinetic form,
 * and the ordinary scalar surface energy work using (u_L+u_R)/2.
 * A regular r=0 radial surface has zero area: its integrated traction/work
 * vanish, and no artificial 1/r is evaluated there.
 */
template<class StateReader>
ARCH_INLINE bool face_traction(const StateReader& read,int right_cell,
    const GridMetrics::GeometryView& grid,int direction,int i,int j,
    double mu_left,double mu_right,Vector& traction,Vector& work_velocity)
{
    if(direction<0||direction>=grid.dim||!std::isfinite(mu_left)||mu_left<0.||
       !std::isfinite(mu_right)||mu_right<0.)return false;
    const int stride=direction==0?1:direction==1?grid.stride_y:grid.stride_z;
    Vector left{},right{};
    if(!velocity(read,right_cell-stride,grid,left)||!velocity(read,right_cell,grid,right))return false;
    const Vector mean{.5*(left[0]+right[0]),.5*(left[1]+right[1]),.5*(left[2]+right[2])};
    for(double value:mean)if(!std::isfinite(value))return false;
    double radius=0.,theta=0.;face_point(grid,direction,i,j,radius,theta);
    if(direction==0&&radius==0.) {traction={};work_velocity=mean;return true;}
    const int k=right_cell/grid.stride_z;
    const int il=i-(direction==0?1:0),jl=j-(direction==1?1:0),kl=k-(direction==2?1:0);
    const double area=GridMetrics::FaceArea(grid,direction,i,j,k,false);
    if(area==0.||(grid.geometry==GridMetrics::Geometry::Spherical&&grid.dim==3&&direction==1&&
        (theta==0.||theta==std::numbers::pi_v<double>))) {traction={};work_velocity=mean;return true;}
    if(!std::isfinite(area)||!(area>0.))return false;
    std::array<double,3> hl{},hr{};
    const double rl=grid.GetCellCenterX(il),rr=grid.GetCellCenterX(i);
    const double tl_point=grid.SourceTheta(jl),tr_point=grid.SourceTheta(j);
    if(!spacings(grid,rl,tl_point,hl)||!spacings(grid,rr,tr_point,hr))return false;
    Gradient pl{},pr{},gl{},gr{};
    if((mu_left!=0.||mu_right!=0.)&&
       (!partials(read,right_cell-stride,grid,hl,direction,pl)||
        !partials(read,right_cell,grid,hr,direction,pr)))return false;
    // The linear Stokes column cancels its own centred normal derivative.
    // Evaluate that column directly with the paired difference; this is the
    // same adjoint expression, without a subtractive cancellation or an extra
    // normal ghost obligation. All transverse columns retain their real halo.
    for(int a=0;a<3;++a) {
        pl[3*a+direction]=(right[a]-left[a])/hl[direction];
        pr[3*a+direction]=(right[a]-left[a])/hr[direction];
    }
    if(!covariant_gradient(grid,left,pl,rl,tl_point,gl)||
       !covariant_gradient(grid,right,pr,rr,tr_point,gr))return false;
    Tensor tl{},tr{};
    if(!NewtonianViscousStress::stress(gl,mu_left,tl)||
       !NewtonianViscousStress::stress(gr,mu_right,tr))return false;
    Vector candidate{};
    const double vl=std::abs(GridMetrics::CellVolume(grid,il,jl,kl));
    const double vr=std::abs(GridMetrics::CellVolume(grid,i,j,k));
    if(!std::isfinite(vl)||!(vl>0.)||!std::isfinite(vr)||!(vr>0.))return false;
    for(int component=0;component<3;++component) {
        candidate[component]=(vl/(2.*hl[direction])*tl[3*component+direction]
            +vr/(2.*hr[direction])*tr[3*component+direction])/area;
        if(!std::isfinite(candidate[component]))return false;
    }
    traction=candidate;work_velocity=mean;return true;
}

/** One-dimensional radial strains in a common physical edge trace.
 * Workflow: borrow q=u/r for the components with a curved basis; interpolate
 * q and differentiate it on the true two-cell edge. Then u_f=r_f*q_f and
 * d_r u=q_f+r_f*d_r q are mutually consistent. Cylindrical axial velocity
 * remains an ordinary scalar difference. Evaluate the existing covariant
 * gradient and Stokes law once; no stress formula is duplicated here.
 * Positive products mu_L,mu_R retain their existing shared mean guard.
 */
template<class StateReader>
ARCH_INLINE RadialFace radial_face(const StateReader& read,int right_cell,
    const GridMetrics::GeometryView& grid,int i,double nu)
{
    RadialFace result{};
    if(grid.dim!=1||grid.semantics!=GridMetrics::GeometrySemantics::Existing||
       !std::isfinite(nu)||nu<0.)return result;
    Vector left{},right{};
    if(!velocity(read,right_cell-1,grid,left)||!velocity(read,right_cell,grid,right))return result;
    const double radius=grid.GetFacePosL(i),low=grid.GetCellCenterX(i-1),high=grid.GetCellCenterX(i);
    if(!std::isfinite(radius)||radius<0.||!std::isfinite(low)||low==0.||
       !std::isfinite(high)||!(high>0.)||!std::isfinite(grid.dx1)||!(grid.dx1>0.))return result;
    if(radius==0.) {
        result.work_velocity={0.,grid.geometry==GridMetrics::Geometry::Cylindrical?
            .5*(left[1]+right[1]):0.,0.};result.valid=true;return result;
    }
    double mu_left=0.,mu_right=0.,mu=0.;
    if(!NewtonianViscousStress::dynamic_viscosity(read(right_cell-1).rho,nu,mu_left)||
       !NewtonianViscousStress::dynamic_viscosity(read(right_cell).rho,nu,mu_right)||
       !NewtonianViscousStress::nonnegative_coefficient_mean(mu_left,mu_right,mu))return result;
    Gradient partial{},gradient{};
    for(int component=0;component<3;++component) {
        if(component==1&&grid.geometry==GridMetrics::Geometry::Cylindrical) {
            result.work_velocity[component]=.5*(left[component]+right[component]);
            partial[3*component]=(right[component]-left[component])/grid.dx1;
        } else {
            const double q_left=left[component]/low,q_right=right[component]/high;
            if(!std::isfinite(q_left)||!std::isfinite(q_right)||
               (left[component]!=0.&&q_left==0.)||(right[component]!=0.&&q_right==0.))return result;
            const double mean=.5*q_left+.5*q_right;
            result.work_velocity[component]=radius*mean;
            partial[3*component]=mean+radius*((q_right-q_left)/grid.dx1);
        }
    }
    if(!NewtonianViscousStress::physical_covariant_gradient(frame(grid),result.work_velocity,
        partial,radius,grid.SourceTheta(0),gradient)||
       !NewtonianViscousStress::stress(gradient,mu,result.stress))return result;
    result.valid=true;return result;
}

/** Geometric residual of the same radial edge variational work.
 * Sphere: M_i*u'_i=sum_e a*mu_e*r_e^4*(q_j-q_i)/(h*r_i),
 * a=(4/3,1,1). Cylinder radial strains give edge dissipation
 * (4/3)*mu_e*r_e*h*(q_f^2+q_f*r_e*dq+(r_e*dq)^2).
 * Its exact transpose contributes -r_e*h*(tau_rr+tau_phiphi)/(2*r_i)
 * to each adjacent integrated radial momentum. The other curved components
 * use their own identical traction/lever arm; cylindrical z stays scalar.
 * Subtract the ordinary FV face divergence to return only its geometric
 * residual. Thus variable mu cannot break the edge kinetic-energy identity.
 * The energy equation still uses only u_f dot tau_f at those same faces.
 */
template<class StateReader>
ARCH_INLINE bool radial_connection_source(const StateReader& read,int cell,
    const GridMetrics::GeometryView& grid,int i,double nu,Vector& output)
{
    const auto lower=radial_face(read,cell,grid,i,nu);
    const auto upper=radial_face(read,cell+1,grid,i+1,nu);
    if(!lower.valid||!upper.valid)return false;
    const double radius=grid.GetCellCenterX(i),left=grid.GetFacePosL(i),right=grid.GetFacePosR(i);
    const double volume=GridMetrics::CellVolume(grid,i,0,0);
    if(!std::isfinite(radius)||!(radius>0.)||!std::isfinite(volume)||!(volume>0.))return false;
    const bool cylinder=grid.geometry==GridMetrics::Geometry::Cylindrical;
    const double area_l=cylinder?left:left*left,area_r=cylinder?right:right*right;
    Vector candidate{};
    for(int component=0;component<3;++component)if(!(cylinder&&component==1)) {
        candidate[component]=((right/radius-1.)*area_r*upper.stress[3*component]
            -(left/radius-1.)*area_l*lower.stress[3*component])/volume;
    }
    if(cylinder)
        candidate[0]-=grid.dx1*(right*(upper.stress[0]+upper.stress[8])
            +left*(lower.stress[0]+lower.stress[8]))/(2.*radius*volume);
    for(double value:candidate)if(!std::isfinite(value))return false;
    output=candidate;return true;
}

/** Full absolute velocity row from the same radial quadratic edge form.
 * Sphere and cylindrical phi have K=a*mu*r_e^(m+2)/h and capacity M*r_i^2.
 * Cylinder radial K_ii=(4/3)*mu*(r_e^3/h +/- r_e^2/2+r_e*h/4),
 * K_ij=(4/3)*mu*(-r_e^3/h+r_e*h/4); z has K=mu*r_e/h.
 * The row includes its diagonal and every off-diagonal. Choosing dt<=1/row
 * respects FE's diagonal condition and the real spectrum of this positive
 * edge quadratic form; zero prescribed ghosts are an external extension.
 */
ARCH_INLINE bool radial_row_sums(const GridMetrics::GeometryView& grid,
    int i,bool high,double rho,double mu,Vector& output)
{
    if(grid.dim!=1||!std::isfinite(rho)||!(rho>0.)||!std::isfinite(mu)||mu<0.)return false;
    const double edge=high?grid.GetFacePosR(i):grid.GetFacePosL(i);
    if(edge==0.)return true;
    const double radius=grid.GetCellCenterX(i),adjacent=grid.GetCellCenterX(i+(high?1:-1));
    const double h=grid.dx1,volume=GridMetrics::CellVolume(grid,i,0,0);
    if(!std::isfinite(edge)||!(edge>0.)||!std::isfinite(radius)||!(radius>0.)||
       !std::isfinite(adjacent)||adjacent==0.||!std::isfinite(volume)||!(volume>0.)||
       !std::isfinite(h)||!(h>0.))return false;
    const bool cylinder=grid.geometry==GridMetrics::Geometry::Cylindrical;
    const double area=cylinder?edge:edge*edge,lever=edge/radius;
    const double base=(mu/rho)*(area/volume)/h;
    const double graph=base*lever*lever,neighbour=radius/std::abs(adjacent);
    Vector candidate=output;
    if(cylinder) {
        const double ratio=h/edge;
        candidate[0]+=(4./3.)*graph*(1.+(high?-.5:.5)*ratio+.25*ratio*ratio
            +(1.-.25*ratio*ratio)*neighbour);
        candidate[1]+=2.*base;
        candidate[2]+=graph*(1.+neighbour);
    } else {
        candidate[0]+=(4./3.)*graph*(1.+neighbour);
        candidate[1]+=graph*(1.+neighbour);candidate[2]+=graph*(1.+neighbour);
    }
    for(double value:candidate)if(!std::isfinite(value)||value<0.)return false;
    output=candidate;return true;
}

/** Form the cell stress from centred physical partials plus all connections. */
template<class StateReader>
ARCH_INLINE bool cell_stress(const StateReader& read,int cell,
    const GridMetrics::GeometryView& grid,int i,int j,double mu,Tensor& output)
{
    Vector u{};Gradient partial{},gradient{};std::array<double,3> h{};
    const double radius=grid.GetCellCenterX(i),theta=grid.SourceTheta(j);
    if(!velocity(read,cell,grid,u)||!spacings(grid,radius,theta,h)||
       !partials(read,cell,grid,h,-1,partial)||
       !covariant_gradient(grid,u,partial,radius,theta,gradient))return false;
    return NewtonianViscousStress::stress(gradient,mu,output);
}

/** Geometric part of div(tau), in the same component frame as face traction.
 * Cylinder: (-tau_phiphi/r,0,tau_rphi/r); polar permutes phi and z.
 * Sphere: (-(tau_thetatheta+tau_phiphi)/r,
 * (tau_rtheta-cot(theta)*tau_phiphi)/r,
 * (tau_rphi+cot(theta)*tau_thetaphi)/r).
 * There is no energy component: its paired face work is a scalar divergence.
 */
ARCH_INLINE bool connection_source(const Tensor& tau,
    const GridMetrics::GeometryView& grid,int i,int j,Vector& output)
{
    for(double value:tau)if(!std::isfinite(value))return false;
    // This is the exact adjoint of the point connection in cell G_i. Replacing
    // it independently by a different quadrature would break the D identity.
    const double inverse=1./grid.GetCellCenterX(i);
    if(!std::isfinite(inverse)||!(inverse>0.))return false;
    Vector candidate{};
    const auto basis=frame(grid);
    if(basis==Frame::CylindricalRZPhi)candidate={-inverse*tau[8],0.,inverse*tau[2]};
    else if(basis==Frame::CylindricalRPhiZ)candidate={-inverse*tau[4],inverse*tau[1],0.};
    else {
        const double theta=grid.SourceTheta(j),cot=grid.dim==3?std::cos(theta)/std::sin(theta):0.;
        candidate={-inverse*(tau[4]+tau[8]),inverse*(tau[1]-cot*tau[8]),
            inverse*(tau[2]+cot*tau[5])};
    }
    for(double value:candidate)if(!std::isfinite(value))return false;
    output=candidate;return true;
}

/** Absolute coefficient sums of G, followed by the identical Stokes map.
 * Face normal differences have sum 2/h, centred columns 1/h, and a paired
 * velocity has sum 1. Cell centred columns also have sum 1/h. This is a
 * triangle bound on actual velocity coefficients, not on a scalar Laplacian.
 */
ARCH_INLINE bool stress_row_sums(const GridMetrics::GeometryView& grid,
    double radius,double theta,int normal,Tensor& output)
{
    std::array<double,3> h{};if(!spacings(grid,radius,theta,h))return false;
    Gradient g{};
    for(int axis=0;axis<grid.dim;++axis)for(int component=0;component<3;++component)
        g[3*component+axis]=(axis==normal?2.:1.)/std::abs(h[axis]);
    const double inv=1./radius;
    if(frame(grid)==Frame::CylindricalRPhiZ) {g[1]+=inv;g[4]+=inv;}
    else if(frame(grid)==Frame::CylindricalRZPhi) {g[2]+=inv;g[8]+=inv;}
    else {
        const double cot=grid.dim==3?std::abs(std::cos(theta)/std::sin(theta)):0.;
        g[1]+=inv;g[4]+=inv;g[2]+=inv;g[5]+=cot*inv;g[8]+=(1.+cot)*inv;
    }
    Tensor candidate{};
    for(int a=0;a<3;++a)for(int b=0;b<3;++b)
        candidate[3*a+b]=a==b?(4.*g[3*a+a]+2.*g[3*((a+1)%3)+(a+1)%3]
            +2.*g[3*((a+2)%3)+(a+2)%3])/3.:g[3*a+b]+g[3*b+a];
    for(double value:candidate)if(!std::isfinite(value)||value<0.)return false;
    output=candidate;return true;
}

/** Actual absolute face row of the multidimensional strain adjoint.
 * A face column has the paired normal difference and centred transverse
 * columns. Actual V/(2h) products are divided by the
 * consuming cell mass capacity; the physical area cancels exactly here.
 */
ARCH_INLINE bool face_row_sums(const GridMetrics::GeometryView& grid,
    int i,int j,int k,int direction,bool high,double rho,double mu_left,double mu_right,
    Vector& output)
{
    if(!std::isfinite(rho)||!(rho>0.)||!std::isfinite(mu_left)||mu_left<0.||
       !std::isfinite(mu_right)||mu_right<0.)return false;
    const int ir=i+(high&&direction==0?1:0),jr=j+(high&&direction==1?1:0),kr=k+(high&&direction==2?1:0);
    const int il=ir-(direction==0?1:0),jl=jr-(direction==1?1:0),kl=kr-(direction==2?1:0);
    double radius=0.,theta=0.;face_point(grid,direction,ir,jr,radius,theta);
    const double area=GridMetrics::FaceArea(grid,direction,i,j,k,high);
    if(area==0.||(grid.geometry==GridMetrics::Geometry::Spherical&&grid.dim==3&&direction==1&&
        (theta==0.||theta==std::numbers::pi_v<double>)))return true;
    const double vl=std::abs(GridMetrics::CellVolume(grid,il,jl,kl)),vr=std::abs(GridMetrics::CellVolume(grid,ir,jr,kr));
    const double volume=GridMetrics::CellVolume(grid,i,j,k);
    if(!std::isfinite(vl)||!(vl>0.)||!std::isfinite(vr)||!(vr>0.)||!std::isfinite(volume)||!(volume>0.))return false;
    Tensor nl{},nr{};std::array<double,3> hl{},hr{};
    const double rl=grid.GetCellCenterX(il),rr=grid.GetCellCenterX(ir),tl=grid.SourceTheta(jl),tr=grid.SourceTheta(jr);
    if(!spacings(grid,rl,tl,hl)||!spacings(grid,rr,tr,hr)||
       !stress_row_sums(grid,rl,tl,direction,nl)||!stress_row_sums(grid,rr,tr,direction,nr))return false;
    Vector candidate=output;
    for(int a=0;a<3;++a) {
        candidate[a]+=(mu_left*vl/(2.*std::abs(hl[direction]))*nl[3*a+direction]
            +mu_right*vr/(2.*std::abs(hr[direction]))*nr[3*a+direction])/(rho*volume);
    }
    for(double value:candidate)if(!std::isfinite(value)||value<0.)return false;
    output=candidate;return true;
}
} // namespace CurvilinearViscousStress
