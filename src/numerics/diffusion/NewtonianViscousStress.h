/**
 * @file NewtonianViscousStress.h
 * @brief One Host/device zero-bulk Newtonian constitutive law in physical 3D.
 *
 * Workflow:
 * 1. The operator supplies metric-divided physical partial derivatives and
 *    an explicit frame. physical_covariant_gradient adds only its basis
 *    connection; actual axis/pole limits and gradient ownership stay upstream.
 *    The constitutive input is G_ij and mu=rho*the configured kinematic nu.
 *    Dimensional reduction does not change the three-dimensional Stokes law.
 * 2. Form tau=mu*(G+G^T-(2/3)*tr(G)*I), using diagonal differences so a true
 *    isotropic homology remains exactly stress-free for any finite mu.
 * 3. Read a selected physical column tau_n as traction, and pair u dot tau_n
 *    with the same traction. The caller owns conservative momentum/energy flux
 *    signs, geometry, quadrature, timestep and all scientific acceptance gates.
 * 4. The optional contraction Q=tau:G diagnoses local mechanical dissipation.
 *    It is not an extra thermal source: no heat is added by this leaf.
 *
 * Inputs/outputs are fixed nine/three-value arrays. There is no Grid, EOS,
 * field ownership, coefficient model, allocation, geometric floor or
 * independent PDE here. False means invalid/unrepresentable; outputs retain
 * their entry values. Full diffusion/BC/RKL/AMR qualification is separate.
 */
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>

#include "core/ArchPortability.h"

namespace NewtonianViscousStress {
/** Row-major G_ij: physical velocity component i, physical derivative axis j. */
using VelocityGradient=std::array<double,9>;
/** Row-major physical stress; all three normal components remain represented. */
using Tensor=std::array<double,9>;
/** Three physical orthonormal components, independent of computational chart. */
using Vector=std::array<double,3>;

/** Explicit order of the three orthonormal components and derivative columns.
 * A lower-dimensional computation still uses all three physical components;
 * the caller supplies its missing coordinate partials, not a reduced trace.
 */
enum class Frame {
    Cartesian,
    CylindricalRZPhi,
    CylindricalRPhiZ,
    SphericalRThetaPhi
};

namespace detail {
/** Reject every nonfinite operand before zero-coefficient or tensor shortcuts. */
template<std::size_t N>
ARCH_INLINE bool finite(const std::array<double,N>& values)
{
    for(std::size_t n=0;n<N;++n)if(!std::isfinite(values[n]))return false;
    return true;
}
/** Divide one required connection term without erasing a nonzero value.
 * Denominators are positive physical radius or sin(theta). A zero numerator
 * is a genuine zero; a nonzero quotient rounded to zero or infinity is not
 * representable for this point helper and is rejected without publishing.
 */
ARCH_INLINE bool connection_ratio(double numerator,double denominator,double& output)
{
    if(!std::isfinite(numerator)||!std::isfinite(denominator)||!(denominator>0.))return false;
    const double candidate=numerator/denominator;
    if(!std::isfinite(candidate)||(numerator!=0.&&candidate==0.))return false;
    output=candidate;return true;
}

/** Multiply a required spherical connection numerator with no tiny-value floor.
 * Exact zero operands remain zero. A nonzero unrepresentable product rejects;
 * this is a finite-representation guard, not an error certificate or scaling.
 */
ARCH_INLINE bool connection_product(double left,double right,double& output)
{
    if(!std::isfinite(left)||!std::isfinite(right))return false;
    const double candidate=left*right;
    if(!std::isfinite(candidate)||(left!=0.&&right!=0.&&candidate==0.))return false;
    output=candidate;return true;
}
} // namespace detail

/** Convert configured kinematic viscosity to its physical dynamic value.
 * mu=rho*nu is evaluated once with the original arithmetic. A genuinely zero
 * nu disables viscosity; a positive product rounded to zero or infinity is
 * unrepresentable and must not silently disable transport. Failure leaves the
 * caller's value intact; there is no density/coefficient floor or new control.
 */
ARCH_INLINE bool dynamic_viscosity(double rho,double nu,double& output)
{
    if(!std::isfinite(rho)||!(rho>0.)||!std::isfinite(nu)||nu<0.)return false;
    const double candidate=rho*nu;
    if(!std::isfinite(candidate)||(nu>0.&&candidate==0.))return false;
    output=candidate;return true;
}

/** Average finite nonnegative physical coefficients without losing half-units.
 * Ordinary values retain (a/2+b/2). If a positive half enters the subnormal
 * range, sum before halving: at least one operand is tiny, so this sum cannot
 * overflow. For example denorm_min averaged with itself remains denorm_min,
 * and its mean with twice denorm_min rounds to twice denorm_min. A positive
 * mean which rounds to zero rejects atomically rather than disabling physics.
 * The limit is binary64 representability, not a configurable small-value floor.
 */
ARCH_INLINE bool nonnegative_coefficient_mean(double left,double right,double& output)
{
    if(!std::isfinite(left)||left<0.||!std::isfinite(right)||right<0.)return false;
    constexpr double half_normal_limit=2.*std::numeric_limits<double>::min();
    const bool subnormal_half=(left>0.&&left<half_normal_limit)
        ||(right>0.&&right<half_normal_limit);
    const double candidate=subnormal_half?.5*(left+right):.5*left+.5*right;
    if(!std::isfinite(candidate)||((left>0.||right>0.)&&candidate==0.))return false;
    output=candidate;return true;
}

/** Add orthonormal basis connections to actual metric-divided point partials.
 * Workflow:
 * 1. Validate the explicit frame, all finite operands and the physical domain.
 *    Cartesian does not require a positive radius. Curved frames require r>0;
 *    the spherical frame requires 0<theta<pi. Actual axes/poles are upstream.
 * 2. Copy physical_partials, whose columns already include all metric factors.
 *    Add only connection terms: RZPhi has G_rphi-=u_phi/r and G_phiphi+=u_r/r;
 *    RPhiZ has G_rphi-=u_phi/r and G_phiphi+=u_r/r in its reordered columns.
 *    Spherical adds G_rtheta-=u_theta/r, G_thetatheta+=u_r/r,
 *    G_rphi-=u_phi/r, G_thetaphi-=cot(theta)*u_phi/r, and
 *    G_phiphi+=u_r/r+cot(theta)*u_theta/r.
 * 3. Form spherical cotangent products as cos(theta)*(u_i/r)/sin(theta),
 *    avoiding an unnecessary standalone cot(theta) overflow for zero velocity.
 *    Required nonzero intermediate connection terms must remain representable;
 *    exact zero inputs and legitimate final cancellation remain zero.
 * 4. Publish the complete finite 3x3 result once. Failure leaves output intact,
 *    including when output aliases the original caller's partial array.
 * This does not construct derivatives, certify axis limits, change the 3D
 * Stokes trace, or establish FV/CF/dissipation/RKL/scientific qualification.
 */
ARCH_INLINE bool physical_covariant_gradient(Frame frame,Vector velocity,
    VelocityGradient physical_partials,double radius,double theta,
    VelocityGradient& output)
{
    if(!detail::finite(velocity)||!detail::finite(physical_partials)||
       !std::isfinite(radius)||!std::isfinite(theta))return false;
    if(frame!=Frame::Cartesian&&frame!=Frame::CylindricalRZPhi&&
       frame!=Frame::CylindricalRPhiZ&&frame!=Frame::SphericalRThetaPhi)return false;
    VelocityGradient candidate=physical_partials;
    if(frame==Frame::Cartesian) {output=candidate;return true;}
    if(!(radius>0.))return false;
    double radial=0.,azimuthal=0.;
    const int phi=frame==Frame::CylindricalRPhiZ?1:2;
    if(!detail::connection_ratio(velocity[0],radius,radial)||
       !detail::connection_ratio(velocity[phi],radius,azimuthal))return false;
    if(frame==Frame::SphericalRThetaPhi) {
        if(!(theta>0.)||!(theta<std::numbers::pi_v<double>))return false;
        const double sine=std::sin(theta),cosine=std::cos(theta);
        if(!std::isfinite(sine)||!(sine>0.)||!std::isfinite(cosine))return false;
        double polar=0.,phi_numerator=0.,polar_numerator=0.,phi_cot=0.,polar_cot=0.;
        if(!detail::connection_ratio(velocity[1],radius,polar)||
           !detail::connection_product(cosine,azimuthal,phi_numerator)||
           !detail::connection_product(cosine,polar,polar_numerator)||
           !detail::connection_ratio(phi_numerator,sine,phi_cot)||
           !detail::connection_ratio(polar_numerator,sine,polar_cot))return false;
        candidate[1]-=polar;
        candidate[4]+=radial;
        candidate[2]-=azimuthal;
        candidate[5]-=phi_cot;
        candidate[8]+=radial+polar_cot;
    } else {
        candidate[phi]-=azimuthal;
        candidate[3*phi+phi]+=radial;
    }
    if(!detail::finite(candidate))return false;
    output=candidate;return true;
}

/** Atomically evaluate the global three-dimensional Stokes stress.
 * Diagonal identity: 2*G_ii-(2/3)*tr(G)
 * =(2/3)*[(G_ii-G_jj)+(G_ii-G_kk)]. Off-diagonal: G_ij+G_ji.
 * No output is published until every component is finite; mu==0 returns exact
 * zero only after validating the actual finite gradient and coefficient.
 */
ARCH_INLINE bool stress(const VelocityGradient& gradient,double mu,Tensor& output)
{
    if(!std::isfinite(mu)||mu<0.||!detail::finite(gradient))return false;
    Tensor candidate{};
    if(mu!=0.) {
        for(int i=0;i<3;++i) {
            const int j=(i+1)%3,k=(i+2)%3;
            candidate[3*i+i]=mu*((2./3.)*((gradient[3*i+i]-gradient[3*j+j])
                +(gradient[3*i+i]-gradient[3*k+k])));
            for(int j2=i+1;j2<3;++j2) {
                const double value=mu*(gradient[3*i+j2]+gradient[3*j2+i]);
                candidate[3*i+j2]=candidate[3*j2+i]=value;
            }
        }
    }
    if(!detail::finite(candidate))return false;
    output=candidate;return true;
}

/** Extract tau_n without inventing an outward sign or geometric lever arm.
 * A physical normal axis n uses traction_i=tau_(i,n). The operator supplies
 * the actual side orientation and conservative flux sign separately.
 */
ARCH_INLINE bool traction(const Tensor& tensor,int direction,Vector& output)
{
    if(direction<0||direction>=3||!detail::finite(tensor))return false;
    const Vector candidate{tensor[direction],tensor[3+direction],tensor[6+direction]};
    output=candidate;return true;
}

/** Pair physical velocity with exactly the traction already used for momentum.
 * Power density=u dot tau_n, with no heating or second stress evaluation.
 */
ARCH_INLINE bool power(const Vector& velocity,const Vector& face_traction,double& output)
{
    if(!detail::finite(velocity)||!detail::finite(face_traction))return false;
    const double candidate=velocity[0]*face_traction[0]
        +velocity[1]*face_traction[1]+velocity[2]*face_traction[2];
    if(!std::isfinite(candidate))return false;
    output=candidate;return true;
}

/** Contract this law's stress with its actual gradient, Q=tau:G.
 * For a genuine Newtonian tensor Q=2*mu*dev(sym(G)):dev(sym(G))>=0.
 * A negative/nonfinite computed contraction is rejected, never clipped or used
 * as a thermal repair. Arbitrary caller tensors are not certified by this API.
 */
ARCH_INLINE bool dissipation(const Tensor& tensor,const VelocityGradient& gradient,double& output)
{
    if(!detail::finite(tensor)||!detail::finite(gradient))return false;
    double candidate=0.;
    for(int n=0;n<9;++n)candidate+=tensor[n]*gradient[n];
    if(!std::isfinite(candidate)||candidate<0.)return false;
    output=candidate;return true;
}

/** Product-weighted paired traction on one uniform Cartesian physical face.
 * Workflow:
 * 1. Validate the direction, positive spacing, both actual velocities, all nine
 *    centred physical gradient entries and nonnegative dynamic viscosities.
 *    Inactive Cartesian directions have actual zero partials supplied by the
 *    caller; the trace remains three-dimensional. Only then may two zero
 *    coefficients publish exact zero, even for finite enormous gradients.
 * 2. Set mu_f=(mu_L+mu_R)/2 with the guarded nonnegative coefficient mean,
 *    retaining normal half-products and repairing only subnormal half-units;
 *    then compute the actual face differences
 *    d_j u_i=(u_i,R-u_i,L)/h. G_ij means component i, derivative direction j.
 * 3. For the normal column use
 *    t_j=(4/3)*mu_f*d_j u_j-(2/3)*(mu_L*sum_(k!=j)G_kk,L
 *                                  +mu_R*sum_(k!=j)G_kk,R)/2.
 *    For i!=j use t_i=mu_f*d_j u_i+(mu_L*G_ji,L+mu_R*G_ji,R)/2.
 *    The transverse derivative is coefficient-product weighted. Replacing it
 *    by mu_f times an averaged gradient is a different operator when mu varies.
 * 4. Publish the finite complete vector once. By-value inputs make failure
 *    atomic even if output aliases one caller velocity array. There is no
 *    outward sign, face velocity choice, additional heat or timestep here;
 *    the caller can pair the same traction with power() for physical work.
 * This is a proposed uniform-Cartesian face law, not a certificate for global
 * work, FV/CF, native/curved geometry, boundaries, RKL or scientific release.
 */
ARCH_INLINE bool cartesian_paired_traction(int direction,double spacing,
    Vector left_velocity,Vector right_velocity,VelocityGradient left_gradient,
    VelocityGradient right_gradient,double mu_left,double mu_right,Vector& output)
{
    if(direction<0||direction>=3||!std::isfinite(spacing)||!(spacing>0.)||
       !std::isfinite(mu_left)||mu_left<0.||!std::isfinite(mu_right)||mu_right<0.||
       !detail::finite(left_velocity)||!detail::finite(right_velocity)||
       !detail::finite(left_gradient)||!detail::finite(right_gradient))return false;
    Vector candidate{};
    if(mu_left==0.&&mu_right==0.) {output=candidate;return true;}
    double mu_face=0.;
    if(!nonnegative_coefficient_mean(mu_left,mu_right,mu_face))return false;
    Vector difference{};
    for(int i=0;i<3;++i) {
        difference[i]=(right_velocity[i]-left_velocity[i])/spacing;
        if(!std::isfinite(difference[i]))return false;
    }
    double transverse_left=0.,transverse_right=0.;
    for(int k=0;k<3;++k)if(k!=direction) {
        transverse_left+=left_gradient[3*k+k];
        transverse_right+=right_gradient[3*k+k];
    }
    candidate[direction]=(4./3.)*(mu_face*difference[direction])
        -(2./3.)*(.5*(mu_left*transverse_left+mu_right*transverse_right));
    for(int i=0;i<3;++i)if(i!=direction)
        candidate[i]=mu_face*difference[i]
            +.5*(mu_left*left_gradient[3*direction+i]
                 +mu_right*right_gradient[3*direction+i]);
    if(!detail::finite(candidate))return false;
    output=candidate;return true;
}
} // namespace NewtonianViscousStress
