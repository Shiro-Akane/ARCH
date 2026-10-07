/**
 * @file AmrFluxMath.h
 * @brief Scalar AMR flux-register mathematics shared by Host and devices.
 *
 * The logical plan contains only geometry and route orientation.  Time
 * integrators supply the (possibly negative) stage weight at execution time.
 * Keeping that value out of the plan makes one canonical plan reusable by
 * Euler/RK hydro and every RKL diffusion stage.
 * Registration accumulates the signed fine-minus-coarse flux difference.
 * Reflux applies U_after = U_before + correction * registered_flux, with
 * geometry and orientation supplied by the execution plan. Species follow
 * the same update for rho*X and are divided by the updated density, so the
 * correction conserves species mass rather than the mass fraction itself.
 * Workflow:
 * 1. Read oriented hydro face fluxes and AMR level interfaces.
 * 2. Construct or apply conservative flux-register contributions.
 * 3. Return coarse-fine corrections to the stage conservation update.
 */

#pragma once

#include <cmath>

#include "amr/transfer/AmrTransferPlans.h"
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

/**
 * Angular register stores torque flux divided by the coarse ordinary face
 * area. The native geometry owner supplies T_source/A_source; radial axis
 * faces supply exactly zero without forming 0/0.
 * This scalar transform deliberately does not select hydro/viscous semantics.
 */
ARCH_HOST_DEVICE inline double angular_registered_flux(
    double physical_azimuthal_flux, double source_torque_per_area) noexcept
{
    return physical_azimuthal_flux * source_torque_per_area;
}

/**
 * Convert the ordinary dt*A/V reflux coefficient to dt*A/W for m_phi=J/W.
 * The geometry owner must validate positive finite native V and W first.
 * No floor, repair or implicit choice of angular state is made here.
 */
ARCH_HOST_DEVICE inline double angular_reflux_coefficient(
    double ordinary_correction, double native_volume,
    double native_angular_measure) noexcept
{
    return ordinary_correction * (native_volume / native_angular_measure);
}

ARCH_HOST_DEVICE inline double reflux_species_density(
    double rho_before, double mass_fraction_before, double correction,
    double registered_species_flux) noexcept
{
    return rho_before * mass_fraction_before
        + correction * registered_species_flux;
}

/** Publish the conserved-species reflux correction without rerounding identity.
 * Workflow: recognize only finite, positive, unchanged density and an exactly
 * absent transport term; preserve the original fraction bits in that case;
 * otherwise evaluate the original rhoX/rho expression at its original site.
 * Formula: X_after=(rho_before*X_before+c*F_rhoX)/rho_after. When c=0 or
 * F_rhoX=0 and rho_after=rho_before, the exact operation is X_after=X_before.
 * A product that underflows to zero is NOT evidence of absent transport.
 * Invalid controls/state follow the original arithmetic and remain visible
 * to the existing acceptance owner; this function does not repair or validate
 * a composition simplex, inspect a physical module, or change active reflux.
 */
ARCH_HOST_DEVICE inline double reflux_mass_fraction(
    double rho_before, double mass_fraction_before, double correction,
    double registered_species_flux, double rho_after) noexcept
{
    if (std::isfinite(rho_before) && rho_before > 0.0
        && std::isfinite(rho_after) && rho_after > 0.0
        && rho_after == rho_before && std::isfinite(mass_fraction_before)
        && std::isfinite(correction) && std::isfinite(registered_species_flux)
        && (correction == 0.0 || registered_species_flux == 0.0))
        return mass_fraction_before;
    return reflux_species_density(
        rho_before, mass_fraction_before, correction,
        registered_species_flux) / rho_after;
}

} // namespace amr::flux_math
