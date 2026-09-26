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

#include "numerics/flux/FluxFunctions.h"

// Conservative convex limiting against a local Lax-Friedrichs bar state.
// One theta modifies the shared face flux for BOTH neighbouring cells and all
// species, before flux registration. Under dt/V * sum(A*a) <= 1 the Cartesian
// update is a convex combination of its mean and these admissible bar states.
// This is a sufficient Euler invariant-domain condition, not a table-EOS theorem.
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

/** Recover both required mean-state thermodynamic values from one EOS state. */
template<class Eos>
ARCH_INLINE void required_mean_thermo(const FluidVector& mean,
    const double* composition, const Eos& eos, double& pressure, double& speed)
{
    if constexpr (requires {
        eos.get_pressure_and_sound_speed(
            mean.rho, 0.0, composition, pressure, speed);
    }) {
        // The limiter queries the unchanged cell means for every face. Helm's
        // grouped path preserves the strict inversion and acoustic derivatives,
        // while avoiding a second inverse for the same (rho, e, X) state.
        calc_endpoint_thermo(mean, arch::state::recover(mean).internal,
                             composition, eos, pressure, speed);
    } else {
        pressure = eos.get_pressure(mean, composition);
        speed = eos.get_sound_speed(mean, pressure, composition);
    }
}

// One host patch-stage owns this cache. A value is published only after the
// complete EOS query succeeds; the caller resets validity on every RK stage.
struct MeanThermoCache {
    std::vector<double> pressure;
    std::vector<double> sound_speed;
    std::vector<unsigned char> ready;

    void reset(int cells) {
        pressure.resize(cells);
        sound_speed.resize(cells);
        ready.assign(cells, 0);
    }
};

/** Blend a face flux using already validated thermodynamics of both cell means. */
ARCH_INLINE void limit_face_with_thermo(const FluidVector& left, const FluidVector& right,
    const double* x_left, const double* x_right, int species,
    double p_left, double c_left, double p_right, double c_right,
    int direction, FluidVector& high, double* species_flux)
{
    const double a = std::max(std::abs(get_un(left, direction)) + c_left,
                              std::abs(get_un(right, direction)) + c_right);
    const auto fl = get_flux(left, p_left, direction);
    const auto fr = get_flux(right, p_right, direction);
    if (!(a > 0.0) || !std::isfinite(a)) {
        high = FluidVector(arch::state::invalid(), 0.0, 0.0, 0.0, arch::state::invalid());
        return;
    }
    // Local Lax-Friedrichs: F_low=(F_L+F_R)/2-a*(U_R-U_L)/2.
    const auto low = 0.5 * fl + 0.5 * fr - (0.5 * a) * (right - left);
    // Invariant-domain bar state: U_bar=(U_L+U_R)/2-(F_R-F_L)/(2a).
    const auto bar = 0.5 * left + 0.5 * right - (0.5 / a) * (fr - fl);
    if (!valid(bar)) {
        high = FluidVector(arch::state::invalid(), 0.0, 0.0, 0.0, arch::state::invalid());
        return;
    }
    const auto correction = (low - high) / a;
    double theta = std::min(segment_fraction(bar, correction), segment_fraction(bar, -1.0 * correction));
    for (int s = 0; s < species; ++s) {
        const double ql = left.rho * x_left[s], qr = right.rho * x_right[s];
        const double f_l = fl.rho * x_left[s], f_r = fr.rho * x_right[s];
        const double low_species = 0.5 * f_l + 0.5 * f_r - 0.5 * a * (qr - ql);
        const double bar_species = 0.5 * ql + 0.5 * qr - (0.5 / a) * (f_r - f_l);
        const double deviation = std::abs((low_species - species_flux[s]) / a);
        if (!std::isfinite(deviation)) theta = 0.0;
        else if (deviation > bar_species)
            theta = std::min(theta, std::max(0.0, bar_species) / deviation);
    }
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
