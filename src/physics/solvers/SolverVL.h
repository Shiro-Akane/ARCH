/**
 * @file SolverVL.h
 * @brief Vinokur-Von Leer Flux Vector Splitting (FVS) Solver.
 * * A first-order upwind scheme.
 * * Robust for capturing shocks because it respects the direction of information propagation
 * * (Characteristic Waves) physically.
 * *
 * * Formulation:
 * * F_{interface} = F^+(U_Left) + F^-(U_Right)
 */

#pragma once

#include <string>
#include <vector>
#include <algorithm>

#include "FluxFunctions.h"

struct SolverVL
{
    static std::string name() { return "VL"; }

    // VL is a 1st-order upwind scheme, so it only needs the immediate neighbors.
    // Stencil: (i, i+1) -> Interface i+1/2.
    static constexpr int NG = 1;

    /**
     * @brief Performs one time-step update using Vinokur-Von Leer splitting.
     */
    template <typename EosType>
    static void solver(const FluidState &state_old,
                       FluidState &state_new, const EosType &eos,
                       const Grid &grid, double dt)
    {
        double dx = grid.dx;
        double coeff = dt / dx;

        int n_spec = state_old.GetNumSpecies();
        int total_size = grid.GetTotalSize();

        // ---------------------------------------------------------
        // Memory Allocation
        // ---------------------------------------------------------
        // We store fluxes at cell interfaces.
        // Mapping: interface_fluxes[i] corresponds to the face between cell (i-1) and (i).
        std::vector<FluidVector3> interface_fluxes(total_size);
        std::vector<double> interface_species_fluxes(n_spec * total_size);

        // Temp buffers for species mass fractions
        std::vector<double> Yi_L(n_spec);
        std::vector<double> Yi_R(n_spec);

        // ---------------------------------------------------------
        // Step 1: Flux Calculation Loop (Upwind Splitting)
        // Loop covers interfaces from the left of the physical domain to the right.
        // ---------------------------------------------------------
        for (int i = grid.Is() - 1; i < grid.Ie(); i++)
        {
            // Left state (U_i) and Right state (U_{i+1}) at interface i+1/2
            FluidVector3 U_L = state_old.get(i);
            FluidVector3 U_R = state_old.get(i + 1);

            state_old.get_species_to_buffer(i, Yi_L.data());
            state_old.get_species_to_buffer(i + 1, Yi_R.data());

            // 1. Calculate Split Fluxes
            // F+ corresponds to positive eigenvalues (waves moving Right) -> Depends on U_L
            FluidVector3 F_plus = calc_vinokur_flux(U_L, Yi_L.data(), eos, +1);

            // F- corresponds to negative eigenvalues (waves moving Left)  -> Depends on U_R
            FluidVector3 F_minus = calc_vinokur_flux(U_R, Yi_R.data(), eos, -1);

            // 2. Reconstruct Total Flux at Interface
            // F_{i+1/2} = F+(U_L) + F-(U_R)
            interface_fluxes[i + 1] = F_plus + F_minus;

            // 3. Calculate Species Fluxes
            // Physical intuition: Species are passive scalars transported by the mass flow.
            // Mass moving right (F+.rho) carries species from Left (Y_L).
            // Mass moving left  (F-.rho) carries species from Right (Y_R).
            for (int k = 0; k < n_spec; ++k)
            {
                double spec_flux_val = F_plus.rho * Yi_L[k] + F_minus.rho * Yi_R[k];
                interface_species_fluxes[k * total_size + (i + 1)] = spec_flux_val;
            }
        }

        // Ensure output memory is ready
        if (state_new.GetNumSpecies() != n_spec)
            state_new.Resize(grid, n_spec);

        // ---------------------------------------------------------
        // Step 2: Update Loop (Finite Volume Formulation)
        // ---------------------------------------------------------
        for (int i = grid.Is(); i < grid.Ie(); i++)
        {
            // Retrieve fluxes at left (i-1/2) and right (i+1/2) faces
            FluidVector3 F_L = interface_fluxes[i];
            FluidVector3 F_R = interface_fluxes[i + 1];

            // Update Conservative Variables
            // U^{n+1} = U^n - (dt/dx) * (F_{i+1/2} - F_{i-1/2})
            FluidVector3 U_new = state_old.get(i) - coeff * (F_R - F_L);

            state_new.set(i, U_new);

            // --- Species Update ---
            double rho_new = std::max(U_new.rho, 1e-12); // Safety floor

            for (int k = 0; k < n_spec; ++k)
            {
                int offset = k * total_size;

                double F_spec_L = interface_species_fluxes[offset + i];
                double F_spec_R = interface_species_fluxes[offset + i + 1];

                // Update Partial Density (rho * Y)
                double rho_Y_old = state_old.rho[i] * state_old.Y(k, i);
                double rho_Y_new = rho_Y_old - coeff * (F_spec_R - F_spec_L);

                // Recover Y
                double Y_final = rho_Y_new / rho_new;

                // Limiter / Clamping
                // Necessary to prevent numerical noise from creating invalid mass fractions
                Y_final = std::max(0.0, std::min(1.0, Y_final));
                state_new.Y(k, i) = Y_final;
            }
        }
    }
};