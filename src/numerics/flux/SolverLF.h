/**
 * @file SolverLF.h
 * @brief Lax-Friedrichs (LF) Solver.
 * * A first-order, central difference scheme.
 * * Known for being very stable but highly dissipative (smears shocks).
 * *
 * * Update Formula:
 * * U^{n+1}_i = 0.5 * (U_{i-1} + U_{i+1}) - 0.5 * (dt/dx) * (F_{i+1} - F_{i-1})
 */

#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <omp.h>

#include "FluxFunctions.h"

struct SolverLF
{
    // Identifier for the Factory
    static std::string name() { return "LF"; }

    // Ghost Cells: LF uses a 3-point stencil (i-1, i, i+1), so 1 is min, but 2 is safe.
    static constexpr int NG = 2;

    /**
     * @brief Performs one time-step update using the Lax-Friedrichs method.
     * * @tparam EosType Equation of State type.
     * @param state_old State at time t (Input).
     * @param state_new State at time t+dt (Output).
     * @param eos       EOS object.
     * @param grid      Grid topology.
     * @param dt        Time step size.
     */
    template <typename EosType>
    static void solver(const FluidState &state_old,
                       FluidState &state_new, const EosType &eos,
                       const Grid &grid, double dt)
    {
        // 1. Pre-compute Coefficients
        double dx = grid.dx;
        // Coefficient C = 0.5 * dt / dx
        double coeff = 0.5 * dt / dx;

        int n_spec = state_old.GetNumSpecies();
        int total_size = grid.GetTotalSize();

        // 2. Allocate Temporary Buffers for Fluxes
        // We pre-calculate fluxes at all relevant nodes to avoid re-computing them inside the update loop.
        std::vector<double> species_fluxes(n_spec * total_size);
        std::vector<FluidVector> node_fluxes(grid.GetTotalSize()); // All flux at interface

        // ---------------------------------------------------------
        // Step 1: Flux Calculation Loop
        // Compute F(U) for the entire stencil range (including ghost cells needed)
        // ---------------------------------------------------------
        #pragma omp parallel
        {
            std::vector<double> Yi_local(n_spec);

            #pragma omp for schedule(static)
            for (int i = grid.Is() - 1; i < grid.Ie(); i++) // Range covers i-1 and i+1
            {
                // Load species at cell i
                state_old.get_species_to_buffer(i, Yi_local.data());

                FluidVector U = state_old.get(i);

                // Compute Physical Flux F(U)
                node_fluxes[i] = get_flux(U, Yi_local.data(), eos);

                // Compute Species Fluxes: F_k = (rho * u) * Y_k
                for (int k = 0; k < n_spec; ++k)
                {
                    species_fluxes[k * total_size + i] = node_fluxes[i].rho * Yi_local[k];
                }
            }
        }

        // Ensure output state memory is allocated
        if (state_new.GetNumSpecies() != n_spec)
        {
            state_new.Resize(grid, n_spec);
        }

        // ---------------------------------------------------------
        // Step 2: State Update Loop
        // Apply LF formula for physical domain
        // ---------------------------------------------------------
        #pragma omp parallel for schedule(static)
        for (int i = grid.Is(); i < grid.Ie(); i++)
        {
            // Central Difference of Fluxes: F_{i+1} - F_{i-1}
            FluidVector F_diff = node_fluxes[i + 1] - node_fluxes[i - 1];

            // Lax-Friedrichs Update:
            // Average State: U_avg = 0.5 * (U_{i-1} + U_{i+1})
            // U^{n+1} = U_avg - (dt / 2dx) * F_diff
            FluidVector U_new_val = 0.5 * (state_old.get(i - 1) + state_old.get(i + 1)) - coeff * F_diff;

            state_new.set(i, U_new_val);

            // --- Species Update ---
            double rho_new = U_new_val.rho;
            if (rho_new < 1e-12)
                rho_new = 1e-12; // Numerical floor

            for (int k = 0; k < n_spec; ++k)
            {
                int offset = k * total_size;

                // 1. Flux Difference for Species k
                double F_spec_diff = species_fluxes[offset + i + 1] - species_fluxes[offset + i - 1];

                // 2. Spatial Average of Partial Density (rho * Y)
                double rhoY_L = state_old.rho[i - 1] * state_old.Y(k, i - 1);
                double rhoY_R = state_old.rho[i + 1] * state_old.Y(k, i + 1);
                double rhoY_avg = 0.5 * (rhoY_L + rhoY_R);

                // 3. Update Partial Density
                // (rho * Y)^{n+1} = (rho * Y)_avg - coeff * delta(F_species)
                double rhoY_new = rhoY_avg - coeff * F_spec_diff;

                // 4. Recover Mass Fraction Y = (rho * Y) / rho
                double Y_final = rhoY_new / rho_new;

                // Clamp values to physical range [0, 1]
                if (Y_final < 0.0)
                    Y_final = 0.0;
                if (Y_final > 1.0)
                    Y_final = 1.0;

                state_new.Y(k, i) = Y_final;
            }
        }
    }
};