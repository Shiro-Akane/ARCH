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
 * @param Yi_L Species at index i.
 * @param U_R  State at index i+1.
 * @param Yi_R Species at index i+1.
 * @param Yi_half_buffer Output buffer for averaged species.
 * @param n_species Number of species.
 * @param eos EOS object.
 * @param dt  Time step.
 * @param grid Grid info (for dx).
 * @return FluidVector3 Flux evaluated at the half-step state.
 */
template <typename EosType>
FluidVector3 compute_half_step_flux(const FluidVector3 &U_L, const double *Yi_L,
                                    const FluidVector3 &U_R, const double *Yi_R,
                                    double *Yi_half_buffer,
                                    int n_species,
                                    const EosType &eos, double dt, const Grid &grid)
{
    double dx = grid.dx;

    // Calculate fluxes at the left and right states
    FluidVector3 F_L = get_flux(U_L, Yi_L, eos); // flux of i-1/2
    FluidVector3 F_R = get_flux(U_R, Yi_R, eos); // flux of i+1/2

    // Evolution Formula (Taylor expansion approximation):
    // U_half = Average(U) - (dt/2dx) * delta(F)
    FluidVector3 U_half = 0.5 * (U_L + U_R) - 0.5 * (dt / dx) * (F_R - F_L);

    // Simple arithmetic average for species
    for (int k = 0; k < n_species; ++k)
    {
        Yi_half_buffer[k] = 0.5 * (Yi_L[k] + Yi_R[k]);
    }

    // Return the flux based on this updated half-state
    return get_flux(U_half, Yi_half_buffer, eos);
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
        double dx = grid.dx;
        double coeff = dt / dx; // Note: Full step coefficient (unlike 0.5*dt/dx in LF)

        int n_spec = state_old.GetNumSpecies();
        int total_size = grid.GetTotalSize();

        // Buffers to store fluxes at interfaces (i + 1/2)
        // inter_fluxes[i] stores the flux across the boundary between cell i and i+1
        std::vector<FluidVector3> inter_fluxes(grid.GetTotalSize()); // all flux at interface
        std::vector<double> inter_species_fluxes(n_spec * total_size);

        // Temp buffers for species reconstruction
        std::vector<double> Yi_curr(n_spec);
        std::vector<double> Yi_next(n_spec);
        std::vector<double> Yi_half_buffer(n_spec);

        // ---------------------------------------------------------
        // Step 1: Predictor Step (Flux Calculation)
        // Calculate fluxes at the cell interfaces (i + 1/2) at time (t + dt/2)
        // Loop range: Is-1 to Ie is sufficient to cover interfaces needed for physical cells
        // ---------------------------------------------------------
        for (int i = grid.Is() - 1; i < grid.Ie(); i++)
        {
            FluidVector3 U_i = state_old.get(i);
            FluidVector3 U_ip1 = state_old.get(i + 1);

            state_old.get_species_to_buffer(i, Yi_curr.data());
            state_old.get_species_to_buffer(i + 1, Yi_next.data());

            // Compute Flux F_{i+1/2}^{n+1/2}
            // This function (defined in FluxFunctions.h) performs the half-step evolution internally.
            inter_fluxes[i] = compute_half_step_flux(
                U_i, Yi_curr.data(),
                U_ip1, Yi_next.data(),
                Yi_half_buffer.data(),
                n_spec,
                eos, dt, grid);

            // Compute Species Fluxes at Interface
            // Approximation: F_{spec} = (Mass Flux)_{half} * (Y)_{avg}
            double mass_flux_half = inter_fluxes[i].rho; // Momentum at half step represents mass flux

            for (int k = 0; k < n_spec; ++k)
            {
                double Y_half = 0.5 * (Yi_curr[k] + Yi_next[k]);
                inter_species_fluxes[k * total_size + i] = mass_flux_half * Y_half;
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
        for (int i = grid.Is(); i < grid.Ie(); i++)
        {
            // Flux Difference: F_{right_interface} - F_{left_interface}
            // inter_fluxes[i] is at (i+1/2), inter_fluxes[i-1] is at (i-1/2)
            FluidVector3 F_diff = inter_fluxes[i] - inter_fluxes[i - 1];
            FluidVector3 U_old = state_old.get(i);

            // Standard Update Formula:
            // U^{n+1} = U^n - (dt/dx) * (F_{i+1/2} - F_{i-1/2})
            FluidVector3 U_new_val = U_old - coeff * F_diff;

            state_new.set(i, U_new_val);

            // --- Species Update ---
            double rho_new = U_new_val.rho;
            if (rho_new < 1e-12)
                rho_new = 1e-12; // Safety floor
            for (int k = 0; k < n_spec; ++k)
            {
                int offset = k * total_size;

                // 1. Partial Density at old step: (rho * Y)^n
                double rhoY_old = U_old.rho * state_old.Y(k, i);

                // 2. Flux Divergence for species
                double F_spec_diff = inter_species_fluxes[offset + i] - inter_species_fluxes[offset + i - 1];

                // 3. Update Partial Density
                double rhoY_new = rhoY_old - coeff * F_spec_diff;

                // 4. Recover Y
                double Y_final = rhoY_new / rho_new;

                // 5. Clamping (Crucial for LW)
                // Lax-Wendroff is dispersive and produces "Gibbs phenomenon" (oscillations)
                // near sharp gradients. This can cause Y to go < 0 or > 1 without this check.
                if (Y_final < 0.0)
                    Y_final = 0.0;
                if (Y_final > 1.0)
                    Y_final = 1.0;

                state_new.Y(k, i) = Y_final;
            }
        }
    }
};