/**
 * @file StationarySlipWallFlux.h
 * @brief Shared stationary impermeable/slip-wall gamma-law point flux.
 *
 * Workflow:
 * 1. Receive a true physical point and already queried pressure/sound speed.
 * 2. Require the explicit roe_gamma_minus_one EOS capability, never an
 *    effective gamma as a substitute for a gamma-law equation of state.
 * 3. Solve the accepted rarefaction invariant or shock jump unchanged.
 * 4. Return zero advective/energy flux and actual normal pressure traction.
 *
 * This leaf owns no wall identity, limiter, CFL, state publication or Runtime
 * qualification. Species advection is zero; its caller publishes those zeros
 * in the same accepted face bundle. A general-EOS exact wall solve remains open.
 */
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "amr/exchange/BoundaryPlan.h"
#include "core/ArchPortability.h"
#include "data/FluidState.h"
#include "numerics/flux/InvariantDomainFlux.h"

namespace StationarySlipWallFlux {
namespace detail {
/** Check every physical component without modifying or repairing the point. */
ARCH_INLINE bool finite_point(const FluidVector& point) {
    return std::isfinite(point.rho)&&std::isfinite(point.mom_u)&&std::isfinite(point.mom_v)
        &&std::isfinite(point.mom_w)&&std::isfinite(point.eng);
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
    if(!std::isfinite(pressure)||!(pressure>0.)||!std::isfinite(logarithm))return false;
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
    if(!std::isfinite(shift)||shift<minimum_shift||shift>maximum_shift
        ||shift<double(std::numeric_limits<int>::min())
        ||shift>double(std::numeric_limits<int>::max()))return false;
    const double combined_exponent=double(pressure_exponent)+shift;
    if(combined_exponent<double(std::numeric_limits<int>::min())
        ||combined_exponent>double(std::numeric_limits<int>::max()))return false;
    const double remainder=logarithm-shift*ln2;
    const double residual_product=mantissa*std::exp(remainder);
    if(!std::isfinite(remainder)||!std::isfinite(residual_product)
        ||!(residual_product>0.))return false;
    const double candidate=std::scalbn(residual_product,static_cast<int>(combined_exponent));
    if(!std::isfinite(candidate)||!(candidate>0.))return false;
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
    for(int s=0;s<species;++s)if(!std::isfinite(species_flux[s]))
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
    if(!std::isfinite(gamma_minus_one)||!(gamma_minus_one>0.)
        ||!std::isfinite(pressure)||!(pressure>0.)
        ||!std::isfinite(sound_speed)||!(sound_speed>0.)
        ||!std::isfinite(away_speed)) return false;
    if(away_speed==0.) {output=pressure;return true;}
    const double gamma=gamma_minus_one+1.;
    const double mach=away_speed/sound_speed;
    if(!std::isfinite(gamma)||!std::isfinite(mach)) return false;
    double candidate=0.;
    if(away_speed>0.) {
        const double decrement=.5*gamma_minus_one*mach;
        if(!std::isfinite(decrement)) return false;
        const double base=1.-decrement;
        if(base<=0.) {output=0.;return true;}
        const double exponent=(2.*gamma)/gamma_minus_one;
        const double logarithm=std::log1p(-decrement);
        if(!std::isfinite(exponent)||!std::isfinite(logarithm)) return false;
        candidate=pressure*std::exp(exponent*logarithm);
        // Preserve the original ordinary-range expression and rounded value.
        // Only a nonvacuum direct-product zero attempts binary range recovery.
        if(candidate<=0.&&!detail::positive_exp_product(pressure,
            exponent*logarithm,candidate))return false;
    } else {
        const double K=.5*gamma*(gamma+1.)*mach*mach;
        const double B=gamma_minus_one/(gamma+1.);
        const double root_argument=K*(1.+B);
        if(!std::isfinite(K)||!std::isfinite(B)||!std::isfinite(root_argument)) return false;
        candidate=pressure*(1.+.5*K+std::hypot(.5*K,std::sqrt(root_argument)));
    }
    // Only actual vacuum may publish zero; never floor an unrepresented
    // strictly positive rarefaction/compression pressure back into existence.
    if(!std::isfinite(candidate)||!(candidate>0.)) return false;
    output=candidate;
    return true;
}

/** Consume actual already queried point acoustics for a gamma-law wall flux.
 * This helper is instantiated only through roe_gamma_minus_one capability;
 * get_gamma/effective gamma alone cannot authorize a gamma-law solution.
 * All stationary-wall advective, total-energy and species fluxes are zero;
 * only the true normal pressure traction is published, atomically. Directions
 * 0/1/2 select mom_u/mom_v/mom_w in the caller's actual orthonormal chart;
 * these are point components, not mixed native cell means. Lower/upper sides
 * select away speed +un/-un. Output may alias the input: all reads finish first.
 */
template<class Eos>
ARCH_INLINE bool gamma_wall_flux(const FluidVector& point,const double* composition,
    const Eos& eos,int direction,int side,double pressure,double speed,FluidVector& output)
{
    if((direction<0||direction>2)||(side!=0&&side!=1)
        ||!detail::finite_point(point)||!(point.rho>0.)) return false;
    const double un=(direction==0?point.mom_u:direction==1?point.mom_v:point.mom_w)/point.rho;
    double wall_pressure=0.;
    if(!gamma_wall_pressure(eos.roe_gamma_minus_one(composition),pressure,speed,
        side==0?un:-un,wall_pressure)) return false;
    FluidVector candidate{};
    if(direction==0)candidate.mom_u=wall_pressure;
    else if(direction==1)candidate.mom_v=wall_pressure;else candidate.mom_w=wall_pressure;
    output=candidate;
    return true;
}

/** Bind a selected gamma-law wall low to the original joint point factor.
 * Workflow: validate required metadata/acoustics/composition pointers; select
 * the actual lower-right or upper-left immutable interior; construct its exact
 * canonical wall traction; initialize caller-owned zero species baseline;
 * invoke the sole original selected-baseline limiter without altering a/CFL.
 * B_L=U_L+(F_L-L)/a and B_R=U_R+(L-F_R)/a must BOTH be admissible before
 * any high correction is accepted. The returned factor always refers to this
 * same L; an LLF low/bar factor cannot be borrowed for a different wall flux.
 * Caller retains original required EOS queries, gamma-law capability dispatch,
 * high-at-rest policy and candidate_eos semantics. Nonfinite optional high is
 * allowed to contract to theta=0; required failures return valid=false.
 * Scratch is S unpublished baseline species entries, no allocation or copies.
 * Its actual half-open array must be disjoint from XiL/XiR/highSpecies; checked
 * count/address arithmetic rejects overlap or unrepresentable ranges before writes.
 * Input Xi retains the existing signed trace cone: no normalization, clipping
 * or additional simplex tolerance is introduced. No Runtime authority follows.
 */
template<class Eos>
ARCH_INLINE FluxAdmissibility::PointFaceBlend gamma_selected_point_blend(
    const FluidVector& base_left,const FluidVector& base_right,
    const double* x_left,const double* x_right,int species,
    double p_left,double c_left,double p_right,double c_right,
    const Eos& eos,int direction,int side,const FluidVector& high,
    const double* species_high,double* species_low)
{
    FluxAdmissibility::PointFaceBlend invalid;
    if(direction<0||direction>2||(side!=0&&side!=1)||species<0
        ||(species&&(!x_left||!x_right||!species_high||!species_low))
        ||!detail::finite_point(base_left)||!detail::finite_point(base_right)
        ||!(base_left.rho>0.)||!(base_right.rho>0.)
        ||!std::isfinite(p_left)||!(p_left>0.)||!std::isfinite(c_left)||!(c_left>0.)
        ||!std::isfinite(p_right)||!(p_right>0.)||!std::isfinite(c_right)||!(c_right>0.))
        return invalid;
    if(!detail::species_scratch_disjoint(species,species_low,x_left,x_right,species_high))
        return invalid;
    for(int s=0;s<species;++s)
        if(!std::isfinite(x_left[s])||!std::isfinite(x_right[s]))return invalid;
    // This workspace remains provisional even if the two baseline bars reject.
    for(int s=0;s<species;++s)species_low[s]=0.;
    FluidVector canonical_low{};
    const auto& interior=side==0?base_right:base_left;
    const double* xi=side==0?x_right:x_left;
    if(!gamma_wall_flux(interior,xi,eos,direction,side,
        side==0?p_right:p_left,side==0?c_right:c_left,canonical_low))return invalid;
    return FluxAdmissibility::point_face_blend_with_baseline_and_thermo(
        base_left,base_right,x_left,x_right,species,p_left,c_left,p_right,c_right,
        direction,high,species_high,canonical_low,species_low);
}

} // namespace StationarySlipWallFlux
