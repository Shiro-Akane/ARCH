/**
 * @file TimeIntegratorEuler.h
 * @brief 1st Order Forward Euler Time Integrator.
 * * Use mainly for debugging or steady-state convergence.
 */

#pragma once

#include <vector>
#include <string>
#include <algorithm>
#include "../../data/FluidState.h"

/**
 * @struct SolverEuler
 * @tparam FluxSchemePolicy The flux calculation scheme (e.g., FluxVL<Muscl...>)
 */
template <typename FluxSchemePolicy>
struct SolverEuler
{
    static std::string name() { return "Euler (1st Order) + " + FluxSchemePolicy::name(); }

    // Euler 只需要 1 层 Ghost Cell 即可进行通量计算（取决于重构），但保持一致即可
    static constexpr int NG = FluxSchemePolicy::NG;

    // ---------------------------------------------------------
    // Helper: FVM Update Formula
    // U_new = U_old - (dt/dx) * (F_R - F_L)
    // ---------------------------------------------------------
    static void update_finite_volume(const FluidState &u_base, FluidState &u_dest,
                                     const std::vector<FluidVector3> &fluxes,
                                     const std::vector<double> &spec_fluxes,
                                     const Grid &grid, double dt_over_dx)
    {
        int n_spec = u_base.GetNumSpecies();
        int total_size = grid.GetTotalSize();

        for (int i = grid.Is(); i < grid.Ie(); i++)
        {
            // 1. Conservative Variables Update
            FluidVector3 F_L = fluxes[i];
            FluidVector3 F_R = fluxes[i + 1];

            // dU = - (dt/dx) * (F_R - F_L)
            FluidVector3 dU = (dt_over_dx) * (F_L - F_R);

            u_dest.set(i, u_base.get(i) + dU);

            // 2. Species Update
            // Y_new = (rhoY_old + d_rhoY) / rho_new
            double rho_new = std::max(u_dest.rho[i], 1e-12);

            for (int k = 0; k < n_spec; ++k)
            {
                int off = k * total_size;
                double d_rhoY = dt_over_dx * (spec_fluxes[off + i] - spec_fluxes[off + i + 1]);

                double rhoY_old = u_base.rho[i] * u_base.Y(k, i);
                double rhoY_new = rhoY_old + d_rhoY;

                double Y_val = rhoY_new / rho_new;

                // Clamp to [0, 1]
                u_dest.Y(k, i) = std::max(0.0, std::min(1.0, Y_val));
            }
        }
    }

    /**
     * @brief Performs one full Euler time step.
     * * Interface matches SolverRK2 exactly for compatibility with Driver.h
     */
    template <typename EosType, typename BCPolicy>
    static void solve(const FluidState &state_n, FluidState &state_np1,
                      FluidState &state_scratch, // [Unused] 占位符，Euler 不需要中间缓存
                      const EosType &eos, const Grid &grid, double dt,
                      BCPolicy &boundary_condition,
                      double entropy_fix_coeff = 0.1) // [Unused] Euler 一步到位，中间不需要刷边界
    {
        double dx = grid.dx;
        double coeff = dt / dx;
        int total_size = grid.GetTotalSize();
        int n_spec = state_n.GetNumSpecies();

        // Memory Management
        std::vector<FluidVector3> fluxes(total_size);
        std::vector<double> spec_fluxes(n_spec * total_size);

        // Ensure size is correct (in case grid changes or first run)
        if (fluxes.size() != total_size)
            fluxes.resize(total_size);
        if (spec_fluxes.size() != n_spec * total_size)
            spec_fluxes.resize(n_spec * total_size);

        // =========================================================
        // Euler Step: U^{n+1} = U^n + dt * L(U^n)
        // =========================================================

        // 1. Compute Fluxes based on U^n
        FluxSchemePolicy::compute_fluxes(state_n, eos, grid, fluxes, spec_fluxes, entropy_fix_coeff);

        // 2. Update directly to U^{n+1}
        update_finite_volume(state_n, state_np1, fluxes, spec_fluxes, grid, coeff);

        // Done! Boundary conditions for n+1 will be handled by the Driver loop next.

        // Suppress unused variable warnings (optional)
        (void)state_scratch;
        (void)boundary_condition;
    }
};