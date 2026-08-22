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
#include <vector>

#include "../core/RuntimeParams.h"
#include "../data/FluidState.h"
#include "../grid/Grid.h"
#include "../numerics/burnsolver/Networks.h"

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
    double local_dt_burn_min = 1e99;
    const int n_spec = current_state.GetNumSpecies();

    int invalid_composition_count = 0;
    int first_invalid_cell = -1;
    double first_invalid_sum = 0.0;
    double first_invalid_min = 0.0;
    double first_invalid_max = 0.0;

    // Each worker reuses one dynamically sized ODE state. Dynamic scheduling is
    // important because stiff substep counts vary strongly across the front.
#pragma omp parallel reduction(min : local_dt_burn_min)
    {
        std::vector<double> X_ODE(static_cast<std::size_t>(n_spec) + 1, 0.0);
#pragma omp for collapse(3) schedule(dynamic, 1)
    for (int k = grid.Ks(); k < grid.Ke(); ++k)
    {
        for (int j = grid.Js(); j < grid.Je(); ++j)
        {
            for (int i_idx = grid.Is(); i_idx < grid.Ie(); ++i_idx)
            {
                int i = grid.GetIndex(i_idx, j, k);
        double rho = current_state.rho[i];

        // Skip cells below the configured nuclear-density activation threshold.
        if (rho < config.physics.burn.nuclearDensMin)
            continue;

        // Pack cell mass fractions into the worker-local network ODE state.
        std::fill(X_ODE.begin(), X_ODE.end(), 0.0);
        current_state.get_species_to_buffer(i, X_ODE.data());

        // A network state is empty only when the complete composition is
        // invalid.  Testing X_ODE[0] and X_ODE[1] is incorrect: H1/He3
        // are normally zero in aprox19/21 helium/carbon fuel, and He4/C12
        // can both be depleted in an evolved alpha-chain state.
        double composition_sum = 0.0;
        double composition_min = X_ODE[0];
        double composition_max = X_ODE[0];
        bool composition_is_finite = true;
        bool composition_has_negative = false;
        for (int k = 0; k < n_spec; ++k)
        {
            const double xk = X_ODE[k];
            composition_is_finite = composition_is_finite && std::isfinite(xk);
            composition_has_negative = composition_has_negative || xk < -10.0 * config.physics.burn.smallx;
            composition_sum += xk;
            composition_min = std::min(composition_min, xk);
            composition_max = std::max(composition_max, xk);
        }

        const bool composition_is_valid = composition_is_finite && !composition_has_negative && std::isfinite(composition_sum) && composition_sum > 1.0e-13 && std::abs(composition_sum - 1.0) <= 1.0e-6;
        if (!composition_is_valid)
        {
#pragma omp critical(burn_invalid_composition)
            {
                ++invalid_composition_count;
                if (first_invalid_cell < 0)
                {
                    first_invalid_cell = i;
                    first_invalid_sum = composition_sum;
                    first_invalid_min = composition_min;
                    first_invalid_max = composition_max;
                }
            }
            continue;
        }

        // Remove kinetic energy and recover the current temperature through the EOS.
        double mx = current_state.mom_u[i];
        double my = current_state.mom_v[i];
        double mz = current_state.mom_w[i];
        double e_kin = 0.5 * (mx * mx + my * my + mz * mz) / rho;
        double e_int = (current_state.eng[i] - e_kin) / rho;
        // Recover temperature from density, specific internal energy, and composition.
        double T = eos.get_temperature(rho, e_int, X_ODE.data());
        if (T < config.physics.burn.nuclearTempMin)
            continue;
        X_ODE[n_spec] = T; // Temperature occupies the final ODE component.
        // Integrate the local network state.
        double dt_rec = burn_dt;
        bool success = burn.integrate(X_ODE.data(), rho, burn_dt, eos, config.physics.burn, dt_rec);

        if (!success)
        {
            std::cerr << "[Fatal Error] Burn failed at cell " << i << std::endl;
            exit(EXIT_FAILURE);
        }
        // Commit the updated composition and temperature.
        current_state.set_species_from_buffer(i, X_ODE.data());
        double T_new = X_ODE[n_spec];
        // Reconstruct total energy from the EOS state.
        double e_int_new = eos.get_eint_from_T(rho, T_new, X_ODE.data());
        current_state.eng[i] = rho * e_int_new + e_kin;
        if (burn_dt > 0.0)
            current_state.enuc_rate[i] = (e_int_new - e_int) / burn_dt;

        // Apply the nuclear energy timestep limiter when requested.
        if (config.physics.burn.enucDtFactor > 0.0)
        {
            double delta_e = std::abs(e_int_new - e_int);
            if (burn_dt > 0.0)
            {
                double enuc_rate = delta_e / burn_dt;

                // Ratio used by the FLASH-style nuclear energy timestep limit.
                double energyRatioInv = enuc_rate / std::max(e_int_new, 1e-20);
                // Limit the next macro step only for a non-negligible source.
                if (energyRatioInv > 1e-30)
                {
                    double dt_enuc_limit = config.physics.burn.enucDtFactor / energyRatioInv;
                    local_dt_burn_min = std::min(local_dt_burn_min, dt_enuc_limit);
                }
            }
        }
    }
        }
    }
    }

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
    dt_burn_global = std::min(dt_burn_global, local_dt_burn_min);
}
