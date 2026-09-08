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
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "DriverBurnPolicy.h"
#include "../data/FluidState.h"
#include "../grid/Grid.h"

#ifdef _OPENMP
#include <omp.h>
#endif


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
    const int state_size = [&] {
        if constexpr (requires { burn.state_size(); }) return burn.state_size();
        else if constexpr (requires { BurnerPolicy::NEQ; }) return BurnerPolicy::NEQ;
        else return 0;
    }();
    if (state_size <= n_spec)
        throw std::logic_error("Active CPU burner has no valid packed ODE state extent.");

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
            static_cast<std::size_t>(state_size), 0.0);
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
                    cell_key.component = DriverBurn::BURN_LIMITER_COMPONENT;

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
                    double energy_change = 0.0;
                    bool success = burn.integrate(
                        X_ODE.data(), rho, burn_dt, eos,
                        config.physics.burn, dt_rec, &energy_change);

                    if (!success)
                    {
                        std::cerr << "[Fatal Error] Burn failed at cell "
                                  << i << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    const auto handoff = DriverBurn::compute_burn_energy_handoff(
                        fluid, X_ODE.data(), n_spec, prepared.internal_energy,
                        prepared.kinetic_energy, burn_dt, eos, burn_cfg, energy_change);
                    if (!handoff.valid) {
                        std::cerr << "[Fatal Error] Invalid burn energy at cell " << i << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    // Publish composition and energy only after the shared
                    // thermodynamic/source handoff has passed its checks.
                    current_state.set_species_from_buffer(i, X_ODE.data());
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
