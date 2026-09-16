/**
 * @file GeometricSources.h
 * @brief Common per-cell orthonormal momentum sources for every backend.
 *
 * These curvature terms complement the metric-weighted flux divergence; they
 * do not add mass or total-energy sources. Radial factors use GridMetrics'
 * cell-volume average of 1/r, not simply the inverse centre radius. Velocity
 * components follow the native orthonormal axes, including the shared 2D
 * polar specialization. The caller supplies the explicit stage duration dt.
 */
#pragma once

#include "../../data/FluidState.h"
#include "../../grid/GridMetrics.h"

#include <algorithm>
#include <cmath>

namespace TimeIntegration {

template <typename EosType>
ARCH_HOST_DEVICE inline void add_geometric_source_cell(
    const FluidVector& U, const double* composition, const EosType& eos,
    const GridMetrics::GeometryView& grid, int i, int j, double dt,
    FluidVector& delta)
{
    using GridMetrics::Geometry;
    if (grid.geometry == Geometry::Cartesian) return;
    const double r = grid.GetCellCenterX(i);
    if (r < 1e-14) return;
    const double p = eos.get_pressure(U, composition);
    const double rho = std::max(U.rho, 1e-12);
    const double v_x = U.mom_u / rho;
    const double v_y = U.mom_v / rho;
    const double v_z = U.mom_w / rho;
    const double inverse_radius = GridMetrics::InverseRadiusVolumeAverage(grid, i);

    if (grid.geometry == Geometry::Cylindrical) {
        // 2D axes are (r,phi); 3D axes are (r,z,phi), as on the CPU.
        const double v_phi = grid.dim == 2 ? v_y : (grid.dim == 3 ? v_z : 0.0);
        delta.mom_u += dt * (rho * v_phi * v_phi + p) * inverse_radius;
        if (grid.dim == 2)
            delta.mom_v += dt * (-rho * v_x * v_y) * inverse_radius;
        else if (grid.dim == 3)
            delta.mom_w += dt * (-rho * v_x * v_z) * inverse_radius;
    } else if (grid.geometry == Geometry::Spherical) {
        if (grid.dim == 1) {
            delta.mom_u += dt * 2.0 * p * inverse_radius;
        } else if (grid.dim == 2) {
            // Preserve the project's 2D polar (r,phi) specialization.
            delta.mom_u += dt * (rho * v_y * v_y + p) * inverse_radius;
            delta.mom_v += dt * (-rho * v_x * v_y) * inverse_radius;
        } else if (grid.dim == 3) {
            const double theta = grid.SourceTheta(j);
            const double cot_theta = std::cos(theta) / std::max(std::sin(theta), 1e-14);
            delta.mom_u += dt * (rho * (v_y * v_y + v_z * v_z) + 2.0 * p) * inverse_radius;
            delta.mom_v += dt * (rho * v_z * v_z * cot_theta + p * cot_theta - rho * v_x * v_y) * inverse_radius;
            delta.mom_w += dt * (-rho * v_x * v_z - rho * v_y * v_z * cot_theta) * inverse_radius;
        }
    }
}

} // namespace TimeIntegration
