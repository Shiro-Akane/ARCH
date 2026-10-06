/**
 * @file RzCellPolynomial.h
 * @brief Conservative radial mean-to-point reconstruction for full-ring RZ with a
 *        native-baseline high-profile ray.
 *
 * Workflow:
 * 1. Read the three stage-local native radial means with their real signed cell
 *    bounds and the accepted native closure cell of the same grid index, ghost
 *    slot and stencil.
 * 2. Build the frozen high polynomial P: the V-measure quadratics of mr, mz and
 *    E, the W-measure cubic of m_phi=J/W, and the accepted native closure
 *    density polynomial rho* in place of a second density fit.
 * 3. Anchor the admissible ray at the accepted native baseline B of the same
 *    cell: P_theta(r)=B(r)+theta*(P(r)-B(r)). theta==0 returns B itself, so no
 *    0*NaN product is formed and no constant native mean is returned as a
 *    profile.
 * 4. Certify the six certified nodes of the cell (true lower face, true upper
 *    face and the four shared Gauss positions of the same actual cell interval)
 *    with the existing state::validate and the caller bounds: first the baseline
 *    B itself, then the largest admissible theta of the fixed dyadic ladder
 *    1,1/2,...,2^-54,0. The two faces are the real lower/upper bounds of the
 *    accepted native density cell and the Gauss positions use that same interval,
 *    because the FP64 grid endpoints need not equal origin-0.5*spacing or
 *    origin+0.5*spacing.
 *    Composition is certified on the same six nodes at each trial theta with the
 *    existing 512*species*epsilon simplex tolerance.
 * 5. limited_fraction evaluates rho*X_k on the same ray, using the exact
 *    original uncontracted density quadratic and the native neighbor fractions.
 *
 * Formulas, with t=(r-cell.origin)/cell.spacing, B=RzThermodynamics::base_point
 * and B'=RzThermodynamics::base_derivative at the same radius:
 *   at(r)         = B(r)+theta*(P(r)-B(r)); theta==0 -> B(r)
 *   derivative(r) = B'(r)+theta*(P'(r)-B'(r)); theta==0 -> B'(r)
 *   q_theta,k(r)  = rho*(r)*Xbar_k+theta*(q_old,k(r)-Xbar_k*rho_old(r))
 *   X_theta,k(r)  = Xbar_k+theta*(q_old,k(r)-Xbar_k*rho_old(r))/rho*(r)
 *   rho_old(r)    = V-measure quadratic of the native rho neighbors, uncontracted
 *   q_old,k(r)    = V-measure quadratic of the native rho*X_k neighbors
 *   nodes         = {baseline.density.lower, baseline.density.upper,
 *                    midpoint+half_width*Gauss 0..3}
 *   theta ladder  = 1, 2^-1, ..., 2^-54, then 0: 54 halvings, one common factor
 *                   for every field and every species.
 *
 * Domain and conservation assumptions:
 * - The admissible set in theta is not assumed convex: the caller energy ceiling
 *   internal_max makes the upper domain nonconvex, so this limiter uses a
 *   bounded dyadic descent, never a monotonic segment bisection, and claims no
 *   maximal theta.
 * - The supplied caller bounds are checked up front with the same finite,
 *   nonnegative-floor, max>=min rule the accepted native closure uses, because
 *   state::validate silently accepts NaN limits.
 * - The required native stencil on offsets [-1,2] must have all five conservative
 *   components finite and rho>0; a required-input failure is rejected at every
 *   theta and is never hidden by the theta=0 baseline. Only a legitimately
 *   nonrepresentable high fit built from all-finite input may fall back to the
 *   baseline.
 * - valid covers exactly the six certified nodes. Consumers must separately
 *   check every other query point, the actual EOS and the physical bounds.
 * - Baseline and high polynomial each keep their native means; theta only
 *   rescales deviations from the accepted native baseline. No mean is
 *   normalized, clipped or replaced by a new dimensional floor.
 * - sum_k q_theta,k(r)=rho*(r) pointwise for every theta while the native
 *   neighbor simplex holds, so the composition ray keeps sum_k X_k(r)=1.
 *
 * The polynomial is temporary numerical data, not a second conserved state.
 * Host/device callers provide their own readers: no allocation, no STL
 * container, no new physical constant, bound, EOS, heating term, floor or
 * clipping is introduced and no new evolved field is stored. This header stays
 * below the flux/configuration layer, so the configured PCM/MUSCL/PPM selection
 * and the consumer policy remain with the caller.
 */
#pragma once

#include <cmath>
#include <limits>

#include "core/ArchPortability.h"
#include "data/FluidState.h"
#include "grid/GridGeometryView.h"
#include "numerics/reconstruction/RzPolynomialMoments.h"
#include "numerics/state/RzNativeClosure.h"
#include "numerics/state/StateAdmissibility.h"

namespace RzReconstruction {
/** A reconstructed radial cell, including derivatives for the shared stress. */
struct Profile {
    Polynomial density,radial,axial,energy;
    Cubic angular;
    double origin=0.,spacing=0.;
    ARCH_INLINE FluidVector at(double radius) const {
        const double t=(radius-origin)/spacing;
        return {density.at(t),radial.at(t),axial.at(t),angular.at(t),energy.at(t)};
    }
    /** Physical d_r derivatives; density and velocity use the same profile. */
    ARCH_INLINE FluidVector derivative(double radius) const {
        const double t=(radius-origin)/spacing;
        return {(density.linear+density.quadratic*(2.*t))/spacing,
            (radial.linear+radial.quadratic*(2.*t))/spacing,
            (axial.linear+axial.quadratic*(2.*t))/spacing,
            angular.derivative(t)/spacing,(energy.linear+energy.quadratic*(2.*t))/spacing};
    }
};

/** Construct a reusable stage-local radial profile with conservative means. */
template<class StateReader>
ARCH_INLINE Profile radial_profile(const StateReader& read,int index,const RadialCell& cell)
{
    const auto low=read(index-1),middle=read(index),high=read(index+1),extra=read(index+2);
    return {field(low.rho,middle.rho,high.rho,cell),
        field(low.mom_u,middle.mom_u,high.mom_u,cell),
        field(low.mom_v,middle.mom_v,high.mom_v,cell),
        field(low.eng,middle.eng,high.eng,cell),
        angular_field(low.mom_w,middle.mom_w,high.mom_w,extra.mom_w,cell),
        cell.origin,cell.spacing};
}

/** Evaluate one point using the same conservative polynomial on both backends. */
template<class StateReader>
ARCH_INLINE FluidVector point_state(const StateReader& read,int index,
    const RadialCell& cell,double radius)
{
    return radial_profile(read,index,cell).at(radius);
}

/** Number of certified nodes of one cell: both true faces plus four Gauss. */
inline constexpr int certified_node_count=RzThermodynamics::physical_node_count;

/** Maximum number of halvings of the single trial factor before theta=0. */
inline constexpr int high_profile_halving_limit=54;

/** Radial position of one certified node of this same cell.
 * n=0/1 are the true lower/upper faces read from the accepted native density
 * cell itself (baseline.density.lower/upper), not from origin-0.5*spacing and
 * origin+0.5*spacing: the FP64 face endpoints of a real grid may differ from
 * that nominal identity. n=2..5 are the four shared Gauss positions of the same
 * actual cell interval, midpoint+half_width*quadrature_node(n-2), which integrate
 * a quadratic density ray exactly. The high polynomial keeps its cell.origin/
 * cell.spacing coordinate and is never refitted here, so this only selects the
 * radii that must be verified.
 */
ARCH_INLINE double certified_node_radius(int n,const RzThermodynamics::Cell& baseline)
{
    return RzThermodynamics::physical_node_radius(baseline,n);
}

/** One admissible conservative profile anchored at the native baseline.
 * theta scales deviations from the accepted native closure cell, never a mean.
 * valid covers the six certified nodes only; every consumer must still check
 * its own query points, the actual EOS and the physical bounds. The retained
 * native mean is caller-visible metadata; the ray itself is anchored at the
 * baseline point, whose density at t=0 is the native density polynomial value.
 * When valid is false the high polynomial has not been certified (it may be
 * left all zeros by an early rejection) and at/derivative must not be used.
 */
struct LimitedProfile {
    Profile polynomial;
    FluidVector mean;
    RzThermodynamics::Cell baseline;
    double theta=0.;
    bool valid=false;
    /** Explicit theta==0 branch: the true base point, never 0*NaN. */
    ARCH_INLINE FluidVector at(double radius) const {
        const FluidVector base=RzThermodynamics::base_point(baseline,radius);
        if(theta==0.)return base;
        return base+theta*(polynomial.at(radius)-base);
    }
    /** Derivative of the same limited ray; theta is fixed on this cell. */
    ARCH_INLINE FluidVector derivative(double radius) const {
        const FluidVector base=RzThermodynamics::base_derivative(baseline,radius);
        if(theta==0.)return base;
        return base+theta*(polynomial.derivative(radius)-base);
    }
};

/** Evaluate a rho*X profile on the same limited ray with the frozen arguments.
 * theta==0 returns the native central fraction X_k(index) directly, so no
 * 0*NaN or product/underflow path is taken when the ray is the baseline.
 * theta!=0 forms q_theta,k(r)=rho*(r)*Xbar_k+theta*(q_old,k(r)-Xbar_k*
 * rho_old(r)) over the point density rho*(r) that the caller read from the same
 * ray, written as Xbar_k+theta*(q_old,k(r)-Xbar_k*rho_old(r))/rho*(r) so the
 * large Xbar_k*rho*(r) product is never materialized. q_old,k is the
 * V-measure quadratic of the native rho*X_k neighbors and rho_old is the exact
 * original uncontracted V-measure quadratic of the native rho neighbors:
 * neither is contracted, normalized or clipped here.
 */
template<class StateReader,class FractionReader>
ARCH_INLINE double limited_fraction(const StateReader& read,const FractionReader& fraction,
    int index,int species,const RadialCell& cell,double radius,
    const LimitedProfile& profile,double density)
{
    const double central=fraction(species,index);
    if(profile.theta==0.)return central;
    const double t=(radius-cell.origin)/cell.spacing;
    const auto q_old=field(read(index-1).rho*fraction(species,index-1),
        read(index).rho*central,read(index+1).rho*fraction(species,index+1),cell);
    const auto rho_old=field(read(index-1).rho,read(index).rho,read(index+1).rho,cell);
    const double deviation=q_old.at(t)-central*rho_old.at(t);
    return central+profile.theta*(deviation/density);
}

/** Existing strict physical acceptance of one reconstructed point. */
ARCH_INLINE bool bounded_point_valid(const FluidVector& point,
    const arch::state::Bounds& bounds)
{
    return arch::state::validate(point,nullptr,0,1,bounds.density,bounds.internal_min,
        bounds.internal_max)==arch::state::Status::valid;
}

/** Explicit bound rule of the accepted native closure, applied to the caller
 * bounds handed to limited_profile. The bounds argument may differ from the one
 * the baseline was constructed with, and state::validate accepts NaN limits
 * silently, so the same finite, nonnegative density/floor and finite max>=min
 * rule used by the native closure is checked here up front. No physical control
 * is added: the caller's own numbers still decide the acceptance band.
 */
ARCH_INLINE bool bounds_valid(const arch::state::Bounds& bounds)
{
    return std::isfinite(bounds.density)&&bounds.density>=0.
        &&std::isfinite(bounds.internal_min)&&bounds.internal_min>=0.
        &&std::isfinite(bounds.internal_max)&&bounds.internal_max>=bounds.internal_min;
}

/** One native cell is admitted only if every conservative component is finite
 * and its density is strictly positive. A finite rho with a nonfinite m_r, m_z,
 * m_phi or E would otherwise build a corrupt high polynomial that the theta=0
 * baseline could silently hide.
 */
ARCH_INLINE bool native_cell_admissible(const FluidVector& native)
{
    return std::isfinite(native.rho)&&native.rho>0.
        &&std::isfinite(native.mom_u)&&std::isfinite(native.mom_v)
        &&std::isfinite(native.mom_w)&&std::isfinite(native.eng);
}

/** Native stencil precondition of the high profile on offsets [-1,2].
 * Every required native cell must have all five conservative components finite
 * with a strictly positive rho. With species>0, every neighbor fraction vector
 * must also be finite, nonnegative and simplex within the existing
 * 512*species*epsilon sum tolerance, so sum_k q_old,k(r) reproduces the native
 * density quadratic pointwise. Native means are never generically recovered or
 * repaired here: an invalid native input is rejected instead of hidden by a
 * recovery, and this rejection is applied before any high polynomial exists.
 */
template<class StateReader,class FractionReader>
ARCH_INLINE bool native_stencil_valid(const StateReader& read,const FractionReader& fraction,
    int index,int species)
{
    for(int offset=-1;offset<=2;++offset) {
        if(!native_cell_admissible(read(index+offset)))return false;
        if(species==0)continue;
        double sum=0.;
        for(int k=0;k<species;++k) {
            const double x=fraction(k,index+offset);
            if(!std::isfinite(x)||x<0.)return false;
            sum+=x;
        }
        if(std::abs(sum-1.)>512.*species*std::numeric_limits<double>::epsilon())
            return false;
    }
    return true;
}

/** Certify the native baseline B on the six certified nodes of this same cell.
 * The nodes are the real lower/upper faces of the accepted native density cell
 * (baseline.density.lower/upper) and the four Gauss positions of that same
 * interval; the finite, strictly increasing, non-axis-crossing support of that
 * interval was already checked by the existing density cell guard inside
 * baseline.valid(). B is a genuine point state, not an averaged mean: it must
 * pass the existing state::validate with the actual caller bounds at all six
 * nodes, because a mean-valid closure cell does not by itself imply a
 * point-resolved one.
 */
ARCH_INLINE bool baseline_nodes_valid(const RzThermodynamics::Cell& baseline,
    const arch::state::Bounds& bounds)
{
    for(int n=0;n<certified_node_count;++n)
        if(!bounded_point_valid(RzThermodynamics::base_point(baseline,
            certified_node_radius(n,baseline)),bounds))return false;
    return true;
}

/** Certify one trial ray on the same six nodes, without allocation.
 * Each node point must pass the existing bounds/recovery acceptance, and with
 * species>0 each species fraction of the same ray must be finite, nonnegative
 * and simplex within the existing 512*species*epsilon tolerance. A nonfinite
 * high polynomial fails the trial and pushes the ray back toward the baseline;
 * composition is never normalized or clipped.
 */
template<class StateReader,class FractionReader>
ARCH_INLINE bool ray_nodes_valid(const StateReader& read,const FractionReader& fraction,
    int index,int species,const RadialCell& cell,const RzThermodynamics::Cell& baseline,
    const arch::state::Bounds& bounds,const LimitedProfile& profile)
{
    for(int n=0;n<certified_node_count;++n) {
        const double radius=certified_node_radius(n,baseline);
        const auto point=profile.at(radius);
        if(!bounded_point_valid(point,bounds))return false;
        if(species==0)continue;
        double sum=0.;
        for(int k=0;k<species;++k) {
            const double x=limited_fraction(read,fraction,index,k,cell,radius,profile,point.rho);
            if(!std::isfinite(x)||x<0.)return false;
            sum+=x;
        }
        if(std::abs(sum-1.)>512.*species*std::numeric_limits<double>::epsilon())
            return false;
    }
    return true;
}

/** Restrict the native-baseline high-profile ray on the six certified nodes.
 * The accepted native closure cell must be valid and must belong to exactly
 * this reconstruction cell (same origin and spacing); a negative species count
 * and invalid caller bounds are rejected, and the native stencil on offsets
 * [-1,2] must have all five components finite with a positive rho and a valid
 * fraction simplex before any high polynomial is built. The baseline point
 * itself is certified first on the real lower/upper faces and the Gauss
 * positions of its own bounds: if any baseline node fails, valid stays false,
 * because a mean-valid closure does not resolve the point state. Otherwise the
 * frozen theta ladder tries one common factor for every field and species,
 * starting at 1 and halving at most 54 times, and falls back to theta=0, which
 * is the already certified native baseline ray. The upper domain is nonconvex
 * because the caller energy ceiling bounds internal energy from above, so this
 * is a bounded dyadic descent with no monotonic segment bisection and no claim
 * of a maximal theta. A required-input failure is never converted into the
 * theta=0 baseline; valid covers the six certified nodes only, and consumers
 * must check their own query points, the actual EOS and the physical bounds.
 */
template<class StateReader,class FractionReader>
ARCH_INLINE LimitedProfile limited_profile(const StateReader& read,
    const FractionReader& fraction,int index,int species,const RadialCell& cell,
    const RzThermodynamics::Cell& baseline,const arch::state::Bounds& bounds={})
{
    LimitedProfile result{};
    result.mean=read(index);
    result.baseline=baseline;
    if(species<0)return result;
    if(!bounds_valid(bounds))return result;
    if(baseline.density.origin!=cell.origin||baseline.density.spacing!=cell.spacing)
        return result;
    if(!baseline.valid()||!RzDensity::detail::cell_valid(baseline.density))return result;
    if(!native_stencil_valid(read,fraction,index,species))return result;
    result.polynomial=radial_profile(read,index,cell);
    result.polynomial.density=baseline.density.density;
    if(!baseline_nodes_valid(baseline,bounds))return result;
    for(int trial=0;trial<=high_profile_halving_limit;++trial) {
        result.theta=trial==0?1.:std::ldexp(1.,-trial);
        if(ray_nodes_valid(read,fraction,index,species,cell,baseline,bounds,result)) {
            result.valid=true;return result;
        }
    }
    result.theta=0.;
    result.valid=true;
    return result;
}

/** Reconstruct rho*X with V, then form its point mass fraction using point rho. */
template<class StateReader,class FractionReader>
ARCH_INLINE double point_fraction(const StateReader& read,const FractionReader& fraction,
    int index,int species,const RadialCell& cell,double radius,double density)
{
    const double left=read(index-1).rho*fraction(species,index-1);
    const double center=read(index).rho*fraction(species,index);
    const double right=read(index+1).rho*fraction(species,index+1);
    return field(left,center,right,cell).at((radius-cell.origin)/cell.spacing)/density;
}
} // namespace RzReconstruction
