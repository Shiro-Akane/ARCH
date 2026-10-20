/**
 * @file HydroGeometryBinding.h
 * @brief Preflight and shared chart selection for Host Hydro scheduler lanes.
 *
 * Workflow: require the selected solver, then preflight every actual Grid
 * and boundary plan before flux-register or patch-output mutation.
 */
#pragma once

#include <stdexcept>

#include "driver/schedule/StageScheduler.h"
#include "numerics/integrator/IHydroSolver.h"

namespace TimeIntegration {
/** Validate the real Host domain before Clear(), output or publication. */
template<typename BCPolicy>
void require_hydro_geometry_domain(
    const amr::AMRControl& control, const BCPolicy& boundary,
    const Numerics::IHydroSolver* hydro)
{
    if (!hydro) throw std::invalid_argument("Host Hydro policy is missing");
    // Validate all patches before Clear(), any stage output, or publication.
    for (int id:control.tree->GetActiveBlocks()) {
        const auto& grid=control.pool->GetBlock(id).grid;
        (void)GridMetrics::make_geometry_view(grid);
        if constexpr (requires { boundary.logical_plan(grid); })
            (void)boundary.logical_plan(grid);
    }
}
} // namespace TimeIntegration
