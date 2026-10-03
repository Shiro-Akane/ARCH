/**
 * @file JeansDiagnostics.h
 * @brief Shared numeric leaf for the frozen Jeans resolution definition.
 *
 * Workflow:
 * 1. Callers provide accepted total density, EOS sound speed squared and max(h_active).
 * 2. Evaluate N_J = sqrt(pi*c_s^2/(G*rho))/h using the shared CGS constant.
 * 3. Return an explicit numeric-domain result; callers own EOS, AMR and failure policy.
 * This leaf does not advertise JENS support or choose a refinement threshold.
 */
#pragma once

#include <cmath>
#include "core/ArchPortability.h"
#include "physics/constant/PhysicalConstants.h"

namespace JeansDiagnostics {

enum class Status { valid, invalid_input, unrepresentable };
struct Resolution {
    double cells = 0.0;
    Status status = Status::invalid_input;
};

/**
 * @brief Evaluate dimensionless Jeans cells for finite, strictly positive inputs.
 * Density is g/cm^3, sound speed squared cm^2/s^2, and spacing cm.
 * Binary exponent decomposition avoids overflow in c_s^2/(G*rho) and avoids
 * underflow in G*rho when the final resolution remains representable.
 * Zero, negative and nonfinite inputs are outside this leaf's numeric domain.
 * No density, pressure, sound-speed or resolution floor is applied.
 */
ARCH_INLINE Resolution evaluate(double density, double sound_speed_squared,
                                double max_active_spacing)
{
    if (!std::isfinite(density) || density <= 0.0 ||
        !std::isfinite(sound_speed_squared) || sound_speed_squared <= 0.0 ||
        !std::isfinite(max_active_spacing) || max_active_spacing <= 0.0)
        return {};

    int density_exponent = 0, sound_exponent = 0, spacing_exponent = 0;
    const double density_fraction = std::frexp(density, &density_exponent);
    const double sound_fraction = std::frexp(sound_speed_squared, &sound_exponent);
    const double spacing_fraction = std::frexp(max_active_spacing, &spacing_exponent);
    int exponent = sound_exponent - density_exponent;
    double ratio = sound_fraction / density_fraction;
    // Make the exponent even, including negative odd exponents, before sqrt.
    if (exponent % 2 != 0) {
        ratio *= 2.0;
        --exponent;
    }
    const double coefficient = std::sqrt(
        arch::constants::math::pi / arch::constants::gravity::cgs::gravitational_constant)
        * std::sqrt(ratio) / spacing_fraction;
    const double cells = std::ldexp(coefficient, exponent / 2 - spacing_exponent);
    if (!std::isfinite(cells) || cells <= 0.0)
        return {cells, Status::unrepresentable};
    return {cells, Status::valid};
}

} // namespace JeansDiagnostics
