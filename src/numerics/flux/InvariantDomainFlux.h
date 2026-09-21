#pragma once

#include "numerics/flux/FluxFunctions.h"

// Conservative convex limiting against a local Lax-Friedrichs bar state.
// One theta modifies the shared face flux for BOTH neighbouring cells and all
// species, before flux registration. Under dt/V * sum(A*a) <= 1 the Cartesian
// update is a convex combination of its mean and these admissible bar states.
// This is a sufficient Euler invariant-domain condition, not a table-EOS theorem.
namespace FluxAdmissibility {
ARCH_INLINE bool valid(const FluidVector& state)
{ return arch::state::recover(state).status == arch::state::Status::valid; }

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
ARCH_INLINE void limit_reconstruction(const FluidVector& mean, FluidVector& face)
{
    if (!valid(face)) {
        const auto offset = face - mean;
        const double theta = segment_fraction(mean, offset);
        face = theta == 0.0 ? mean : mean + theta * offset;
    }
}

template<class Eos>
ARCH_INLINE void limit_face(const FluidVector& left, const FluidVector& right,
    const double* x_left, const double* x_right, int species, const Eos& eos,
    int direction, FluidVector& high, double* species_flux)
{
    const double p_left = eos.get_pressure(left, x_left);
    const double p_right = eos.get_pressure(right, x_right);
    const double a = std::max(std::abs(get_un(left, direction)) + eos.get_sound_speed(left, p_left, x_left),
                              std::abs(get_un(right, direction)) + eos.get_sound_speed(right, p_right, x_right));
    const auto fl = get_flux(left, p_left, direction);
    const auto fr = get_flux(right, p_right, direction);
    if (!(a > 0.0) || !std::isfinite(a)) {
        high = FluidVector(arch::state::invalid(), 0.0, 0.0, 0.0, arch::state::invalid());
        return;
    }
    const auto low = 0.5 * fl + 0.5 * fr - (0.5 * a) * (right - left);
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
} // namespace FluxAdmissibility
