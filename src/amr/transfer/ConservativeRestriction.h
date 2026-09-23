/**
 * @file ConservativeRestriction.h
 * @brief Small Host/device authority for conservative AMR restriction math.
 *
 * Topology and Morton lowering remain Host-only.  These scalar leaves merely
 * express that species are conserved as rho*X and that physical-volume
 * integrals are converted back to cell averages after restriction.
 * For child volumes V_c, U_parent = sum(V_c U_c) / sum(V_c), whereas
 * X_parent = sum(V_c rho_c X_c) / sum(V_c rho_c). Thus composition is
 * mass-weighted, not volume-weighted. The executor supplies valid positive
 * volume and density integrals before calling the division leaves.
 * Workflow:
 * 1. Receive a committed source topology and staged destination.
 * 2. Build or apply conservative restriction and limited prolongation.
 * 3. Validate transferred state before publishing new block ownership.
 */

#pragma once

#include "core/ArchPortability.h"

namespace amr::restriction_math {

ARCH_INLINE double weighted_conserved_value(
    double value, double measure) noexcept
{
    return value * measure;
}

ARCH_INLINE double weighted_species_density(
    double density, double mass_fraction, double measure) noexcept
{
    return density * mass_fraction * measure;
}

ARCH_INLINE double restricted_average(
    double integral, double measure_sum) noexcept
{
    return integral / measure_sum;
}

/** Recover X from the restricted conserved species and density integrals. */
ARCH_INLINE double restricted_mass_fraction(
    double species_density_integral, double density_integral) noexcept
{
    return species_density_integral / density_integral;
}

} // namespace amr::restriction_math
