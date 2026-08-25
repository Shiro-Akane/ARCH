/**
 * @file RKL1TimeIntegrator.h
 * @brief First-order RKL production adapters for the shared stage scheduler.
 */

#pragma once

#include "DiffusionAMRStages.h"

struct RKL1TimeIntegrator
{
    static void integrate(amr::Block& block, const auto& eos,
                          const Grid& grid, const SimConfig& config,
                          double dt_hydro, double dt_diff,
                          const auto& bc_handler)
    {
        Numerics::Diffusion::detail::advance_single_rkl(
            block, eos, grid, config, dt_hydro, dt_diff, bc_handler,
            arch::scheduler::RklMethod::RKL1);
    }
};

namespace Numerics::Diffusion {

template <typename EosType, typename BCPolicy>
inline void advance_amr_rkl1(amr::AMRControl& amr_ctrl, double dt,
                             double dt_diff_fe,
                             BCPolicy& boundary_condition,
                             const EosType& eos,
                             const SimConfig& config)
{
    advance_amr_rkl(amr_ctrl, dt, dt_diff_fe, boundary_condition, eos,
                    config, arch::scheduler::RklMethod::RKL1);
}

} // namespace Numerics::Diffusion
