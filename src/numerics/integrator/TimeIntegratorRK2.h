/**
 * @file TimeIntegratorRK2.h
 * @brief 2nd Order Strong Stability Preserving Runge-Kutta (SSPRK2) Time Integrator.
 */

/**
 * Workflow:
 * 1. Evaluate block-local flux divergence and physical source terms.
 * 2. Combine stages with the documented Euler, RK2, or RK3 coefficients.
 * 3. Leave AMR communication and reflux ownership with the common driver services.
 */
#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "IHydroSolver.h"
#include "TimeIntegratorHelper.h"

#include "../../amr/AMRControl.h"
#include "../../data/FluidState.h"

struct SolverRK2
{
    static std::string name() { return "SSPRK2"; }

    template <typename BCPolicy>
    static void solve(amr::AMRControl &amr_ctrl,
                      double dt,
                      BCPolicy &boundary_condition,
                      const Physical::Gravity::IGravityPolicy* gravity,
                      const Numerics::IHydroSolver* hydro,
                      const NumericsConfig &num_cfg)
    {
        amr_ctrl.flux_register.Clear();
        const auto& active_blocks = amr_ctrl.tree->GetActiveBlocks();
        if (!active_blocks.empty()) {
            amr_ctrl.flux_register.EnsureSpecies(amr_ctrl.pool->GetBlock(active_blocks[0]).fluid_state.GetNumSpecies());
        }
        int total_size = active_blocks.empty() ? 0 : amr_ctrl.pool->GetBlock(active_blocks[0]).grid.GetTotalSize();
        int dim = amr_ctrl.tree->GetRootGridDim();

        // Stage 1
        #pragma omp parallel
        {
            int n_spec = active_blocks.empty() ? 0 : amr_ctrl.pool->GetBlock(active_blocks[0]).fluid_state.GetNumSpecies();
            std::vector<FluidVector> dU(total_size);
            std::vector<double> d_spec(n_spec * total_size);

            #pragma omp for schedule(dynamic)
            for (size_t i = 0; i < active_blocks.size(); ++i) {
                amr::Block &b = amr_ctrl.pool->GetBlock(active_blocks[i]);
                hydro->evaluate_patch(&amr_ctrl, active_blocks[i], b.fluid_state, b.grid, dt, dU, d_spec, gravity, num_cfg, 0.5, nullptr);
                hydro->update_patch(b.fluid_state, b.fluid_state, b.state_scratch, dU, d_spec, b.grid, 0.0, 1.0, num_cfg, nullptr);
            }
        }

        #pragma omp parallel for schedule(dynamic)
        for (size_t i = 0; i < active_blocks.size(); ++i) {
            amr::Block &b = amr_ctrl.pool->GetBlock(active_blocks[i]);
            boundary_condition.apply(b.state_scratch, b.grid);
        }
        amr_ctrl.ghost_exchange.ExecuteExchange(amr_ctrl.pool, amr_ctrl.tree, dim, &amr::Block::state_scratch);

        // Stage 2
        #pragma omp parallel
        {
            int n_spec = active_blocks.empty() ? 0 : amr_ctrl.pool->GetBlock(active_blocks[0]).fluid_state.GetNumSpecies();
            std::vector<FluidVector> dU(total_size);
            std::vector<double> d_spec(n_spec * total_size);

            #pragma omp for schedule(dynamic)
            for (size_t i = 0; i < active_blocks.size(); ++i) {
                amr::Block &b = amr_ctrl.pool->GetBlock(active_blocks[i]);
                hydro->evaluate_patch(&amr_ctrl, active_blocks[i], b.state_scratch, b.grid, dt, dU, d_spec, gravity, num_cfg, 0.5, nullptr);
                hydro->update_patch(b.fluid_state, b.state_scratch, b.state_next, dU, d_spec, b.grid, 0.5, 0.5, num_cfg, nullptr);
            }
        }

        #pragma omp parallel for schedule(dynamic)
        for (size_t i = 0; i < active_blocks.size(); ++i) {
            amr::Block &b = amr_ctrl.pool->GetBlock(active_blocks[i]);
            std::swap(b.fluid_state, b.state_next);
        }

        amr_ctrl.ApplyReflux(dt);
    }
};
