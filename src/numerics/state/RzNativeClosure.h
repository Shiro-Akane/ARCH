/**
 * @file RzNativeClosure.h
 * @brief Shared numerical thermodynamic closure of native full-ring RZ means.
 *
 * Workflow:
 * 1. Borrow the density polynomial/moments of the same stage and ghost stencil.
 * 2. Map m_phi=J/W to an effective ordinary momentum with the same rotational
 *    kinetic mean, then use the existing strict energy/bounds validation.
 * 3. Expose the mean EOS input and a conservative physical baseline at radius r.
 *
 * I_* is the inertia of the explicit numerical density reconstruction, not a
 * certificate of an unknown subcell physical field. No new evolved field, EOS,
 * floor, heating term or backend-specific formula is introduced. The caller
 * owns slot/version/ghost/topology identity and the final physical EOS checks.
 */
#pragma once

#include <cmath>
#include <limits>

#include "core/ArchPortability.h"
#include "data/FluidState.h"
#include "numerics/reconstruction/RzDensityMoments.h"
#include "numerics/state/StateAdmissibility.h"

namespace RzThermodynamics {

/** Stack-local native closure; effective_mean is an EOS input, not evolved U. */
struct Cell {
    RzDensity::Cell density;
    FluidVector effective_mean{};
    double radial_velocity=0.,axial_velocity=0.,omega=0.,internal=0.;
    arch::state::Status status=arch::state::Status::invalid_thermodynamics;
    ARCH_INLINE bool valid() const {return status==arch::state::Status::valid;}
};

/** Convert one native mean using the explicit density reconstruction inertia.
 * kappa=rho_V*W^2/(V*I_*). Factoring out h and radius gives
 * kappa=rho_V*weighted_two^2/(density_scale*weighted_V*abs(weighted_three)),
 * weighted_V=abs(r_left/R)+abs(r_right/R). This works for reflected negative
 * ghosts as well: inertia in the kinetic energy is positive while omega retains
 * the signed density capacity/parity used by the physical point profile.
 */
ARCH_INLINE Cell from_density(const FluidVector& native,const RzDensity::Cell& density,
    const arch::state::Bounds& bounds={})
{
    Cell result{};result.density=density;
    if(!RzDensity::detail::cell_valid(density))return result;
    if(!std::isfinite(native.rho)||!std::isfinite(native.mom_u)
       ||!std::isfinite(native.mom_v)||!std::isfinite(native.mom_w)
       ||!std::isfinite(native.eng)) {
        result.status=arch::state::Status::nonfinite;return result;
    }
    if(!(native.rho>0.)) {
        result.status=arch::state::Status::nonpositive_density;return result;
    }
    if(!std::isfinite(bounds.density)||bounds.density<0.
       ||!std::isfinite(bounds.internal_min)||bounds.internal_min<0.
       ||!std::isfinite(bounds.internal_max)||bounds.internal_max<bounds.internal_min)
        return result;
    const double volume_weight=std::abs(density.lower/density.radius_scale)
        +std::abs(density.upper/density.radius_scale);
    const double numerator[]{native.rho,density.weighted_two,density.weighted_two};
    const double denominator[]{density.density_scale,volume_weight,
        std::abs(density.weighted_three)};
    const double kappa=RzDensity::detail::scaled_value(numerator,3,denominator,3);
    if(!std::isfinite(kappa)||!(kappa>0.))return result;
    result.effective_mean=native;
    const double momentum[]{native.mom_w,std::sqrt(kappa)};
    result.effective_mean.mom_w=RzDensity::detail::scaled_value(momentum,2);
    result.omega=RzDensity::angular_velocity(native.mom_w,density);
    if(!std::isfinite(result.omega)) {
        result.status=arch::state::Status::nonfinite;return result;
    }
    result.status=arch::state::validate(result.effective_mean,nullptr,0,1,
        bounds.density,bounds.internal_min,bounds.internal_max);
    if(!result.valid())return result;
    const auto kinematics=arch::state::recover(result.effective_mean);
    result.radial_velocity=kinematics.u;result.axial_velocity=kinematics.v;
    result.internal=kinematics.internal;
    return result;
}

/** Bind the same three native density means and real signed cell geometry. */
template<class StateReader>
ARCH_INLINE Cell make_cell(const StateReader& read,int index,
    const GridMetrics::GeometryView& grid,int i,const arch::state::Bounds& bounds={})
{
    return from_density(read(index),RzDensity::density_cell(read,index,grid,i),bounds);
}

/** Physical conservative baseline whose mathematical V/W means are native U.
 * m_r=rho*u_r, m_z=rho*u_z, m_phi=rho*omega*r;
 * E=rho*e0 + (m_r*u_r+m_z*u_z+m_phi*u_phi)/2.
 * Caller checks the resulting point with the actual EOS/physical bounds.
 */
ARCH_INLINE FluidVector base_point(const Cell& cell,double radius)
{
    if(!cell.valid()||!std::isfinite(radius)) {
        const double bad=std::numeric_limits<double>::quiet_NaN();
        return {bad,bad,bad,bad,bad};
    }
    const double rho=cell.density.density.at((radius-cell.density.origin)/cell.density.spacing);
    const double velocity=cell.omega*radius;
    const double mr=rho*cell.radial_velocity,mz=rho*cell.axial_velocity,mphi=rho*velocity;
    const double energy=rho*cell.internal+(.5*mr)*cell.radial_velocity
        +(.5*mz)*cell.axial_velocity+(.5*mphi)*velocity;
    return {rho,mr,mz,mphi,energy};
}

/** Physical derivative of exactly the same baseline, including rotational work. */
ARCH_INLINE FluidVector base_derivative(const Cell& cell,double radius)
{
    if(!cell.valid()||!std::isfinite(radius))return base_point(cell,arch::state::invalid());
    const double t=(radius-cell.density.origin)/cell.density.spacing;
    const auto& polynomial=cell.density.density;
    const double rho=polynomial.at(t);
    // Keep 2*t together: 2*c2 can overflow at t=0 even when p'(0)=c1 is finite.
    const double gradient=(polynomial.linear+polynomial.quadratic*(2.*t))/cell.density.spacing;
    const double velocity=cell.omega*radius;
    const double angular=gradient*velocity+rho*cell.omega;
    const double mr=gradient*cell.radial_velocity,mz=gradient*cell.axial_velocity;
    const double energy=gradient*cell.internal+(.5*mr)*cell.radial_velocity
        +(.5*mz)*cell.axial_velocity+(.5*angular)*velocity
        +(.5*rho*velocity)*cell.omega;
    return {gradient,mr,mz,angular,energy};
}
} // namespace RzThermodynamics
