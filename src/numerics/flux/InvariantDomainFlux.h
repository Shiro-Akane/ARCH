/**
 * @file InvariantDomainFlux.h
 * @brief Shared point admissibility and conservative face assembly.
 *
 * Workflow:
 * 1. Consume the selected reconstruction and actual required endpoint EOS data.
 * 2. Evaluate the configured point flux and the one shared bar/segment factor;
 *    selected walls lend their physical reflection/pressure law to this owner.
 * 3. Every retained chart consumes one physical point per face; the same
 *    conservative blend is returned for fluid and species.
 * 4. Return one complete fluid/species face; traversal, measures, AMR
 *    registration and Runtime state/EOS publication remain with their owners.
 *
 * Canonical required and directional Riemann EOS inputs retain their roles;
 * completed density/EOS acceptance remains mandatory.
 */

#pragma once

#include "core/MathClassification.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "numerics/curvilinear/GridGeometryView.h"
#include "physics/boundary/BoundaryFlux.h"
#include "numerics/flux/FluxFunctions.h"
#include "numerics/flux/StationarySlipWallFlux.h"
#include "numerics/reconstruction/Reconstruction.h"

struct IdealGas;

// Conservative convex limiting against a local Lax-Friedrichs bar state.
// One theta modifies the shared face flux for BOTH neighbouring cells and all
// species, before flux registration. Under dt/V * sum(A*a) <= 1 the Cartesian
// update is a convex combination of its mean and these admissible bar states.
// This is a sufficient Euler invariant-domain condition, not a table-EOS theorem.
// Density/internal energy use the strict domain. Composition uses the same
// bounded trace cone as stage acceptance; accepted negative traces require a
// recorded mass correction before any state is published.
namespace FluxAdmissibility {
// A high-order face is a trial state. The final conservative limiter always
// queries the unchanged owning cell means through the required EOS contract.

// Host tabular EOS raises on an out-of-table high-order trial. Convert only
// that documented physics failure to a rejected candidate; leave every other
// exception and all required mean-state queries untouched.
template <class Compute>
ARCH_INLINE void compute_candidate(Compute&& compute, FluidVector& high,
                              double* species_flux, int species)
{
#if defined(__CUDA_ARCH__)
    // Device trial EOS returns nonfinite values without a required-state latch;
    // the unchanged point factor rejects them. Device code has no exceptions.
    compute();
#else
    try {
        compute();
    } catch (const std::runtime_error&) {
        high = FluidVector(arch::state::invalid(), 0.0, 0.0, 0.0,
                           arch::state::invalid());
        for (int s = 0; s < species; ++s)
            species_flux[s] = arch::state::invalid();
    }
#endif
}

/** Test the recovered conserved state against the shared admissible domain. */
ARCH_INLINE bool valid(const FluidVector& state)
{ return arch::state::recover(state).status == arch::state::Status::valid; }

namespace point_face_detail {
/** The same segment owner has already rejected its full trial and accepted
 * its immutable mean. Only the original bisection remains to be performed. */
ARCH_INLINE double bisect_valid_mean(const FluidVector& mean, const FluidVector& offset)
{
    double lo = 0.0, hi = 1.0;
    for (int iteration = 0; iteration < 54; ++iteration) {
        const double mid = lo + 0.5 * (hi - lo);
        if (valid(mean + mid * offset)) lo = mid; else hi = mid;
    }
    return lo * (1.0 - 16.0 * std::numeric_limits<double>::epsilon());
}
/** A point-face owner may consume its own already accepted bar state. New
 * correction states still take the original full-trial/bisection checks. */
ARCH_INLINE double segment_from_valid_mean(const FluidVector& mean, const FluidVector& offset)
{
    if (valid(mean + offset)) return 1.0;
    return bisect_valid_mean(mean, offset);
}
} // namespace point_face_detail

/** Bisect the largest admissible fraction along a conserved-state segment. */
ARCH_INLINE double segment_fraction(const FluidVector& mean, const FluidVector& offset)
{
    if (valid(mean + offset)) return 1.0;
    if (!valid(mean)) return 0.0;
    return point_face_detail::bisect_valid_mean(mean, offset);
}

// Reconstructed states are limited along the ray from their conserved mean.
// The cell average itself is never replaced, so an invalid mean still fails.
/** Limit one reconstructed face on a ray from its unchanged cell mean. */
ARCH_INLINE void limit_reconstruction(const FluidVector& mean, FluidVector& face)
{
    if (!valid(face)) {
        const auto offset = face - mean;
        const double theta = segment_fraction(mean, offset);
        face = theta == 0.0 ? mean : mean + theta * offset;
    }
}

/** Recover required mean thermodynamics from the caller's ordinary EOS input:
 * the actual cell mean, without replacing the evolved conserved state.
 */
template<class Eos>
ARCH_INLINE void required_mean_thermo(const FluidVector& mean,
    const double* composition, const Eos& eos, double& pressure, double& speed)
{
    if constexpr (requires {
        eos.get_pressure_and_sound_speed(
            mean.rho, 0.0, composition, pressure, speed);
    }) {
        // The caller fixes the mean's geometry/energy interpretation. Helm's
        // grouped path preserves the strict inversion and acoustic derivatives,
        // while avoiding a second inverse for the same (rho, e, X) state.
        calc_endpoint_thermo(mean, arch::state::recover(mean).internal,
                             composition, eos, pressure, speed);
    } else {
        pressure = eos.get_pressure(mean, composition);
        speed = eos.get_sound_speed(mean, pressure, composition);
    }
}

/** Borrow already validated mean thermodynamics from one immutable stage.
 * Host and device owners provide the same SoA layout; no allocation or EOS
 * approximation lives here. A null ready array means the launch owner has
 * completed all means used by this face. Input lifetime ends with the stage.
 */
struct MeanThermoView {
    const double *rho = nullptr, *mom_u = nullptr, *mom_v = nullptr;
    const double *mom_w = nullptr, *eng = nullptr, *mass_fractions = nullptr;
    const double *pressure = nullptr, *sound_speed = nullptr;
    const unsigned char* ready = nullptr;
    int cells = 0, species = 0;
    bool roe_wave_speed = true;

    /** Apply the original complete conserved-state/composition equality test. */
    ARCH_INLINE bool matches(int cell, const FluidVector& state,
                             const double* composition, int count) const {
        if (cell < 0 || cell >= cells || count != species || !pressure
            || !sound_speed || (ready && !ready[cell])) return false;
        if (rho[cell] != state.rho || mom_u[cell] != state.mom_u
            || mom_v[cell] != state.mom_v || mom_w[cell] != state.mom_w
            || eng[cell] != state.eng) return false;
        for (int s = 0; s < count; ++s)
            if (mass_fractions[s * cells + cell] != composition[s]) return false;
        return true;
    }

    /** Reuse an identical ordinary mean for one face endpoint query. */
    ARCH_INLINE bool query(const FluidVector& state, const double* composition,
                           int count, int left, int right, double& p, double& c) const {
        const int cell = matches(left,state,composition,count) ? left
            : matches(right,state,composition,count) ? right : -1;
        if (cell < 0) return false;
        p = pressure[cell]; c = sound_speed[cell];
        return true;
    }
};

/** Recover physical point EOS; only identical ordinary means permit cache reuse. */
template<class Eos>
ARCH_INLINE void face_thermo(const FluidVector& state, double energy,
    const double* composition, int count, const Eos& eos,
    const MeanThermoView* means, int cell, double& pressure, double& sound)
{
    if constexpr (requires {
        eos.get_pressure_and_sound_speed(state.rho,energy,composition,pressure,sound);
    }) {
        // Ordinary means use recover(state).internal. Directional primitive
        // arithmetic can round an equal conserved state to a different e;
        // reuse only the same (rho,e,X) query, not merely the same U and X.
        if (means && means->query(state,composition,count,cell,cell,pressure,sound)
            && energy == arch::state::recover(state).internal) return;
    }
    calc_endpoint_thermo(state,energy,composition,eos,pressure,sound);
}

// One host patch-stage owns this cache. A value is published only after the
// complete EOS query succeeds; the caller resets validity on every RK stage.
struct MeanThermoCache {
    bool roe_wave_speed = true;
    arch::boundary::HydroBoundaryView hydro_boundary{}; // Scoped mathematical view; owner authenticates it.
    std::vector<double> pressure;
    std::vector<double> sound_speed;
    std::vector<unsigned char> ready;

    void reset(int cells) {
        hydro_boundary={}; // Never carry wall authority into another patch/stage.
        pressure.resize(cells);
        sound_speed.resize(cells);
        ready.assign(cells, 0);
    }
};

/** Physical point LLF baseline and the unchanged joint face factor.
 * valid certifies only the original wave/bar prerequisite, not an EOS table
 * domain or scheduler publication.
 */
struct PointFaceBlend {
    FluidVector low{};
    double wave_speed=0.;
    double theta=0.;
    bool valid=false;
};

namespace point_face_detail {
/** Stack-only original arithmetic retained for the ordinary final species loop.
 * It avoids recomputing a/F_L/F_R or adding hidden fields to the public result.
 */
struct Arithmetic { double a=0.;FluidVector fl{},fr{}; };

/** Sole factor owner for public point inspection and the ordinary limiter.
 * Workflow: original a and physical fluxes -> LLF low/bar -> segment factors
 * -> original shifted species cone. No caller output is mutated here.
 * low=(F_L+F_R)/2-a*(U_R-U_L)/2;
 * bar=(U_L+U_R)/2-(F_R-F_L)/(2a).
 */
ARCH_INLINE PointFaceBlend evaluate(const FluidVector& left,const FluidVector& right,
    const double* x_left,const double* x_right,int species,
    double p_left,double c_left,double p_right,double c_right,
    int direction,const FluidVector& high,const double* species_high,
    Arithmetic& arithmetic)
{
    PointFaceBlend result;
    const double a = std::max(std::abs(get_un(left, direction)) + c_left,
                              std::abs(get_un(right, direction)) + c_right);
    const auto fl = get_flux(left, p_left, direction);
    const auto fr = get_flux(right, p_right, direction);
    arithmetic.a=a;arithmetic.fl=fl;arithmetic.fr=fr;
    result.wave_speed=a;
    if (!(a > 0.0) || !arch::math::isfinite(a)) return result;
    // Local Lax-Friedrichs: F_low=(F_L+F_R)/2-a*(U_R-U_L)/2.
    const auto low = 0.5 * fl + 0.5 * fr - (0.5 * a) * (right - left);
    // Invariant-domain bar state: U_bar=(U_L+U_R)/2-(F_R-F_L)/(2a).
    const auto bar = 0.5 * left + 0.5 * right - (0.5 / a) * (fr - fl);
    result.low=low;
    if (!valid(bar)) return result;
    const auto correction = (low - high) / a;
    double theta = std::min(segment_from_valid_mean(bar, correction), segment_from_valid_mean(bar, -1.0 * correction));
    for (int s = 0; s < species; ++s) {
        const double ql = left.rho * x_left[s], qr = right.rho * x_right[s];
        const double f_l = fl.rho * x_left[s], f_r = fr.rho * x_right[s];
        const double low_species = 0.5 * f_l + 0.5 * f_r - 0.5 * a * (qr - ql);
        const double bar_species = 0.5 * ql + 0.5 * qr - (0.5 / a) * (f_r - f_l);
        // Use the same bounded trace cone as conservative-stage acceptance:
        // q_s + tau*rho >= 0, tau = composition_roundoff_limit. For either
        // bar state the shifted correction is dq_s + tau*drho, so this bound
        // controls both signs without a zero-trace 0/0 switch that can turn
        // an arbitrarily small species perturbation into a full fluid-flux
        // fallback. Density/energy remain strictly admissible above; any
        // accepted negative trace is still repaired with its stage receipt.
        const double tau = arch::state::composition_roundoff_limit;
        const double shifted_bar = bar_species + tau * bar.rho;
        const double shifted_deviation = std::abs(
            (low_species - species_high[s]) / a + tau * correction.rho);
        if (!arch::math::isfinite(shifted_deviation) || !arch::math::isfinite(shifted_bar))
            theta = 0.0;
        else if (shifted_deviation > shifted_bar)
            theta = std::min(theta, std::max(0.0, shifted_bar) / shifted_deviation);
    }
    result.theta=theta;result.valid=true;return result;
}
} // namespace point_face_detail

/** Return the original physical point limiter factor without blending a flux.
 * Native radial integration can take a minimum of these four node factors and
 * blend all low/high components once. Caller supplies actual point EOS inputs;
 * this entry never upgrades that point lemma to a native stage guarantee.
 * Existing species/pointer/direction preconditions remain caller-owned.
 */
ARCH_INLINE PointFaceBlend point_face_blend_with_thermo(
    const FluidVector& left,const FluidVector& right,
    const double* x_left,const double* x_right,int species,
    double p_left,double c_left,double p_right,double c_right,
    int direction,const FluidVector& high,const double* species_high)
{
    point_face_detail::Arithmetic arithmetic;
    return point_face_detail::evaluate(left,right,x_left,x_right,species,
        p_left,c_left,p_right,c_right,direction,high,species_high,arithmetic);
}

/** Limit a high face flux against an explicitly selected physical baseline.
 * Workflow: use the existing endpoint acoustic speed a; build the two
 * distinct baseline states; reject an inadmissible prerequisite; limit the
 * high-minus-baseline correction with one fluid/species fraction.
 * B_L=U_L+(F_L-L)/a, B_R=U_R+(L-F_R)/a;
 * B_L(theta)=B_L+theta*(L-H)/a, B_R(theta)=B_R-theta*(L-H)/a.
 * An arbitrary baseline cannot borrow the LLF bar or its fraction. This
 * point algebra supports a Cartesian convex update only with the owning
 * sum(dt*A*a/V)<=1 and closed uniform physical flux geometry; it grants
 * no mixed-measure native stage, geometric source or general-EOS theorem.
 * The ordinary LLF entry retains its original arithmetic above. Caller owns
 * species storage and supplies the same selected baseline for every field.
 */
ARCH_INLINE PointFaceBlend point_face_blend_with_baseline_and_thermo(
    const FluidVector& left,const FluidVector& right,
    const double* x_left,const double* x_right,int species,
    double p_left,double c_left,double p_right,double c_right,
    int direction,const FluidVector& high,const double* species_high,
    const FluidVector& low,const double* species_low)
{
    PointFaceBlend result;
    const double a=std::max(std::abs(get_un(left,direction))+c_left,
                            std::abs(get_un(right,direction))+c_right);
    result.wave_speed=a;result.low=low;
    if(!(a>0.)||!arch::math::isfinite(a))return result;
    const auto fl=get_flux(left,p_left,direction);
    const auto fr=get_flux(right,p_right,direction);
    const auto bar_left=left+(fl-low)/a;
    const auto bar_right=right+(low-fr)/a;
    if(!valid(bar_left)||!valid(bar_right))return result;
    const auto correction=(low-high)/a;
    double theta=std::min(point_face_detail::segment_from_valid_mean(bar_left,correction),
                          point_face_detail::segment_from_valid_mean(bar_right,-1.*correction));
    for(int s=0;s<species;++s) {
        if(!arch::math::isfinite(species_low[s]))return result;
        const double bar_species_left=left.rho*x_left[s]
            +(fl.rho*x_left[s]-species_low[s])/a;
        const double bar_species_right=right.rho*x_right[s]
            +(species_low[s]-fr.rho*x_right[s])/a;
        const double tau=arch::state::composition_roundoff_limit;
        const double shifted_left=bar_species_left+tau*bar_left.rho;
        const double shifted_right=bar_species_right+tau*bar_right.rho;
        if(!arch::math::isfinite(shifted_left)||!arch::math::isfinite(shifted_right)
            ||shifted_left<0.||shifted_right<0.)return result;
        const double deviation=(species_low[s]-species_high[s])/a
                              +tau*correction.rho;
        if(!arch::math::isfinite(deviation))theta=0.;
        else if(deviation<0.)theta=std::min(theta,shifted_left/(-deviation));
        else if(deviation>0.)theta=std::min(theta,shifted_right/deviation);
    }
    result.theta=theta;result.valid=true;return result;
}

/** Blend a face flux using the sole original shared factor owner.
 * Keep original final fluid/species expression order and rejection bytes.
 * No required mean EOS query, trace window or segment iteration is changed.
 */
ARCH_INLINE void limit_face_with_thermo(const FluidVector& left, const FluidVector& right,
    const double* x_left, const double* x_right, int species,
    double p_left, double c_left, double p_right, double c_right,
    int direction, FluidVector& high, double* species_flux)
{
    point_face_detail::Arithmetic arithmetic;
    const auto factor=point_face_detail::evaluate(left,right,x_left,x_right,species,
        p_left,c_left,p_right,c_right,direction,high,species_flux,arithmetic);
    if (!factor.valid) {
        high = FluidVector(arch::state::invalid(), 0.0, 0.0, 0.0, arch::state::invalid());
        return;
    }
    const double a=arithmetic.a;
    const auto fl=arithmetic.fl,fr=arithmetic.fr;
    const auto low=factor.low;
    const double theta=factor.theta;
    if (theta >= 1.0) return;
    high = theta == 0.0 ? low : low + theta * (high - low);
    for (int s = 0; s < species; ++s) {
        const double low_species = 0.5 * fl.rho * x_left[s] + 0.5 * fr.rho * x_right[s]
            - 0.5 * a * (right.rho * x_right[s] - left.rho * x_left[s]);
        species_flux[s] = theta == 0.0 ? low_species : low_species + theta * (species_flux[s] - low_species);
    }
}

/** Blend the high-order flux with Lax-Friedrichs using one conservative face theta. */
template<class Eos>
ARCH_INLINE void limit_face(const FluidVector& left, const FluidVector& right,
    const double* x_left, const double* x_right, int species, const Eos& eos,
    int direction, FluidVector& high, double* species_flux)
{
    double p_left, p_right, c_left, c_right;
    required_mean_thermo(left, x_left, eos, p_left, c_left);
    required_mean_thermo(right, x_right, eos, p_right, c_right);
    limit_face_with_thermo(left, right, x_left, x_right, species,
                           p_left, c_left, p_right, c_right, direction,
                           high, species_flux);
}

/** Bind the selected stationary-wall low to the original joint point factor.
 * Workflow: validate required metadata/acoustics/composition pointers; select
 * the actual lower-right or upper-left immutable interior; construct its exact
 * canonical wall traction (exact gamma-law or actual general-EOS pressure);
 * initialize caller-owned zero species baseline;
 * invoke the sole original selected-baseline limiter without altering a/CFL.
 * B_L=U_L+(F_L-L)/a and B_R=U_R+(L-F_R)/a must BOTH be admissible before
 * any high correction is accepted. The returned factor always refers to this
 * same L; an LLF low/bar factor cannot be borrowed for a different wall flux.
 * Caller retains required EOS queries, explicit EOS-capability dispatch,
 * high-at-rest policy and candidate_eos semantics. Nonfinite optional high is
 * allowed to contract to theta=0; required failures return valid=false.
 * Scratch is S unpublished baseline species entries, no allocation or copies.
 * Its actual half-open array must be disjoint from XiL/XiR/highSpecies; checked
 * count/address arithmetic rejects overlap or unrepresentable ranges before writes.
 * Input Xi retains the existing signed trace cone: no normalization, clipping
 * or additional simplex tolerance is introduced. No Runtime authority follows.
 */
template<class Eos>
ARCH_INLINE FluxAdmissibility::PointFaceBlend selected_wall_point_blend(
    const FluidVector& base_left,const FluidVector& base_right,
    const double* x_left,const double* x_right,int species,
    double p_left,double c_left,double p_right,double c_right,
    const Eos& eos,int direction,int side,const FluidVector& high,
    const double* species_high,double* species_low)
{
    FluxAdmissibility::PointFaceBlend invalid;
    if(direction<0||direction>2||(side!=0&&side!=1)||species<0
        ||(species&&(!x_left||!x_right||!species_high||!species_low))
        ||!StationarySlipWallFlux::detail::finite_point(base_left)||!StationarySlipWallFlux::detail::finite_point(base_right)
        ||!(base_left.rho>0.)||!(base_right.rho>0.)
        ||!arch::math::isfinite(p_left)||!(p_left>0.)||!arch::math::isfinite(c_left)||!(c_left>0.)
        ||!arch::math::isfinite(p_right)||!(p_right>0.)||!arch::math::isfinite(c_right)||!(c_right>0.))
        return invalid;
    if(!StationarySlipWallFlux::detail::species_scratch_disjoint(species,species_low,x_left,x_right,species_high))
        return invalid;
    for(int s=0;s<species;++s)
        if(!arch::math::isfinite(x_left[s])||!arch::math::isfinite(x_right[s]))return invalid;
    // This workspace remains provisional even if the two baseline bars reject.
    for(int s=0;s<species;++s)species_low[s]=0.;
    FluidVector canonical_low{};
    const auto& interior=side==0?base_right:base_left;
    const double* xi=side==0?x_right:x_left;
    if(!StationarySlipWallFlux::point_wall_flux(interior,xi,eos,direction,side,
        side==0?p_right:p_left,side==0?c_right:c_left,canonical_low))return invalid;
    return FluxAdmissibility::point_face_blend_with_baseline_and_thermo(
        base_left,base_right,x_left,x_right,species,p_left,c_left,p_right,c_right,
        direction,high,species_high,canonical_low,species_low);
}

/** Point-only result: Host owns exception translation and publication. */
enum class PointWallStatus : unsigned char {
    valid,invalid_metadata,invalid_high_traction,inadmissible_baseline,
    invalid_pressure_traction,nonfinite_flux,nonfinite_species
};

/** Assemble one stationary ordinary point wall with the existing selected law.
 * Workflow: mirror actual interior H and immutable B independently; dispatch
 * explicit gamma-law moving walls or the general-EOS physical-pressure law;
 * evaluate optional H through the actual candidate EOS/selected policy; canonicalize FINITE H before its factor; then
 * replace both Xi arrays with true B composition read in original species order.
 * Required P/c are the actual already prepared interior mean cache values.
 * Selected wall laws use the sole selected-baseline factor; gamma-law at rest
 * retains its original mirrored-policy LLF low and original bar/factor. Apply
 * the same theta to all fluid/species and publish fluid ONLY after every check.
 * No post-factor projection, effective-gamma substitute or second limiter.
 *
 * BaseSpecies(s) reads immutable true interior fractions AFTER high computation.
 * HighCompute(left,right,XiL,XiR,trialEOS,H,highSpecies) owns the selected policy
 * and any exact caller cache indices/coefficient, never Runtime wall authority.
 * Four caller-owned S planes XiL/XiR/Xi_cell/highSpecies remain unpublished
 * scratch, with actual disjoint extents; zero species permits null pointers.
 * Input points are value copies for alias-safe mirroring. Fluid output can alias
 * an input; it is assigned only after full success. Scratch may change on failure.
 * This shared Host/device leaf owns no Grid, storage, boundary identity or EOS
 * qualification. The caller retains actual required EOS preparation and guards.
 */
template<class Eos,class BaseSpecies,class HighCompute>
ARCH_INLINE PointWallStatus assemble_wall_point(FluidVector base_left,FluidVector base_right,
    FluidVector U_L,FluidVector U_R,int dir,int wall_side,int n_spec,
    double pressure,double speed,const Eos& eos,double* Xi_L,double* Xi_R,
    double* Xi_cell,double* face_species_flux,const BaseSpecies& base_species,
    const HighCompute& high_compute,FluidVector& output)
{
    if(dir<0||dir>2||(wall_side!=0&&wall_side!=1)||n_spec<0
        ||(n_spec&&(!Xi_L||!Xi_R||!Xi_cell||!face_species_flux)))
        return PointWallStatus::invalid_metadata;
    if(wall_side==0) {
        U_L=StationarySlipWallFlux::reflected_point(U_R,dir);
        base_left=StationarySlipWallFlux::reflected_point(base_right,dir);
        for(int s=0;s<n_spec;++s)Xi_L[s]=Xi_R[s];
    } else {
        U_R=StationarySlipWallFlux::reflected_point(U_L,dir);
        base_right=StationarySlipWallFlux::reflected_point(base_left,dir);
        for(int s=0;s<n_spec;++s)Xi_R[s]=Xi_L[s];
    }
    bool gamma_wall=false;
    if constexpr(requires {eos.roe_gamma_minus_one(Xi_L);}) {
        const auto& h=wall_side==0?U_R:U_L;
        const auto& base=wall_side==0?base_right:base_left;
        gamma_wall=(dir==0?h.mom_u:dir==1?h.mom_v:h.mom_w)!=0.
            ||(dir==0?base.mom_u:dir==1?base.mom_v:base.mom_w)!=0.;
    }
    constexpr bool pressure_wall=!requires {eos.roe_gamma_minus_one(Xi_L);};
    FluidVector high;
    const auto& trial_eos=arch::state::candidate_eos(eos);
    FluxAdmissibility::compute_candidate([&] {
        if(gamma_wall||pressure_wall) {
            const auto& interior=wall_side==0?U_R:U_L;
            const double* xi=wall_side==0?Xi_R:Xi_L;
            double pressure=0.,speed=0.;
            FluxAdmissibility::required_mean_thermo(interior,xi,trial_eos,pressure,speed);
            if(StationarySlipWallFlux::point_wall_flux(interior,xi,trial_eos,
                dir,wall_side,pressure,speed,high)) {
                for(int s=0;s<n_spec;++s)face_species_flux[s]=0.;
            } else {
                high=FluidVector(arch::state::invalid(),0.,0.,0.,arch::state::invalid());
                for(int s=0;s<n_spec;++s)face_species_flux[s]=arch::state::invalid();
            }
            return;
        }
        high_compute(U_L,U_R,Xi_L,Xi_R,trial_eos,high,face_species_flux);
    },high,face_species_flux,n_spec);
    if(StationarySlipWallFlux::stationary_candidate(
        dir,high,face_species_flux,n_spec)
            ==StationarySlipWallFlux::StationaryCandidateStatus::invalid)
        return PointWallStatus::invalid_high_traction;
    for(int s=0;s<n_spec;++s)Xi_L[s]=base_species(s);
    for(int s=0;s<n_spec;++s)Xi_R[s]=Xi_L[s];
    FluxAdmissibility::PointFaceBlend factor;
    if(gamma_wall||pressure_wall) {
        factor=FluxAdmissibility::selected_wall_point_blend(
                base_left,base_right,Xi_L,Xi_R,n_spec,
                pressure,speed,pressure,speed,eos,dir,wall_side,
                high,face_species_flux,Xi_cell);
    } else {
        factor=FluxAdmissibility::point_face_blend_with_thermo(
            base_left,base_right,Xi_L,Xi_R,n_spec,
            pressure,speed,pressure,speed,dir,high,face_species_flux);
    }
    if(!factor.valid||!StationarySlipWallFlux::finite_flux(factor.low)
        ||!arch::math::isfinite(factor.theta)||factor.theta<0.||factor.theta>1.)
        return PointWallStatus::inadmissible_baseline;
    const double hp=dir==0?high.mom_u:dir==1?high.mom_v:high.mom_w;
    const double lp=dir==0?factor.low.mom_u:dir==1?factor.low.mom_v:factor.low.mom_w;
    if(hp<0.||(!arch::math::isfinite(hp)&&factor.theta!=0.)||!arch::math::isfinite(lp)||lp<0.)
        return PointWallStatus::invalid_pressure_traction;
    const auto blended=factor.theta==0.?factor.low:factor.theta==1.?high:
        factor.low+factor.theta*(high-factor.low);
    if(!StationarySlipWallFlux::finite_flux(blended))
        return PointWallStatus::nonfinite_flux;
    // Publish exactly the H/low blend verified above. No
    // post-factor projection may change the verified object.
    const double flrho=dir==0?base_left.mom_u:dir==1?base_left.mom_v:base_left.mom_w;
    const double frrho=dir==0?base_right.mom_u:dir==1?base_right.mom_v:base_right.mom_w;
    for(int s=0;s<n_spec;++s) {
        const double low_species=(gamma_wall||pressure_wall)?0.:
            .5*flrho*Xi_L[s]+.5*frrho*Xi_R[s]
            -(.5*factor.wave_speed)*(base_right.rho*Xi_R[s]-base_left.rho*Xi_L[s]);
        const double value=factor.theta==0.?low_species:
            factor.theta==1.?face_species_flux[s]:
            low_species+factor.theta*(face_species_flux[s]-low_species);
        if(!arch::math::isfinite(value))
            return PointWallStatus::nonfinite_species;
        face_species_flux[s]=value;
    }
    output=blended;
    return PointWallStatus::valid;
}

} // namespace FluxAdmissibility
