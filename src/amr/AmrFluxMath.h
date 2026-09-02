/**
 * @file AmrFluxMath.h
 * @brief Scalar AMR flux-register mathematics shared by Host and devices.
 *
 * The logical plan contains only geometry and route orientation.  Time
 * integrators supply the (possibly negative) stage weight at execution time.
 * Keeping that value out of the plan makes one canonical plan reusable by
 * Euler/RK hydro and every RKL diffusion stage.
 */

#pragma once

#include "AmrTransferPlans.h"
#include "core/ArchPortability.h"

namespace amr::flux_math {

ARCH_HOST_DEVICE constexpr double registration_route_sign(
    RefinementRule rule) noexcept
{
    return rule == RefinementRule::FineFluxContribution ? 1.0 : -1.0;
}

ARCH_HOST_DEVICE constexpr bool is_flux_registration_rule(
    RefinementRule rule) noexcept
{
    return rule == RefinementRule::FineFluxContribution
        || rule == RefinementRule::CoarseFluxContribution;
}

/** Direct coefficient accumulated into (fine flux - coarse flux). */
ARCH_HOST_DEVICE inline double registration_coefficient(
    RefinementRule rule, double geometric_weight,
    double stage_weight) noexcept
{
    return registration_route_sign(rule) * geometric_weight * stage_weight;
}

ARCH_HOST_DEVICE inline double reflux_conserved(
    double before, double correction, double registered_flux) noexcept
{
    return before + correction * registered_flux;
}

ARCH_HOST_DEVICE inline double reflux_species_density(
    double rho_before, double mass_fraction_before, double correction,
    double registered_species_flux) noexcept
{
    return rho_before * mass_fraction_before
        + correction * registered_species_flux;
}

ARCH_HOST_DEVICE inline double reflux_mass_fraction(
    double rho_before, double mass_fraction_before, double correction,
    double registered_species_flux, double rho_after) noexcept
{
    return reflux_species_density(
        rho_before, mass_fraction_before, correction,
        registered_species_flux) / rho_after;
}

} // namespace amr::flux_math
