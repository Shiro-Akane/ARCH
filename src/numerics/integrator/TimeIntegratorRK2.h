/**
 * @file TimeIntegratorRK2.h
 * @brief 2nd Order Strong Stability Preserving Runge-Kutta (SSPRK2) Time Integrator.
 */

#pragma once

#include <vector>
#include <string>
#include <algorithm>

#include "TimeIntegratorHelper.h"

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

    /**
     * @brief Performs one full RK2 time step.
     */
    template <typename EosType, typename BCPolicy, typename GravityPolicy>
    static void solve(const FluidState &state_n, FluidState &state_np1,
                      FluidState &state_star, // Intermediate buffer provided by driver
                      const EosType &eos, const Grid &grid, double dt,
                      BCPolicy &boundary_condition,
                      GravityPolicy &gravity,
                      double entropy_fix_coeff = 0.1) // Need BCs for intermediate step
    {
        int total_size = grid.GetTotalSize();
        int n_spec = state_n.GetNumSpecies();

        std::vector<FluidVector> dU(total_size);
        std::vector<double> d_spec(n_spec * total_size);
        std::vector<FluidVector> fluxes(total_size);
        std::vector<double> spec_fluxes(n_spec * total_size);

        // =========================================================
        // Stage 1: Predictor
        // U* = U^n + dt * L(U^n)
        // =========================================================
        TimeIntegration::evaluate_all_dimensions<FluxSchemePolicy>(
            state_n, eos, grid, dt, dU, d_spec, fluxes, spec_fluxes, gravity, entropy_fix_coeff);

        TimeIntegration::perform_stage_update(
            state_n, state_n, state_star, dU, d_spec, grid, 0.0, 1.0);

        // 必须在中间态应用边界条件，以便 Stage 2 正确计算边界通量
        boundary_condition.apply(state_star, grid);

        // =========================================================
        // Stage 2: Corrector
        // U^{n+1} = 0.5 * U^n + 0.5 * (U* + dt * L(U*))
        // =========================================================
        TimeIntegration::evaluate_all_dimensions<FluxSchemePolicy>(
            state_star, eos, grid, dt, dU, d_spec, fluxes, spec_fluxes, gravity, entropy_fix_coeff);

        TimeIntegration::perform_stage_update(
            state_n, state_star, state_np1, dU, d_spec, grid, 0.5, 0.5);

        // Final BC is usually handled by the Driver loop after return
    }
};