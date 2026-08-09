/**
 * @file TimeIntegratorRK3.h
 * @brief 3rd Order Strong Stability Preserving Runge-Kutta (SSPRK3) Time Integrator.
 */

/**
 * Workflow:
 * 1. Evaluate block-local flux divergence and physical source terms.
 * 2. Combine stages with the documented Euler, RK2, or RK3 coefficients.
 * 3. Leave AMR communication and reflux ownership with the common driver services.
 */

#pragma once

#include <vector>
#include <string>
#include <algorithm>

#include "TimeIntegratorHelper.h"
#include "IHydroSolver.h"

#include "../../data/FluidState.h"
#include "../../amr/AMRControl.h"

struct SolverRK3
{
    static std::string name() { return "SSPRK3"; }

    /**
     * @brief Performs one full RK3 time step.
     */
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

        // =========================================================
        // Stage 1: U^(1) = U^n + dt * L(U^n)
        // =========================================================
        #pragma omp parallel
        {
            int n_spec = active_blocks.empty() ? 0 : amr_ctrl.pool->GetBlock(active_blocks[0]).fluid_state.GetNumSpecies();
            std::vector<FluidVector> dU(total_size);
            std::vector<double> d_spec(n_spec * total_size);

            #pragma omp for schedule(dynamic)
            for (size_t i = 0; i < active_blocks.size(); ++i) {
                amr::Block &b = amr_ctrl.pool->GetBlock(active_blocks[i]);

                hydro->evaluate_patch(&amr_ctrl, active_blocks[i], b.fluid_state, b.grid, dt, dU, d_spec, gravity, num_cfg, 1.0/6.0, nullptr);

                hydro->update_patch(b.fluid_state, b.fluid_state, b.state_scratch, dU, d_spec, b.grid, 0.0, 1.0, num_cfg, nullptr);
            }
        }

        #pragma omp parallel for schedule(dynamic)
        for (size_t i = 0; i < active_blocks.size(); ++i) {
            amr::Block &b = amr_ctrl.pool->GetBlock(active_blocks[i]);
            boundary_condition.apply(b.state_scratch, b.grid);
        }
        amr_ctrl.ghost_exchange.ExecuteExchange(amr_ctrl.pool, amr_ctrl.tree, dim, &amr::Block::state_scratch);

        // =========================================================
        // Stage 2: U^(2) = (3/4) * U^n + (1/4) * U^(1) + (1/4) * dt * L(U^(1))
        // =========================================================
        #pragma omp parallel
        {
            int n_spec = active_blocks.empty() ? 0 : amr_ctrl.pool->GetBlock(active_blocks[0]).fluid_state.GetNumSpecies();
            std::vector<FluidVector> dU(total_size);
            std::vector<double> d_spec(n_spec * total_size);

            #pragma omp for schedule(dynamic)
            for (size_t i = 0; i < active_blocks.size(); ++i) {
                amr::Block &b = amr_ctrl.pool->GetBlock(active_blocks[i]);

                hydro->evaluate_patch(&amr_ctrl, active_blocks[i], b.state_scratch, b.grid, dt, dU, d_spec, gravity, num_cfg, 1.0/6.0, nullptr);

                hydro->update_patch(b.fluid_state, b.state_scratch, b.state_next, dU, d_spec, b.grid, 0.75, 0.25, num_cfg, nullptr);
            }
        }

        #pragma omp parallel for schedule(dynamic)
        for (size_t i = 0; i < active_blocks.size(); ++i) {
            amr::Block &b = amr_ctrl.pool->GetBlock(active_blocks[i]);
            boundary_condition.apply(b.state_next, b.grid);
        }
        amr_ctrl.ghost_exchange.ExecuteExchange(amr_ctrl.pool, amr_ctrl.tree, dim, &amr::Block::state_next);


        // =========================================================
        // Stage 3: U^{n+1} = (1/3) * U^n + (2/3) * U^(2) + (2/3) * dt * L(U^(2))
        // =========================================================
        #pragma omp parallel
        {
            int n_spec = active_blocks.empty() ? 0 : amr_ctrl.pool->GetBlock(active_blocks[0]).fluid_state.GetNumSpecies();
            std::vector<FluidVector> dU(total_size);
            std::vector<double> d_spec(n_spec * total_size);

            #pragma omp for schedule(dynamic)
            for (size_t i = 0; i < active_blocks.size(); ++i) {
                amr::Block &b = amr_ctrl.pool->GetBlock(active_blocks[i]);

                hydro->evaluate_patch(&amr_ctrl, active_blocks[i], b.state_next, b.grid, dt, dU, d_spec, gravity, num_cfg, 2.0/3.0, nullptr);

                // Note: state_scratch is reused as destination for the final step to avoid 3 temp buffers
                hydro->update_patch(b.fluid_state, b.state_next, b.state_scratch, dU, d_spec, b.grid, 1.0/3.0, 2.0/3.0, num_cfg, nullptr);
            }
        }

        // Final Swap: U^{n+1} is in state_scratch, so we swap it with fluid_state
        #pragma omp parallel for schedule(dynamic)
        for (size_t i = 0; i < active_blocks.size(); ++i) {
            amr::Block &b = amr_ctrl.pool->GetBlock(active_blocks[i]);
            std::swap(b.fluid_state, b.state_scratch);
        }

        amr_ctrl.ApplyReflux(dt);
    }
};