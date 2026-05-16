/**
 * @file TimeIntegratorEuler.h
 * @brief 1st Order Forward Euler Time Integrator.
 * * Use mainly for debugging or steady-state convergence.
 */

#pragma once

#include <vector>
#include <string>
#include <algorithm>

#include "TimeIntegratorHelper.h"

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

    /**
     * @brief Performs one full Euler time step.
     * * Interface matches SolverRK2 exactly for compatibility with Driver.h
     */
    template <typename EosType, typename BCPolicy, typename GravityPolicy>
    static void solve(const FluidState &state_n, FluidState &state_np1,
                      FluidState &state_scratch, // [Unused] 占位符，Euler 不需要中间缓存
                      const EosType &eos, const Grid &grid, double dt,
                      BCPolicy &boundary_condition,
                      GravityPolicy &gravity,
                      double entropy_fix_coeff = 0.1) // [Unused] Euler 一步到位，中间不需要刷边界
    {

        int total_size = grid.GetTotalSize();
        int n_spec = state_n.GetNumSpecies();

        // Memory Management
        std::vector<FluidVector> dU(total_size);
        std::vector<double> d_spec(n_spec * total_size);
        std::vector<FluidVector> fluxes(total_size);
        std::vector<double> spec_fluxes(n_spec * total_size);

        // =========================================================
        // Single Euler Step: U^{n+1} = U^n + dt * L(U^n)
        // =========================================================
        TimeIntegration::evaluate_all_dimensions<FluxSchemePolicy>(
            state_n, eos, grid, dt, dU, d_spec, fluxes, spec_fluxes, gravity, entropy_fix_coeff);

        // Update directly to state_np1 (w_n = 0.0, w_flux = 1.0)
        TimeIntegration::perform_stage_update(
            state_n, state_n, state_np1, dU, d_spec, grid, 0.0, 1.0);

        // Suppress unused variables
        (void)state_scratch;
        (void)boundary_condition;
        (void)gravity;
    }
};