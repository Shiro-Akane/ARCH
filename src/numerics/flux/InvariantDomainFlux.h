/**
 * @file InvariantDomainFlux.h
 * @brief Limit face fluxes when the high-order update leaves the admissible state domain.
 *
 * Workflow:
 * 1. Receive an explicit mesh/operator and signed cell-centered fields.
 * 2. Limit face fluxes when the high-order update leaves the admissible state domain.
 * 3. Return corrections or fluxes through the shared numerical contract.
 */

#pragma once

#include <stdexcept>
#include <vector>

#include "grid/GridGeometryView.h"
#include "numerics/flux/FluxFunctions.h"

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
template <class Eos>
ARCH_INLINE auto candidate_eos(const Eos& eos)
{
    if constexpr (requires { eos.candidate_view(); }) return eos.candidate_view();
    else return eos;
}

// Host tabular EOS raises on an out-of-table high-order trial. Convert only
// that documented physics failure to a rejected candidate; leave every other
// exception and all required mean-state queries untouched.
template <class Compute>
inline void compute_candidate(Compute&& compute, FluidVector& high,
                              double* species_flux, int species)
{
    try {
        compute();
    } catch (const std::runtime_error&) {
        high = FluidVector(arch::state::invalid(), 0.0, 0.0, 0.0,
                           arch::state::invalid());
        for (int s = 0; s < species; ++s)
            species_flux[s] = arch::state::invalid();
    }
}

/** Test the recovered conserved state against the shared admissible domain. */
ARCH_INLINE bool valid(const FluidVector& state)
{ return arch::state::recover(state).status == arch::state::Status::valid; }

/** Bisect the largest admissible fraction along a conserved-state segment. */
ARCH_INLINE double segment_fraction(const FluidVector& mean, const FluidVector& offset)
{
    if (valid(mean + offset)) return 1.0;
    if (!valid(mean)) return 0.0;
    double lo = 0.0, hi = 1.0;
    for (int iteration = 0; iteration < 54; ++iteration) {
        const double mid = lo + 0.5 * (hi - lo);
        if (valid(mean + mid * offset)) lo = mid; else hi = mid;
    }
    return lo * (1.0 - 16.0 * std::numeric_limits<double>::epsilon());
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

/** Recover required mean thermodynamics from the caller's ordinary EOS input.
 * Legacy callers supply the original mean; native RZ callers supply the shared
 * closure's effective_mean, without replacing their evolved conserved state.
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
    // Appended default preserves Existing semantics for older Host/device
    // aggregate initializers. Native RZ mean EOS inputs are not point states.
    GridMetrics::GeometrySemantics geometry_semantics = GridMetrics::GeometrySemantics::Existing;

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

    /** Reuse an identical ordinary mean; native RZ always requires point EOS. */
    ARCH_INLINE bool query(const FluidVector& state, const double* composition,
                           int count, int left, int right, double& p, double& c) const {
        // Native U stores m_phi=J/W. Its cached EOS uses effective_mean and
        // the density-inertia closure, so equal U bytes do not prove equal
        // point (rho,e,X). This also protects HLLC's direct equal-state query.
        if (geometry_semantics == GridMetrics::GeometrySemantics::AxisymmetricRz)
            return false;
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
        // Ordinary means use recover(state).internal; native RZ query rejects
        // reuse and reaches the original point EOS below. Directional primitive
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
    GridMetrics::GeometrySemantics geometry_semantics=GridMetrics::GeometrySemantics::Existing;
    bool roe_wave_speed = true;
    arch::state::Bounds physical_bounds{}; // Actual numerics bounds, not a GUI policy.
    std::vector<double> pressure;
    std::vector<double> sound_speed;
    std::vector<unsigned char> ready;

    void reset(int cells) {
        pressure.resize(cells);
        sound_speed.resize(cells);
        ready.assign(cells, 0);
    }
};

/** Physical point LLF baseline and the unchanged joint face factor.
 * valid certifies only the original wave/bar prerequisite, not an EOS table,
 * native mixed-measure whole-stage domain or scheduler publication.
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
    if (!(a > 0.0) || !std::isfinite(a)) return result;
    // Local Lax-Friedrichs: F_low=(F_L+F_R)/2-a*(U_R-U_L)/2.
    const auto low = 0.5 * fl + 0.5 * fr - (0.5 * a) * (right - left);
    // Invariant-domain bar state: U_bar=(U_L+U_R)/2-(F_R-F_L)/(2a).
    const auto bar = 0.5 * left + 0.5 * right - (0.5 / a) * (fr - fl);
    result.low=low;
    if (!valid(bar)) return result;
    const auto correction = (low - high) / a;
    double theta = std::min(segment_fraction(bar, correction), segment_fraction(bar, -1.0 * correction));
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
        if (!std::isfinite(shifted_deviation) || !std::isfinite(shifted_bar))
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
} // namespace FluxAdmissibility
