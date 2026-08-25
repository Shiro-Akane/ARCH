/**
 * @file DriverBurn.h
 * @brief Operator splitting integration of the nuclear reaction network.
 *
 * Workflow:
 * 1. Filters cells by a minimum density threshold to skip vacuums.
 * 2. Extracts cell composition and calculates cell temperature.
 * 3. Calls the underlying ODE solver (e.g. BE_NR) to integrate species abundances.
 * 4. Applies a nuclear energy (enuc) limiter to safely restrict the global CFL timestep.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "ReductionSpec.h"
#include "../core/RuntimeParams.h"
#include "../data/FluidState.h"
#include "../grid/Grid.h"
#include "../numerics/burnsolver/Networks.h"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace DriverBurn
{
inline constexpr double INACTIVE_LIMITER_CANDIDATE = 1.0e99;

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

/** Reconstruct energy, signed ENUC and the per-cell global limiter candidate. */
template <typename EosPolicy>
ARCH_INLINE BurnEnergyHandoff compute_burn_energy_handoff(
    const FluidVector& fluid, const double* ode_state, int n_spec,
    double old_internal_energy, double kinetic_energy, double burn_dt,
    const EosPolicy& eos, const BurnConfigView& burn_cfg)
{
    BurnEnergyHandoff handoff{};
    const double new_internal_energy = recover_burn_internal_energy(
        fluid.rho, ode_state[n_spec], ode_state, eos);
    handoff.new_internal_energy = new_internal_energy;
    handoff.total_energy = fluid.rho * new_internal_energy + kinetic_energy;
    if (burn_dt > 0.0)
        handoff.enuc_rate = (new_internal_energy - old_internal_energy) / burn_dt;

    if (burn_cfg.enucDtFactor > 0.0)
    {
        const double delta_e = std::abs(new_internal_energy - old_internal_energy);
        if (burn_dt > 0.0)
        {
            const double enuc_rate = delta_e / burn_dt;
            const double energyRatioInv = enuc_rate
                / std::max(new_internal_energy, 1.0e-20);
            if (energyRatioInv > 1.0e-30)
                handoff.limiter_candidate = burn_cfg.enucDtFactor / energyRatioInv;
        }
    }
    return handoff;
}

ARCH_INLINE void commit_burn_energy(
    FluidVector& fluid, const BurnEnergyHandoff& handoff)
{
    fluid.eng = handoff.total_energy;
}
} // namespace DriverBurn

template <typename EosPolicy, typename BurnerPolicy>
void execute_burn_step(FluidState &current_state, double burn_dt, const EosPolicy &eos,
                       BurnerPolicy &burn, const Grid &grid, const SimConfig &config,
                       double &dt_burn_global)
{
    if (current_state.enuc_rate.size() != current_state.rho.size())
        throw std::runtime_error("Burn diagnostic storage is not initialized.");
    std::fill(current_state.enuc_rate.begin(), current_state.enuc_rate.end(), 0.0);
    if (!config.physics.burn.use_burn)
        return;
    const BurnConfigView burn_cfg = make_burn_config_view(config.physics.burn);
    const auto reduction_spec = arch::reduction::minimum_spec(
        DriverBurn::INACTIVE_LIMITER_CANDIDATE);
    auto global_reduction = arch::reduction::begin_reduction(reduction_spec);
    const int n_spec = current_state.GetNumSpecies();

    int invalid_composition_count = 0;
    int first_invalid_cell = -1;
    double first_invalid_sum = 0.0;
    double first_invalid_min = 0.0;
    double first_invalid_max = 0.0;

    // Each cell owns its ODE state, network evaluation and LU factorization.
    // Dynamic scheduling is important because stiff substep counts vary strongly
    // across the reaction front; nested teams inside a 22x22 LU are counterproductive.
#pragma omp parallel
    {
        std::vector<double> X_ODE(
            static_cast<std::size_t>(n_spec) + 1, 0.0);
        auto local_reduction = arch::reduction::begin_reduction(reduction_spec);
#pragma omp for collapse(3) schedule(dynamic, 1)
        for (int k = grid.Ks(); k < grid.Ke(); ++k)
        {
            for (int j = grid.Js(); j < grid.Je(); ++j)
            {
                for (int i_idx = grid.Is(); i_idx < grid.Ie(); ++i_idx)
                {
                    int i = grid.GetIndex(i_idx, j, k);
                    FluidVector fluid = current_state.get(i);
                    double rho = fluid.rho;
                    amr::CellLogicalKey cell_key{};
                    cell_key.logical_i = i_idx;
                    cell_key.logical_j = j;
                    cell_key.logical_k = k;
                    cell_key.component = 3;

                    // Preserve the density gate before composition packing.
                    if (DriverBurn::check_burn_density(fluid, burn_cfg)
                        == DriverBurn::BurnCellDisposition::BelowDensity) {
                        arch::reduction::combine_candidate(
                            reduction_spec, local_reduction,
                            {DriverBurn::INACTIVE_LIMITER_CANDIDATE,
                             cell_key, true});
                        continue;
                    }

                    // Reuse one exact-size state per worker; CPU custom
                    // networks are not constrained by the CUDA dense limit.
                    std::fill(X_ODE.begin(), X_ODE.end(), 0.0);
                    current_state.get_species_to_buffer(i, X_ODE.data());

                    // A network state is empty only when the complete composition is
                    // invalid.  Testing X_ODE[0] and X_ODE[1] is incorrect: H1/He3
                    // are normally zero in aprox19/21 helium/carbon fuel, and He4/C12
                    // can both be depleted in an evolved alpha-chain state.
                    const auto prepared = DriverBurn::prepare_burn_cell(
                        fluid, X_ODE.data(), n_spec, eos, burn_cfg);
                    if (prepared.disposition
                        == DriverBurn::BurnCellDisposition::BelowTemperature) {
                        arch::reduction::combine_candidate(
                            reduction_spec, local_reduction,
                            {DriverBurn::INACTIVE_LIMITER_CANDIDATE,
                             cell_key, true});
                        continue;
                    }
                    if (prepared.disposition
                        == DriverBurn::BurnCellDisposition::InvalidComposition)
                    {
#pragma omp critical(burn_invalid_composition)
                        {
                            ++invalid_composition_count;
                            if (first_invalid_cell < 0)
                            {
                                first_invalid_cell = i;
                                first_invalid_sum = prepared.composition_sum;
                                first_invalid_min = prepared.composition_min;
                                first_invalid_max = prepared.composition_max;
                            }
                        }
                        arch::reduction::combine_candidate(
                            reduction_spec, local_reduction,
                            {DriverBurn::INACTIVE_LIMITER_CANDIDATE,
                             cell_key, true});
                        continue;
                    }

                    // Integrate the local network state.
                    double dt_rec = burn_dt;
                    bool success = burn.integrate(
                        X_ODE.data(), rho, burn_dt, eos,
                        config.physics.burn, dt_rec);

                    if (!success)
                    {
                        std::cerr << "[Fatal Error] Burn failed at cell "
                                  << i << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    // Commit the updated composition and temperature.
                    current_state.set_species_from_buffer(i, X_ODE.data());
                    const auto handoff = DriverBurn::compute_burn_energy_handoff(
                        fluid, X_ODE.data(), n_spec, prepared.internal_energy,
                        prepared.kinetic_energy, burn_dt, eos, burn_cfg);
                    DriverBurn::commit_burn_energy(fluid, handoff);
                    current_state.eng[i] = fluid.eng;
                    current_state.enuc_rate[i] = handoff.enuc_rate;
                    arch::reduction::combine_candidate(
                        reduction_spec, local_reduction,
                        arch::reduction::ReductionCandidate{
                            handoff.limiter_candidate, cell_key, true});
                }
            }
        }
#pragma omp critical(burn_limiter_reduction)
        {
            arch::reduction::combine_state(
                reduction_spec, global_reduction, local_reduction);
        }
    }

    const auto reduction_result = arch::reduction::finalize_reduction(
        reduction_spec, global_reduction);
    if (reduction_result.status != arch::reduction::ReductionStatus::Ok)
        throw std::runtime_error("Invalid burn limiter reduction");

    if (invalid_composition_count > 0)
    {
        std::cerr << "[Fatal Error] Invalid complete composition before burn: "
                  << invalid_composition_count << " cell(s); first cell="
                  << first_invalid_cell << ", sum(X)=" << first_invalid_sum
                  << ", min(X)=" << first_invalid_min
                  << ", max(X)=" << first_invalid_max
                  << ", network=" << config.physics.burn.network_name
                  << std::endl;
        throw std::runtime_error("Invalid complete composition before burn");
    }

    // Reduce the per-cell burn recommendation into the global candidate step.
    dt_burn_global = DriverBurn::combine_burn_minimum(
        dt_burn_global, reduction_result.value);
}
