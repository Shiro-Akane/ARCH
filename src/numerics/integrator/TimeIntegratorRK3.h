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

#include "TimeIntegratorHelper.h"

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

        int total_size = grid.GetTotalSize();
        int n_spec = state_n.GetNumSpecies();

        // Buffers
        std::vector<FluidVector> dU(total_size);
        std::vector<double> d_spec(n_spec * total_size);
        std::vector<FluidVector> fluxes(total_size);
        std::vector<double> spec_fluxes(n_spec * total_size);

        // =========================================================
        // Stage 1: U(1) = U^n + dt * L(U^n)
        // Store in: state_star
        // =========================================================

        TimeIntegration::evaluate_all_dimensions<FluxSchemePolicy>(state_n, eos, grid, dt, dU, d_spec, fluxes, spec_fluxes, entropy_fix_coeff);
        TimeIntegration::perform_stage_update(state_n, state_n, state_star, dU, d_spec, grid, 0.0, 1.0);
        boundary_condition.apply(state_star, grid);

        // =========================================================
        // Stage 2: U(2) = 0.75 * U^n + 0.25 * (U(1) + dt * L(U(1)))
        // Store in: state_np1 (Using np1 as the second buffer)
        // =========================================================

        TimeIntegration::evaluate_all_dimensions<FluxSchemePolicy>(state_star, eos, grid, dt, dU, d_spec, fluxes, spec_fluxes, entropy_fix_coeff);
        TimeIntegration::perform_stage_update(state_n, state_star, state_np1, dU, d_spec, grid, 3.0 / 4.0, 1.0 / 4.0);
        boundary_condition.apply(state_np1, grid);

        // =========================================================
        // Stage 3: U(n+1) = 1/3 * U^n + 2/3 * (U(2) + dt * L(U(2)))
        // Store in: state_np1 (Overwrite result)
        // =========================================================

        TimeIntegration::evaluate_all_dimensions<FluxSchemePolicy>(state_np1, eos, grid, dt, dU, d_spec, fluxes, spec_fluxes, entropy_fix_coeff);
        TimeIntegration::perform_stage_update(state_n, state_np1, state_np1, dU, d_spec, grid, 1.0 / 3.0, 2.0 / 3.0);
    }
};