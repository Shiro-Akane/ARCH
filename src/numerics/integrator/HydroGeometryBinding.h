/**
 * @file HydroGeometryBinding.h
 * @brief Preflight and shared chart selection for Host Hydro scheduler lanes.
 */
#pragma once
#include "numerics/integrator/IHydroSolver.h"

namespace TimeIntegration {
struct HydroGeometryBinding {
    GridMetrics::GeometrySemantics semantics;
    amr::CoordinateSeamGeometry exchange_chart;
};

template<typename BCPolicy>
HydroGeometryBinding bind_hydro_geometry(
    const amr::AMRControl& control, const BCPolicy& boundary,
    const Numerics::IHydroSolver* hydro,
    const Physical::Gravity::IGravityPolicy* gravity)
{
    if (!hydro) throw std::invalid_argument("Host Hydro policy is missing");
    const auto semantics=hydro->geometry_semantics();
    const bool rz=semantics==GridMetrics::GeometrySemantics::AxisymmetricRz;
    if (!rz && semantics!=GridMetrics::GeometrySemantics::Existing)
        throw std::invalid_argument("Unknown Host Hydro chart");
    if constexpr (requires { boundary.geometry_semantics(); }) {
        if (boundary.geometry_semantics()!=semantics)
            throw std::invalid_argument("Hydro and boundary chart mismatch");
    } else {
        if (rz) throw std::invalid_argument("RZ boundary chart identity is missing");
    }
    if (rz && gravity)
        throw std::invalid_argument("RZ gravity requires authoritative finite-ring contract");
    // Validate all patches before Clear(), any stage output, or publication.
    for (int id:control.tree->GetActiveBlocks()) {
        const auto& grid=control.pool->GetBlock(id).grid;
        (void)GridMetrics::make_geometry_view(grid,semantics);
        if constexpr (requires { boundary.logical_plan(grid); })
            (void)boundary.logical_plan(grid);
        else if (rz)
            throw std::invalid_argument("RZ boundary preflight is missing");
    }
    return {semantics,rz ? amr::CoordinateSeamGeometry::RzAxisymmetric
                        : amr::CoordinateSeamGeometry::ExistingChart};
}
} // namespace TimeIntegration
