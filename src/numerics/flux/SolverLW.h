/**
 * @file SolverLW.h
 * @brief Lax-Wendroff (Richtmyer Two-Step) Solver.
 * * A second-order accurate, central difference scheme.
 * * More accurate than Lax-Friedrichs but dispersive (introduces oscillations/ripples near shocks).
 * *
 * * Logic:
 * * 1. Predictor: Compute fluxes at half-time, half-space (i+1/2, n+1/2).
 * * 2. Corrector: Update cell center (i, n+1) using the divergence of predictor fluxes.
 */

#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <omp.h>

#include "FluxFunctions.h"

// ------------------------------------------------------------------
// 2. Richtmyer Scheme Helper (Predictor Step)
// ------------------------------------------------------------------

/**
 * @brief Computes the flux at the half-step state (Richtmyer predictor).
 * * Used in the Two-Step Lax-Wendroff method.
 * * Step 1: Compute intermediate state U_{i+1/2}^{n+1/2} using Lax-Friedrichs average.
 * * Step 2: Compute Flux F(U_{half}).
 * * @param U_L  State at index i.
 * @param Xi_L Species at index i.
 * @param U_R  State at index i+1.
 * @param Xi_R Species at index i+1.
 * @param Xi_half_buffer Output buffer for averaged species.
 * @param n_species Number of species.
 * @param eos EOS object.
 * @param dt  Time step.
 * @param grid Grid info (for dx1).
 * @return FluidVector3 Flux evaluated at the half-step state.
 */
template <typename EosType>
FluidVector compute_half_step_flux(const FluidVector &U_L, const double *Xi_L,
                                   const FluidVector &U_R, const double *Xi_R,
                                   double *Xi_half_buffer,
                                   int n_species,
                                   const EosType &eos, double dt, const Grid &grid)
{
    double dx1 = grid.dx1;

    // Calculate fluxes at the left and right states
    FluidVector F_L = get_flux(U_L, Xi_L, eos); // flux of i-1/2
    FluidVector F_R = get_flux(U_R, Xi_R, eos); // flux of i+1/2

    // Evolution Formula (Taylor expansion approximation):
    // U_half = Average(U) - (dt/2dx) * delta(F)
    FluidVector U_half = 0.5 * (U_L + U_R) - 0.5 * (dt / dx1) * (F_R - F_L);

    // Simple arithmetic average for species
    for (int k = 0; k < n_species; ++k)
    {
        Xi_half_buffer[k] = 0.5 * (Xi_L[k] + Xi_R[k]);
    }

    // Return the flux based on this updated half-state
    return get_flux(U_half, Xi_half_buffer, eos);
}

struct SolverLW
{
    static std::string name() { return "LW"; }

    // LW uses a 3-point stencil for the full step, but the intermediate step implies dependencies.
    // 2 Ghost Cells is standard safety for 2nd order schemes.
    static constexpr int NG = 2;

    /**
     * @brief Performs one time-step update using the Richtmyer Lax-Wendroff method.
     */
    template <typename EosType>
    static void solver(const FluidState &state_old,
                       FluidState &state_new, const EosType &eos,
                       const Grid &grid, double dt)
    {
        double dx1 = grid.dx1;
        double coeff = dt / dx1; // Note: Full step coefficient (unlike 0.5*dt/dx1 in LF)

        int n_spec = state_old.GetNumSpecies();
        int total_size = grid.GetTotalSize();

        // Buffers to store fluxes at interfaces (i + 1/2)
        // inter_fluxes[i] stores the flux across the boundary between cell i and i+1
        std::vector<FluidVector> inter_fluxes(grid.GetTotalSize()); // all flux at interface
        std::vector<double> inter_species_fluxes(n_spec * total_size);

// ---------------------------------------------------------
// Step 1: Predictor Step (Flux Calculation)
// Calculate fluxes at the cell interfaces (i + 1/2) at time (t + dt/2)
// Loop range: Is-1 to Ie is sufficient to cover interfaces needed for physical cells
// ---------------------------------------------------------
#pragma omp parallel
        {
            // Thread-local species buffers
            std::vector<double> Xi_curr(n_spec);
            std::vector<double> Xi_next(n_spec);
            std::vector<double> Xi_half_buffer(n_spec);

#pragma omp for schedule(static)
            for (int i = grid.Is() - 1; i < grid.Ie(); i++)
            {
                FluidVector U_i = state_old.get(i);
                FluidVector U_ip1 = state_old.get(i + 1);

                state_old.get_species_to_buffer(i, Xi_curr.data());
                state_old.get_species_to_buffer(i + 1, Xi_next.data());

                // Compute Flux F_{i+1/2}^{n+1/2}
                inter_fluxes[i] = compute_half_step_flux(
                    U_i, Xi_curr.data(),
                    U_ip1, Xi_next.data(),
                    Xi_half_buffer.data(),
                    n_spec,
                    eos, dt, grid);

                // Compute Species Fluxes at Interface
                double mass_flux_half = inter_fluxes[i].rho;

                for (int k = 0; k < n_spec; ++k)
                {
                    double X_half = 0.5 * (Xi_curr[k] + Xi_next[k]);
                    inter_species_fluxes[k * total_size + i] = mass_flux_half * X_half;
                }
            }
        }

        // Ensure output memory is ready
        if (state_new.GetNumSpecies() != n_spec)
        {
            state_new.Resize(grid, n_spec);
        }

// ---------------------------------------------------------
// Step 2: Corrector Step (State Update)
// Update cell centers using the divergence of the interface fluxes
// ---------------------------------------------------------
#pragma omp parallel for schedule(static)
        for (int i = grid.Is(); i < grid.Ie(); i++)
        {
            // Flux Difference: F_{right_interface} - F_{left_interface}
            // inter_fluxes[i] is at (i+1/2), inter_fluxes[i-1] is at (i-1/2)
            FluidVector F_diff = inter_fluxes[i] - inter_fluxes[i - 1];
            FluidVector U_old = state_old.get(i);

            // Standard Update Formula:
            // U^{n+1} = U^n - (dt/dx1) * (F_{i+1/2} - F_{i-1/2})
            FluidVector U_new_val = U_old - coeff * F_diff;

            state_new.set(i, U_new_val);

            // --- Species Update ---
            double rho_new = U_new_val.rho;
            if (rho_new < 1e-12)
                rho_new = 1e-12; // Safety floor
            for (int k = 0; k < n_spec; ++k)
            {
                int offset = k * total_size;

                // 1. Partial Density at old step: (rho * X)^n
                double rhoX_old = U_old.rho * state_old.X(k, i);

                // 2. Flux Divergence for species
                double F_spec_diff = inter_species_fluxes[offset + i] - inter_species_fluxes[offset + i - 1];

                // 3. Update Partial Density
                double rhoX_new = rhoX_old - coeff * F_spec_diff;

                // 4. Recover X
                double X_final = rhoX_new / rho_new;

                // 5. Clamping (Crucial for LW)
                // Lax-Wendroff is dispersive and produces "Gibbs phenomenon" (oscillations)
                // near sharp gradients. This can cause X to go < 0 or > 1 without this check.
                if (X_final < 0.0)
                    X_final = 0.0;
                if (X_final > 1.0)
                    X_final = 1.0;

                state_new.X(k, i) = X_final;
            }
        }
    }
};