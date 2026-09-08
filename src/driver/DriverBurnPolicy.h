/** Shared single-cell burn preparation, energy handoff and limiter semantics.
 * CPU iteration and CUDA launch/storage are consumers, not alternative physics.
 * This header deliberately does not import runtime parsing or a host Grid.
 */
#pragma once

#include <algorithm>
#include <cmath>

#include "ReductionSpec.h"
#include "data/GlobalDefs.h"
#include "data/FluidState.h"

namespace DriverBurn
{
inline constexpr double INACTIVE_LIMITER_CANDIDATE = 1.0e99;
inline constexpr int BURN_LIMITER_COMPONENT = 3;

ARCH_INLINE double combine_burn_minimum(double minimum, double candidate)
{
    const auto spec = arch::reduction::minimum_spec(
        INACTIVE_LIMITER_CANDIDATE);
    auto state = arch::reduction::begin_reduction(spec);
    arch::reduction::combine_candidate(
        spec, state, {minimum, {}, true});
    amr::CellLogicalKey candidate_key{};
    candidate_key.component = 1;
    arch::reduction::combine_candidate(
        spec, state, {candidate, candidate_key, true});
    return arch::reduction::finalize_reduction(spec, state).value;
}

enum class BurnCellDisposition : unsigned char
{
    BurnDisabled,
    Ready,
    BelowDensity,
    BelowTemperature,
    InvalidComposition,
    SolverFailed
};

struct BurnCellPreparation
{
    BurnCellDisposition disposition = BurnCellDisposition::InvalidComposition;
    double kinetic_energy = 0.0;
    double internal_energy = 0.0;
    double composition_sum = 0.0;
    double composition_min = 0.0;
    double composition_max = 0.0;
};

struct BurnEnergyHandoff
{
    double new_internal_energy = 0.0;
    double total_energy = 0.0;
    double enuc_rate = 0.0;
    double limiter_candidate = INACTIVE_LIMITER_CANDIDATE;
    bool valid = false;
};

template <typename EosPolicy>
ARCH_INLINE double recover_burn_temperature(
    double rho, double internal_energy, const double* composition,
    const EosPolicy& eos)
{
    return eos.get_temperature(rho, internal_energy, composition);
}

template <typename EosPolicy>
ARCH_INLINE double recover_burn_internal_energy(
    double rho, double temperature, const double* composition,
    const EosPolicy& eos)
{
    return eos.get_eint_from_T(rho, temperature, composition);
}

ARCH_INLINE BurnCellDisposition check_burn_density(
    const FluidVector& fluid, const BurnConfigView& burn_cfg)
{
    return fluid.rho < burn_cfg.nuclearDensMin
        ? BurnCellDisposition::BelowDensity
        : BurnCellDisposition::Ready;
}

/** Prepare one post-density-gate, already-packed ODE state. */
template <typename EosPolicy>
ARCH_INLINE BurnCellPreparation prepare_burn_cell(
    const FluidVector& fluid, double* ode_state, int n_spec,
    const EosPolicy& eos, const BurnConfigView& burn_cfg)
{
    BurnCellPreparation prepared{};
    const double rho = fluid.rho;

    double composition_sum = 0.0;
    double composition_min = ode_state[0];
    double composition_max = ode_state[0];
    bool composition_is_finite = true;
    bool composition_has_negative = false;
    for (int k = 0; k < n_spec; ++k)
    {
        const double xk = ode_state[k];
        composition_is_finite = composition_is_finite && std::isfinite(xk);
        composition_has_negative = composition_has_negative
            || xk < -10.0 * burn_cfg.smallx;
        composition_sum += xk;
        composition_min = std::min(composition_min, xk);
        composition_max = std::max(composition_max, xk);
    }
    prepared.composition_sum = composition_sum;
    prepared.composition_min = composition_min;
    prepared.composition_max = composition_max;

    const bool composition_is_valid = composition_is_finite
        && !composition_has_negative && std::isfinite(composition_sum)
        && composition_sum > 1.0e-13
        && std::abs(composition_sum - 1.0) <= 1.0e-6;
    if (!composition_is_valid)
    {
        prepared.disposition = BurnCellDisposition::InvalidComposition;
        return prepared;
    }

    prepared.kinetic_energy = 0.5
        * (fluid.mom_u * fluid.mom_u + fluid.mom_v * fluid.mom_v
           + fluid.mom_w * fluid.mom_w) / rho;
    prepared.internal_energy = (fluid.eng - prepared.kinetic_energy) / rho;
    const double temperature = recover_burn_temperature(
        rho, prepared.internal_energy, ode_state, eos);
    if (temperature < burn_cfg.nuclearTempMin)
    {
        prepared.disposition = BurnCellDisposition::BelowTemperature;
        return prepared;
    }
    ode_state[n_spec] = temperature;
    prepared.disposition = BurnCellDisposition::Ready;
    return prepared;
}

/** Commit the accepted source integral while checking the final EOS state.
 * Never infer a small heat release by subtracting two large EOS energies.
 */
template <typename EosPolicy>
ARCH_INLINE BurnEnergyHandoff compute_burn_energy_handoff(
    const FluidVector& fluid, const double* ode_state, int n_spec,
    double old_internal_energy, double kinetic_energy, double burn_dt,
    const EosPolicy& eos, const BurnConfigView& burn_cfg, double energy_change)
{
    BurnEnergyHandoff handoff{};
    const double solved_internal_energy = recover_burn_internal_energy(
        fluid.rho, ode_state[n_spec], ode_state, eos);
    const double new_internal_energy = old_internal_energy + energy_change;
    handoff.new_internal_energy = new_internal_energy;
    handoff.total_energy = fluid.rho * new_internal_energy + kinetic_energy;
    handoff.valid = std::isfinite(solved_internal_energy) && solved_internal_energy > 0.0
        && std::isfinite(fluid.rho) && fluid.rho > 0.0
        && std::isfinite(energy_change) && std::isfinite(new_internal_energy)
        && new_internal_energy > 0.0 && std::isfinite(handoff.total_energy);
    if (!handoff.valid) return handoff;
    if (burn_dt > 0.0)
        handoff.enuc_rate = energy_change / burn_dt;

    if (burn_cfg.enucDtFactor > 0.0)
    {
        const double delta_e = std::abs(energy_change);
        if (burn_dt > 0.0)
        {
            const double enuc_rate = delta_e / burn_dt;
            const double energyRatioInv = enuc_rate
                / std::max(new_internal_energy, 1.0e-20);
            if (energyRatioInv > 1.0e-30)
                handoff.limiter_candidate = burn_cfg.enucDtFactor / energyRatioInv;
        }
    }
    handoff.valid = std::isfinite(handoff.enuc_rate)
        && std::isfinite(handoff.limiter_candidate) && handoff.limiter_candidate >= 0.0;
    return handoff;
}

ARCH_INLINE void commit_burn_energy(
    FluidVector& fluid, const BurnEnergyHandoff& handoff)
{
    fluid.eng = handoff.total_energy;
}
} // namespace DriverBurn
