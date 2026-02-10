/**
 * @file TimeIntegratorRK3.h
 * @brief 3rd Order Strong Stability Preserving Runge-Kutta (SSPRK3) Time Integrator.
 * * Logic:
 * Stage 1: U(1) = U^n + dt * L(U^n)
 * Stage 2: U(2) = 3/4 * U^n + 1/4 * (U(1) + dt * L(U(1)))
 * Stage 3: U(n+1) = 1/3 * U^n + 2/3 * (U(2) + dt * L(U(2)))
 */

#pragma once

#include <vector>
#include <string>
#include <algorithm>
#include "../../data/FluidState.h"

/**
 * @struct SolverRK3
 * @tparam FluxSchemePolicy The flux calculation scheme (e.g., FluxVL<Muscl...>)
 */
template <typename FluxSchemePolicy>
struct SolverRK3
{
    static std::string name() { return "SSPRK3 + " + FluxSchemePolicy::name(); }
    static constexpr int NG = FluxSchemePolicy::NG;

    // ---------------------------------------------------------
    // Helper: Generalized Weighted Update
    // Formula: U_dest = weight_n * U_n + weight_flux * (U_current + dt*L(U_current))
    //
    // This handles the SSP blending directly in the loop.
    // ---------------------------------------------------------
    static void perform_stage_update(const FluidState &u_n,       // The starting state (U^n)
                                     const FluidState &u_current, // The state used for flux calc
                                     FluidState &u_dest,          // Where to write result
                                     const std::vector<FluidVector3> &fluxes,
                                     const std::vector<double> &spec_fluxes,
                                     const Grid &grid, double dt_over_dx,
                                     double weight_n, double weight_flux)
    {
        int n_spec = u_n.GetNumSpecies();
        int total_size = grid.GetTotalSize();

        for (int i = grid.Is(); i < grid.Ie(); i++)
        {
            // 1. Calculate dU = - dt/dx * (F_R - F_L)
            // Note: fluxes[i] is F_{i-1/2}, fluxes[i+1] is F_{i+1/2}
            // Standard conservation law: dU/dt + dF/dx = 0  => dU = - (F_R - F_L)
            // Your code used: (F_L - F_R), which is correct for -dF/dx.
            FluidVector3 dU = dt_over_dx * (fluxes[i] - fluxes[i + 1]);

            // 2. Get state values
            FluidVector3 U_old = u_n.get(i);        // U^n
            FluidVector3 U_curr = u_current.get(i); // Current Stage State

            // 3. SSPRK Combination
            // U_dest = w_n * U^n + w_flux * (U_curr + dU)
            FluidVector3 U_new = weight_n * U_old + weight_flux * (U_curr + dU);

            u_dest.set(i, U_new);

            // 4. Species Update (with same weighting logic)
            double rho_new = std::max(U_new.rho, 1e-13);
            double sum_Y = 0.0;

            for (int k = 0; k < n_spec; ++k)
            {
                int off = k * total_size;
                double d_rhoY = dt_over_dx * (spec_fluxes[off + i] - spec_fluxes[off + i + 1]);

                double rhoY_old = u_n.rho[i] * u_n.Y(k, i);              // (rhoY)^n
                double rhoY_curr = u_current.rho[i] * u_current.Y(k, i); // (rhoY)^curr

                // Combine: w_n * (rhoY)^n + w_flux * ((rhoY)^curr + d_rhoY)
                double rhoY_comb = weight_n * rhoY_old + weight_flux * (rhoY_curr + d_rhoY);

                double Y_k = std::max(0.0, rhoY_comb / rho_new);

                u_dest.Y(k, i) = Y_k;
                sum_Y += Y_k;
            }

            if (sum_Y > 1e-13)
            {
                double inv_sum = 1.0 / sum_Y;
                for (int k = 0; k < n_spec; ++k)
                {
                    u_dest.Y(k, i) *= inv_sum;
                }
            }
            else
            {
                // 极端情况兜底：如果所有组分都为0 (真空?)，重置为默认值
                u_dest.Y(0, i) = 1.0;
            }
        }
    }

    /**
     * @brief Performs one full RK3 time step.
     */
    template <typename EosType, typename BCPolicy>
    static void solve(const FluidState &state_n, FluidState &state_np1,
                      FluidState &state_star, // Used as buffer for U(1)
                      const EosType &eos, const Grid &grid, double dt,
                      BCPolicy &boundary_condition,
                      double entropy_fix_coeff = 0.1)
    {
        double dx = grid.dx;
        double coeff = dt / dx;
        int total_size = grid.GetTotalSize();
        int n_spec = state_n.GetNumSpecies();

        // Buffers
        std::vector<FluidVector3> fluxes(total_size);
        std::vector<double> spec_fluxes(n_spec * total_size);
        if (fluxes.size() != total_size)
            fluxes.resize(total_size);
        if (spec_fluxes.size() != n_spec * total_size)
            spec_fluxes.resize(n_spec * total_size);

        // =========================================================
        // Stage 1: U(1) = U^n + dt * L(U^n)
        // Store in: state_star
        // =========================================================

        // 1.1 Flux on U^n
        FluxSchemePolicy::compute_fluxes(state_n, eos, grid, fluxes, spec_fluxes, entropy_fix_coeff);

        // 1.2 Update
        // w_n = 0.0, w_flux = 1.0 -> Pure Euler step
        perform_stage_update(state_n, state_n, state_star, fluxes, spec_fluxes, grid, coeff,
                             0.0, 1.0);

        // 1.3 Boundary Condition (Critical for next stage flux)
        boundary_condition.apply(state_star, grid);

        // =========================================================
        // Stage 2: U(2) = 0.75 * U^n + 0.25 * (U(1) + dt * L(U(1)))
        // Store in: state_np1 (Using np1 as the second buffer)
        // =========================================================

        // 2.1 Flux on U(1) (state_star)
        FluxSchemePolicy::compute_fluxes(state_star, eos, grid, fluxes, spec_fluxes);

        // 2.2 Update
        // w_n = 3/4, w_flux = 1/4
        perform_stage_update(state_n, state_star, state_np1, fluxes, spec_fluxes, grid, coeff,
                             3.0 / 4.0, 1.0 / 4.0);

        // 2.3 Boundary Condition
        boundary_condition.apply(state_np1, grid);

        // =========================================================
        // Stage 3: U(n+1) = 1/3 * U^n + 2/3 * (U(2) + dt * L(U(2)))
        // Store in: state_np1 (Overwrite result)
        // =========================================================

        // 3.1 Flux on U(2) (state_np1)
        FluxSchemePolicy::compute_fluxes(state_np1, eos, grid, fluxes, spec_fluxes);

        // 3.2 Update
        // w_n = 1/3, w_flux = 2/3
        // Note: input U_current is state_np1, output is also state_np1.
        // This is safe because perform_stage_update is element-wise.
        perform_stage_update(state_n, state_np1, state_np1, fluxes, spec_fluxes, grid, coeff,
                             1.0 / 3.0, 2.0 / 3.0);

        // Final BC is handled by Driver, but standard practice might apply here too if needed immediately
    }
};