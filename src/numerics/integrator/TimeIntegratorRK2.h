/**
 * @file TimeIntegratorRK2.h
 * @brief 2nd Order Strong Stability Preserving Runge-Kutta (SSPRK2) Time Integrator.
 */

#pragma once

#include <vector>
#include <string>
#include "../../data/FluidState.h"

/**
 * @struct SolverRK2
 * @tparam FluxSchemePolicy The flux calculation scheme (e.g., FluxVL<Muscl...>)
 */
template <typename FluxSchemePolicy>
struct SolverRK2
{
    static std::string name() { return "SSPRK2 + " + FluxSchemePolicy::name(); }
    static constexpr int NG = FluxSchemePolicy::NG; // Forward ghost cell requirement

    // ---------------------------------------------------------
    // Helper: Compute dU/dt = - (F_R - F_L) / dx
    // This function updates the 'destination' state based on fluxes
    // ---------------------------------------------------------
    static void update_finite_volume(const FluidState &u_base, FluidState &u_dest,
                                     const std::vector<FluidVector3> &fluxes,
                                     const std::vector<double> &spec_fluxes,
                                     const Grid &grid, double dt_over_dx,
                                     double weight_base, double weight_update)
    {
        int n_spec = u_base.GetNumSpecies();
        int total_size = grid.GetTotalSize();

        for (int i = grid.Is(); i < grid.Ie(); i++)
        {
            // FVM Update Formula
            FluidVector3 F_L = fluxes[i];
            FluidVector3 F_R = fluxes[i + 1];

            // L(U) term * dt
            FluidVector3 dU = (dt_over_dx) * (F_L - F_R); // Note: -(F_R - F_L) = F_L - F_R

            // SSPRK combination: u_dest = weight_base * u_base + weight_update * (u_current + dU)
            // Usually u_dest is initialized outside or we handle it here.
            // Simplified logic: u_dest = w1 * u_base + w2 * (u_base_of_stage + dU)
            // But to keep this helper generic, let's just do:
            // u_dest = u_base + dU (Euler step) OR specialized linear combination.

            // Let's implement the specific Euler step: U_new = U_old + dU
            // The weighting happens in the main solver function.
            u_dest.set(i, u_base.get(i) + dU);

            // Species
            double rho_new = std::max(u_dest.rho[i], 1e-12);
            for (int k = 0; k < n_spec; ++k)
            {
                int off = k * total_size;
                double d_rhoY = dt_over_dx * (spec_fluxes[off + i] - spec_fluxes[off + i + 1]);

                double rhoY_old = u_base.rho[i] * u_base.Y(k, i);
                double rhoY_new = rhoY_old + d_rhoY;

                u_dest.Y(k, i) = std::max(0.0, std::min(1.0, rhoY_new / rho_new));
            }
        }
    }

    /**
     * @brief Performs one full RK2 time step.
     */
    template <typename EosType, typename BCPolicy>
    static void solve(const FluidState &state_n, FluidState &state_np1,
                      FluidState &state_star, // Intermediate buffer provided by driver
                      const EosType &eos, const Grid &grid, double dt,
                      BCPolicy &boundary_condition) // Need BCs for intermediate step
    {
        double dx = grid.dx;
        double coeff = dt / dx;
        int total_size = grid.GetTotalSize();
        int n_spec = state_n.GetNumSpecies();

        // Reuse buffers to save allocation (could be member variables if class was instantiated)
        static std::vector<FluidVector3> fluxes(total_size);
        static std::vector<double> spec_fluxes(n_spec * total_size);
        if (fluxes.size() != total_size)
            fluxes.resize(total_size);
        if (spec_fluxes.size() != n_spec * total_size)
            spec_fluxes.resize(n_spec * total_size);

        // =========================================================
        // Stage 1: Predictor
        // U* = U^n + dt * L(U^n)
        // =========================================================

        // 1.1 Compute Fluxes based on U^n
        FluxSchemePolicy::compute_fluxes(state_n, eos, grid, fluxes, spec_fluxes);

        // 1.2 Update to obtain U* (Euler Step)
        // state_star = state_n - coeff * (F_R - F_L)
        update_finite_volume(state_n, state_star, fluxes, spec_fluxes, grid, coeff, 0, 1);

        // 1.3 *** CRITICAL *** Apply Boundary Conditions to U*
        // Because Stage 2 needs valid ghost cells for U*
        boundary_condition.apply(state_star, grid);

        // =========================================================
        // Stage 2: Corrector
        // U^{n+1} = 0.5 * U^n + 0.5 * (U* + dt * L(U*))
        // =========================================================

        // 2.1 Compute Fluxes based on U*
        FluxSchemePolicy::compute_fluxes(state_star, eos, grid, fluxes, spec_fluxes);

        // 2.2 Final SSPRK2 Combination
        // We need to manually combine because update_finite_volume is simple Euler.
        // Formula: U^{n+1} = 0.5 * state_n + 0.5 * (state_star + dU_star)

        for (int i = grid.Is(); i < grid.Ie(); i++)
        {
            // Calculate dU_star = - coeff * (F_R - F_L)
            FluidVector3 dU_star = coeff * (fluxes[i] - fluxes[i + 1]);

            // Get values
            FluidVector3 U_n = state_n.get(i);
            FluidVector3 U_star = state_star.get(i);

            // SSPRK2 Combine
            FluidVector3 U_np1 = 0.5 * U_n + 0.5 * (U_star + dU_star);
            state_np1.set(i, U_np1);

            // Species Combine
            double rho_np1 = std::max(U_np1.rho, 1e-12);
            for (int k = 0; k < n_spec; ++k)
            {
                int off = k * total_size;
                double d_rhoY_star = coeff * (spec_fluxes[off + i] - spec_fluxes[off + i + 1]);

                double rhoY_n = state_n.rho[i] * state_n.Y(k, i);
                double rhoY_star = state_star.rho[i] * state_star.Y(k, i);

                double rhoY_np1 = 0.5 * rhoY_n + 0.5 * (rhoY_star + d_rhoY_star);

                state_np1.Y(k, i) = std::max(0.0, std::min(1.0, rhoY_np1 / rho_np1));
            }
        }

        // Final BC is usually handled by the Driver loop after return
    }
};