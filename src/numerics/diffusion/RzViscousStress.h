/**
 * @file RzViscousStress.h
 * @brief Shared conservative RZ angular-velocity graph and paired stress work.
 *
 * Workflow:
 * 1. Consume the shared density-only cell (quadratic rho ray, signed capacity
 *    C=int rho*r^3 dr and graph abscissa s=int rho*r^5 dr/C) from the leaf
 *    numerics/reconstruction/RzDensityMoments.h.
 * 2. Require a finite stored m_phi=J/W and infer temporary
 *    omega=J/(2*pi*dz*C) with the shared angular_velocity rule.
 * 3. Use tau_rphi=mu*r*d_r(omega) on positive radial graph links, or the
 *    corresponding axial omega difference, and pair the same traction/work.
 * 4. Return face fluxes in their distinct torque/area normalizations and the
 *    matching nonnegative row rate to the existing divergence/timestep owner.
 *    An axial row divides by the shared face distance and separately by the
 *    actual current-cell height; only uniform cells make those lengths equal.
 *
 * Density is invariant in pure diffusion. Frozen nu gives fixed positive
 * capacities and symmetric positive links, with eigenvalues in [-2*q_max,0].
 * State-dependent coefficients give a local frozen certificate; this leaf does
 * not certify a nonlinear full-tensor RKL step. It uses no EOS, extra evolved
 * angular array, heating term or density floor. Both backends share this math.
 * AngularCell publicly extends the shared RzDensity::Cell and adds omega alone;
 * all density/capacity/abscissa math comes from that shared leaf, while the
 * finite-m_phi check, omega and every link/face/row guard stay here unchanged.
 */
#pragma once

#include <cmath>
#include <limits>

#include "core/ArchPortability.h"
#include "grid/GridGeometryView.h"
#include "numerics/reconstruction/RzDensityMoments.h"

namespace RzViscousStress {
/** Phi traction in torque normalization and paired area work density. */
struct FaceFlux { double momentum=0.,energy=0.; bool valid=false; };

/** Shared density cell plus the temporary angular velocity; no new state.
 * density is p(t), t=(r-origin)/spacing. C=int rho*r^3 dr is signed in a
 * wholly negative reflected ghost, while s=int rho*r^5 dr/C stays positive.
 * Only omega is added on top of the shared RzDensity::Cell.
 */
struct AngularCell : public RzDensity::Cell {
    double omega=0.;
};

namespace detail {
/** Shared density algebra (exponent-scaled product/quotient) used below. */
using RzDensity::detail::scaled_value;

/** Shared density validity plus the finite omega this leaf requires. */
ARCH_INLINE bool cell_valid(const AngularCell& cell)
{
    return RzDensity::detail::cell_valid(static_cast<const RzDensity::Cell&>(cell))&&
        std::isfinite(cell.omega);
}

/** Symmetric positive radial connection used by both flux and timestep math. */
struct RadialLink {double density=0.,delta_s=0.;bool valid=false;};

/** Bind an actual adjacent radial face; other BC traces require their own owner. */
ARCH_INLINE RadialLink radial_link(const AngularCell& left,
    const AngularCell& right,double radius)
{
    RadialLink result{};
    if(!(radius>0.)||!std::isfinite(radius)||!cell_valid(left)||!cell_valid(right)||
       left.lower<0.||right.lower<0.||left.upper!=radius||right.lower!=radius)return result;
    const double square=radius*radius,delta=right.mean_s-left.mean_s;
    if(!std::isfinite(square)||!std::isfinite(delta)||!(delta>0.)||
       square<left.mean_s||square>right.mean_s)return result;
    const double low=left.density.at((radius-left.origin)/left.spacing);
    const double high=right.density.at((radius-right.origin)/right.spacing);
    if(!std::isfinite(low)||!std::isfinite(high)||!(low>0.)||!(high>0.))return result;
    result.density=.5*low+.5*high;result.delta_s=delta;
    result.valid=std::isfinite(result.density)&&result.density>0.;return result;
}

/** Require the same positive physical annulus on both sides of an axial face. */
ARCH_INLINE bool axial_pair(const AngularCell& left,const AngularCell& right)
{
    return cell_valid(left)&&cell_valid(right)&&left.lower>=0.&&
        left.lower==right.lower&&left.upper==right.upper;
}
} // namespace detail

/** Fit density alone (shared leaf) and add the temporary angular velocity.
 * The finite-m_phi policy stays here: the shared density cell never reads a
 * momentum state and never sees an EOS. Invalid geometry/density returns an
 * invalid cell through the same guards the original angular cell used.
 */
template<class StateReader>
ARCH_INLINE AngularCell angular_cell(const StateReader& read,int index,
    const GridMetrics::GeometryView& grid,int i)
{
    AngularCell result{};
    const auto cell=RzDensity::density_cell(read,index,grid,i);
    if(!cell.valid)return result;
    const auto middle=read(index);
    if(!std::isfinite(middle.mom_w))return result;
    static_cast<RzDensity::Cell&>(result)=cell;
    result.omega=RzDensity::angular_velocity(middle.mom_w,cell);
    result.valid=detail::cell_valid(result);
    return result;
}

/** Same symmetric traction and its conservative face work.
 * Radial tau=2*nu*rho_face*r_face^2*domega/ds, s=<r^2>_(rho*r^3).
 * Axial torque=-nu*C_face*domega/dz uses int r^2 dr; energy uses int r dr.
 * Axis traction/work are the exact regular zero limit.
 */
ARCH_INLINE FaceFlux azimuthal_face(const AngularCell& left,const AngularCell& right,
    int direction,double normal_spacing,double nu,double radial_face)
{
    FaceFlux result{};
    if((direction!=0&&direction!=1)||!std::isfinite(normal_spacing)||
       !(normal_spacing>0.)||!std::isfinite(nu)||nu<0.||
       !detail::cell_valid(left)||!detail::cell_valid(right))return result;
    if(direction==0) {
        if(!std::isfinite(radial_face)||radial_face<0.)return result;
        if(radial_face==0.) {
            if(left.upper!=0.||right.lower!=0.)return result;
            return {0.,0.,true};
        }
        const auto link=detail::radial_link(left,right,radial_face);
        if(!link.valid)return result;
        const double difference=right.omega-left.omega;
        if(!std::isfinite(difference))return result;
        const double numerator[]{2.,nu,link.density,radial_face,radial_face,difference};
        const double denominator[]{link.delta_s};
        const double tau=detail::scaled_value(numerator,6,denominator,1);
        const double fraction=(radial_face*radial_face-left.mean_s)/link.delta_s;
        const double omega=(1.-fraction)*left.omega+fraction*right.omega;
        const double velocity=radial_face*omega;
        if(!std::isfinite(tau)||!std::isfinite(omega)||!std::isfinite(velocity))return result;
        result.momentum=-tau;result.energy=-velocity*tau;
    } else {
        if(!detail::axial_pair(left,right))return result;
        const double capacity=.5*left.capacity+.5*right.capacity;
        const double difference=right.omega-left.omega;
        const double ratio=left.lower/left.upper,width=left.upper-left.lower;
        const double numerator[]{-nu,capacity,difference};
        const double torque_denominator[]{normal_spacing,width,left.upper,left.upper,
            (1.+ratio+ratio*ratio)/3.};
        const double mean_omega=.5*left.omega+.5*right.omega;
        const double energy_numerator[]{-nu,capacity,difference,mean_omega};
        const double area_denominator[]{normal_spacing,width,left.upper,.5*(1.+ratio)};
        result.momentum=detail::scaled_value(numerator,3,torque_denominator,5);
        result.energy=detail::scaled_value(energy_numerator,4,area_denominator,4);
    }
    result.valid=std::isfinite(result.momentum)&&std::isfinite(result.energy);return result;
}

/** Nonnegative row contribution from the same actual frozen face connection.
 * Radial K=2*nu*rho_face*r_face^4/|s_adj-s_center|, q=K/C_center.
 * Axial K=nu*C_face/d_face, q=K/(dz_center*C_center). Summing all faces
 * supplies the original dtFE graph bound, with actual capacity height rather
 * than a second copy of the gradient distance. Existing callers that omit
 * current_height retain their original uniform-spacing denominator; zero is
 * solely that optional-argument marker, never a Native physical cell height.
 * Native callers must explicitly supply their validated positive actual dz.
 * Invalid geometry/density/coefficient yields NaN, never a fabricated bound.
 */
ARCH_INLINE double face_row_rate(const AngularCell& center,const AngularCell& adjacent,
    int direction,double normal_spacing,double nu,double radial_face,
    double current_height=0.)
{
    const double invalid=std::numeric_limits<double>::quiet_NaN();
    if((direction!=0&&direction!=1)||!std::isfinite(normal_spacing)||
       !(normal_spacing>0.)||!std::isfinite(nu)||nu<0.||
       !detail::cell_valid(center)||!detail::cell_valid(adjacent)||center.lower<0.)return invalid;
    double rate=invalid;
    if(direction==0) {
        if(!std::isfinite(radial_face)||radial_face<0.)return invalid;
        if(radial_face==0.)
            return center.lower==0.&&adjacent.upper==0.?0.:invalid;
        const bool adjacent_left=adjacent.upper==radial_face&&center.lower==radial_face;
        const auto link=adjacent_left?detail::radial_link(adjacent,center,radial_face)
            :detail::radial_link(center,adjacent,radial_face);
        if(!link.valid)return invalid;
        const double numerator[]{2.,nu,link.density,radial_face,radial_face,radial_face,radial_face};
        const double denominator[]{link.delta_s,center.capacity};
        rate=detail::scaled_value(numerator,7,denominator,2);
    } else {
        if(!detail::axial_pair(center,adjacent))return invalid;
        const double height=current_height==0.?normal_spacing:current_height;
        if(!std::isfinite(height)||!(height>0.))return invalid;
        const double capacity=.5*center.capacity+.5*adjacent.capacity;
        const double numerator[]{nu,capacity};
        const double denominator[]{normal_spacing,height,center.capacity};
        rate=detail::scaled_value(numerator,2,denominator,3);
    }
    return std::isfinite(rate)&&rate>=0.?rate:invalid;
}
} // namespace RzViscousStress
