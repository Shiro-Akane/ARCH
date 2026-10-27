/**
 * @file StationarySlipWallFlux.h
 * @brief Shared stationary impermeable/slip-wall point flux.
 *
 * Workflow:
 * 1. Receive a true physical point and already queried pressure/sound speed.
 * 2. Select the exact gamma-law wall only for roe_gamma_minus_one capability.
 *    Other EOS use their actual interior pressure as a numerical wall law.
 * 3. Retain the original rarefaction/shock solve or actual pressure, respectively.
 * 4. Return zero advective/energy flux and actual normal pressure traction.
 *
 * Common InvariantDomainFlux owns selected-baseline limiting and face assembly.
 * This leaf owns no wall identity, limiter, CFL, state publication or Runtime
 * qualification. Species advection is zero; its caller publishes those zeros
 * in the same accepted face bundle. The general-EOS law is not an exact
 * Riemann solve; its selected baseline must pass the original two-bar test.
 */
#pragma once

#include "core/MathClassification.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "amr/exchange/BoundaryPlan.h"
#include "core/ArchPortability.h"
#include "data/FluidState.h"

namespace StationarySlipWallFlux {
namespace detail {
/** Check every physical component without modifying or repairing the point. */
ARCH_INLINE bool finite_point(const FluidVector& point) {
    return arch::math::isfinite(point.rho)&&arch::math::isfinite(point.mom_u)&&arch::math::isfinite(point.mom_v)
        &&arch::math::isfinite(point.mom_w)&&arch::math::isfinite(point.eng);
}
/** Check half-open borrowed species spans by integer address, without
 * undefined relational pointer comparisons or overflowing endpoint arithmetic.
 * A caller must supply actual S-element arrays; this only verifies disjointness.
 */
ARCH_INLINE bool species_ranges_disjoint(const double* scratch,const double* input,
    std::uintptr_t bytes) {
    const auto destination=reinterpret_cast<std::uintptr_t>(scratch);
    const auto source=reinterpret_cast<std::uintptr_t>(input);
    const auto maximum=std::numeric_limits<std::uintptr_t>::max();
    if(destination>maximum-bytes||source>maximum-bytes)return false;
    return destination+bytes<=source||source+bytes<=destination;
}
/** Validate exact count representation and all three required scratch exclusions.
 * Zero species consumes no pointer; no numerical inputs or outputs are touched.
 */
ARCH_INLINE bool species_scratch_disjoint(int species,double* scratch,
    const double* left,const double* right,const double* high) {
    if(species==0)return true;
    const auto count=static_cast<std::size_t>(species);
    if(count>std::numeric_limits<std::size_t>::max()/sizeof(double)
        ||static_cast<std::uintmax_t>(count)
            >std::numeric_limits<std::uintptr_t>::max()/sizeof(double))return false;
    const auto bytes=static_cast<std::uintptr_t>(count)*sizeof(double);
    return species_ranges_disjoint(scratch,left,bytes)
        &&species_ranges_disjoint(scratch,right,bytes)
        &&species_ranges_disjoint(scratch,high,bytes);
}
/** Recover a representable positive P*exp(t) after the direct exponential
 * underflows. Workflow: retain P's binary mantissa/exponent, split t into a
 * whole binary exponent and bounded remainder, check the shift before any
 * integer conversion, then perform the final IEEE scalbn rounding once.
 * P=m*2^e, t=k*ln(2)+f gives P*exp(t)=(m*exp(f))*2^(e+k).
 * No logarithm of P, pressure floor or zero-to-positive clamp is used. This
 * is a finite-representation fallback, not a complete scalar-range guarantee.
 * Only a finite strictly positive final product is published; output is atomic.
 */
ARCH_INLINE bool positive_exp_product(double pressure,double logarithm,double& output) {
    if(!arch::math::isfinite(pressure)||!(pressure>0.)||!arch::math::isfinite(logarithm))return false;
    int pressure_exponent=0;
    const double mantissa=std::frexp(pressure,&pressure_exponent);
    // Correctly rounded binary64 ln(2); exact dyadic exponent witnesses use
    // the same rounded product for t and k*ln(2), hence a zero remainder.
    constexpr double ln2=0x1.62e42fefa39efp-1;
    const double shift=std::floor(logarithm/ln2);
    // The remainder product is in [1/2,2) in real arithmetic. Include the
    // nearest-rounding subnormal boundary; scalbn still decides actual zero.
    const double minimum_shift=double(std::numeric_limits<double>::min_exponent)
        -double(std::numeric_limits<double>::digits)-1.-double(pressure_exponent);
    const double maximum_shift=double(std::numeric_limits<double>::max_exponent)
        +1.-double(pressure_exponent);
    if(!arch::math::isfinite(shift)||shift<minimum_shift||shift>maximum_shift
        ||shift<double(std::numeric_limits<int>::min())
        ||shift>double(std::numeric_limits<int>::max()))return false;
    const double combined_exponent=double(pressure_exponent)+shift;
    if(combined_exponent<double(std::numeric_limits<int>::min())
        ||combined_exponent>double(std::numeric_limits<int>::max()))return false;
    const double remainder=logarithm-shift*ln2;
    const double residual_product=mantissa*std::exp(remainder);
    if(!arch::math::isfinite(remainder)||!arch::math::isfinite(residual_product)
        ||!(residual_product>0.))return false;
    const double candidate=std::scalbn(residual_product,static_cast<int>(combined_exponent));
    if(!arch::math::isfinite(candidate)||!(candidate>0.))return false;
    output=candidate;
    return true;
}
} // namespace detail

/** Shared finite flux check; numerical callers do not depend on a chart-specific owner. */
ARCH_INLINE bool finite_flux(const FluidVector& flux) {return detail::finite_point(flux);}

/** Optional-high disposition distinguishes unresolved trials from invalid wall data. */
enum class StationaryCandidateStatus {canonical,nonfinite_trial,invalid};
/** Canonicalize a FINITE stationary wall high BEFORE its joint limiter factor.
 * Workflow: validate metadata, inspect all five components and every species
 * without writes, reject negative finite normal traction, then retain that
 * traction and set all advective/energy/species components to exact zero.
 * A nonfinite optional trial remains byte unchanged so the original factor
 * can contract to theta=0; this function never converts a NaN trial to success.
 * The caller supplies disjoint actual S-entry species storage. This is point
 * mathematics only: it does not authenticate wall identity or required EOS.
 */
ARCH_INLINE StationaryCandidateStatus stationary_candidate(int direction,
    FluidVector& candidate,double* species_flux,int species) {
    if(direction<0||direction>2||species<0||(species&&!species_flux))
        return StationaryCandidateStatus::invalid;
    if(!finite_flux(candidate))return StationaryCandidateStatus::nonfinite_trial;
    for(int s=0;s<species;++s)if(!arch::math::isfinite(species_flux[s]))
        return StationaryCandidateStatus::nonfinite_trial;
    const double pressure=direction==0?candidate.mom_u:
        direction==1?candidate.mom_v:candidate.mom_w;
    if(pressure<0.)return StationaryCandidateStatus::invalid;
    FluidVector canonical{};
    if(direction==0)canonical.mom_u=pressure;
    else if(direction==1)canonical.mom_v=pressure;else canonical.mom_w=pressure;
    candidate=canonical;
    for(int s=0;s<species;++s)species_flux[s]=0.;
    return StationaryCandidateStatus::canonical;
}

/** Mirror only physical normal momentum with the original reflection sign.
 * Workflow: borrow an actual physical point -> select its orthonormal normal
 * component -> apply the shared Reflecting sign; rho/E/tangential bytes stay.
 * Caller must validate direction 0/1/2 and prove actual wall identity separately.
 * This is not Native axis parity, world-coordinate rotation or a BC authority.
 */
ARCH_INLINE FluidVector reflected_point(const FluidVector& interior,int direction) {
    auto result=interior;
    const auto axis=static_cast<arch::boundary::BoundaryAxis>(direction);
    const auto field=direction==0?arch::boundary::BoundaryFieldClass::MomentumX:
        direction==1?arch::boundary::BoundaryFieldClass::MomentumY:
        arch::boundary::BoundaryFieldClass::MomentumZ;
    const double sign=arch::boundary::reflection_sign(axis,
        arch::boundary::BoundaryType::Reflecting,field);
    if(direction==0)result.mom_u*=sign;
    else if(direction==1)result.mom_v*=sign;else result.mom_w*=sign;
    return result;
}

/** Exact stationary slip-wall pressure for an explicit gamma-law EOS.
 * Workflow: validate the gamma-law parameters and physical endpoint acoustics;
 * return unchanged P at rest; solve the rarefaction invariant or shock jump;
 * publish only a representable pressure. Away speed is +u at a lower wall
 * and -u at an upper wall. No state, EOS energy or heat is modified.
 * Rarefaction: c_wall/c=1-(gamma-1)*v/(2c),
 * P_wall/P=(c_wall/c)^(2*gamma/(gamma-1)); a genuine vacuum has P_wall=0.
 * Compression: K=gamma*(gamma+1)*(v/c)^2/2, B=(gamma-1)/(gamma+1),
 * P_wall/P=1+K/2+sqrt((K/2)^2+K*(1+B)). hypot avoids squaring K/2.
 * This is point gamma-law mathematics, not a general-EOS or Runtime grant.
 */
ARCH_INLINE bool gamma_wall_pressure(double gamma_minus_one,double pressure,
    double sound_speed,double away_speed,double& output)
{
    if(!arch::math::isfinite(gamma_minus_one)||!(gamma_minus_one>0.)
        ||!arch::math::isfinite(pressure)||!(pressure>0.)
        ||!arch::math::isfinite(sound_speed)||!(sound_speed>0.)
        ||!arch::math::isfinite(away_speed)) return false;
    if(away_speed==0.) {output=pressure;return true;}
    const double gamma=gamma_minus_one+1.;
    const double mach=away_speed/sound_speed;
    if(!arch::math::isfinite(gamma)||!arch::math::isfinite(mach)) return false;
    double candidate=0.;
    if(away_speed>0.) {
        const double decrement=.5*gamma_minus_one*mach;
        if(!arch::math::isfinite(decrement)) return false;
        const double base=1.-decrement;
        if(base<=0.) {output=0.;return true;}
        const double exponent=(2.*gamma)/gamma_minus_one;
        const double logarithm=std::log1p(-decrement);
        if(!arch::math::isfinite(exponent)||!arch::math::isfinite(logarithm)) return false;
        candidate=pressure*std::exp(exponent*logarithm);
        // Preserve the original ordinary-range expression and rounded value.
        // Only a nonvacuum direct-product zero attempts binary range recovery.
        if(candidate<=0.&&!detail::positive_exp_product(pressure,
            exponent*logarithm,candidate))return false;
    } else {
        const double K=.5*gamma*(gamma+1.)*mach*mach;
        const double B=gamma_minus_one/(gamma+1.);
        const double root_argument=K*(1.+B);
        if(!arch::math::isfinite(K)||!arch::math::isfinite(B)||!arch::math::isfinite(root_argument)) return false;
        candidate=pressure*(1.+.5*K+std::hypot(.5*K,std::sqrt(root_argument)));
    }
    // Only actual vacuum may publish zero; never floor an unrepresented
    // strictly positive rarefaction/compression pressure back into existence.
    if(!arch::math::isfinite(candidate)||!(candidate>0.)) return false;
    output=candidate;
    return true;
}

/** Construct the selected stationary-wall point law from actual EOS data.
 * Explicit gamma-law capability retains its exact rarefaction/shock pressure.
 * Otherwise P_wall=P(interior) defines the consistent physical-pressure
 * numerical wall F=(0,P_wall*n,0): no mass, energy or species crosses the wall.
 * This is neither an effective-gamma approximation nor a negative-stress clip.
 * At the same thermodynamic point its Euler entropy boundary residual is
 * u_n*(P_wall-P)/T=0 (Gibbs relation); this does not prove entropy stability
 * after reconstruction/limiting or a complete finite-volume stage.
 * See Hindenlang/Gassner/Kopriva, arXiv:1901.04924, eqs. 43-44 for the wall
 * flux form. Their ideal-gas DG analysis is not a general-EOS FV theorem.
 * Directions select physical momentum components; side chooses gamma-law
 * away speed. The output is atomic and may alias the already read input.
 */
template<class Eos>
ARCH_INLINE bool point_wall_flux(const FluidVector& point,const double* composition,
    const Eos& eos,int direction,int side,double pressure,double speed,FluidVector& output)
{
    if((direction<0||direction>2)||(side!=0&&side!=1)
        ||!detail::finite_point(point)||!(point.rho>0.)) return false;
    const double un=(direction==0?point.mom_u:direction==1?point.mom_v:point.mom_w)/point.rho;
    double wall_pressure=0.;
    if constexpr(requires {eos.roe_gamma_minus_one(composition);}) {
        if(!gamma_wall_pressure(eos.roe_gamma_minus_one(composition),pressure,speed,
            side==0?un:-un,wall_pressure)) return false;
    } else {
        if(!arch::math::isfinite(pressure)||!(pressure>0.)
            ||!arch::math::isfinite(speed)||!(speed>0.))return false;
        wall_pressure=pressure;
    }
    FluidVector candidate{};
    if(direction==0)candidate.mom_u=wall_pressure;
    else if(direction==1)candidate.mom_v=wall_pressure;else candidate.mom_w=wall_pressure;
    output=candidate;
    return true;
}

} // namespace StationarySlipWallFlux
