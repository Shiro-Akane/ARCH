/**
 * @file GeometricSources.h
 * @brief Shared curvilinear momentum sources for one accepted cell.
 *
 * Workflow:
 * 1. Consume the actual coordinate layout, physical means and source duration.
 * 2. Apply common cylindrical/polar/spherical curvature with the same measures
 *    used by the flux divergence; these terms do not source mass or total energy.
 * The caller owns stage scratch, geometry/state preparation and publication.
 * No allocation, extra evolved field, floor or clipping is introduced.
 */
#pragma once

#include <cmath>
#include "core/ArchPortability.h"
#include "data/FluidState.h"
#include "numerics/curvilinear/GridMetrics.h"

namespace TimeIntegration {

/**
 * Shared cylindrical curvature terms for a piecewise-constant accepted cell.
 * The loader chooses its actual angular component; the same formula serves
 * polar and full cylindrical orthonormal bases.
 * Positive density, resolved pressure and GridMetrics average 1/r are inputs.
 * Angular momentum density gets -rho*v_r*v_phi/r; z, mass and energy do not.
 */
ARCH_HOST_DEVICE inline void add_cylindrical_momentum_sources(
    double rho, double radial_velocity, double angular_velocity,
    double pressure, double inverse_radius, double dt,
    double& radial_delta, double* angular_delta)
{
    radial_delta += dt * (rho * angular_velocity * angular_velocity + pressure) * inverse_radius;
    if (angular_delta)
        *angular_delta += dt * (-rho * radial_velocity * angular_velocity) * inverse_radius;
}

template <typename EosType>
ARCH_HOST_DEVICE inline void add_geometric_source_cell(
    const FluidVector& U, const double* composition, const EosType& eos,
    const GridMetrics::GeometryView& grid, int i, int j, double dt,
    FluidVector& delta)
{
    using GridMetrics::Geometry;
    if (grid.geometry == Geometry::Cartesian) return;
    const double r = grid.GetCellCenterX(i);
    if (r == 0.0) return;
    const double p = eos.get_pressure(U, composition);
    const double rho = U.rho;
    const double v_x = U.mom_u / rho;
    const double v_y = U.mom_v / rho;
    const double v_z = U.mom_w / rho;
    const double inverse_radius = GridMetrics::InverseRadiusVolumeAverage(grid, i);

    // The caller lends an authenticated chart; its component order also serves
    // diffusion and source arithmetic stays in the shared cylindrical leaf.
    const auto layout=GridMetrics::resolve_coordinate_layout(
        grid.geometry,grid.dim);
    if (grid.geometry == Geometry::Cylindrical ||
        (grid.geometry == Geometry::Spherical && grid.dim == 2)) {
        const bool polar=layout.basis==GridMetrics::CoordinateLayout::Basis::CylindricalRPhiZ;
        // Radial cylindrical 1D retains its pressure-only source and the full
        // (r,z,phi) band its azimuthal curvature through mom_w.
        const double v_phi=polar?v_y:(grid.dim==3?v_z:0.0);
        double* angular_delta=polar?&delta.mom_v:
            (grid.dim==3?&delta.mom_w:nullptr);
        add_cylindrical_momentum_sources(rho,v_x,v_phi,p,inverse_radius,dt,
            delta.mom_u,angular_delta);
    } else if (grid.geometry == Geometry::Spherical) {
        if (grid.dim == 1) {
            delta.mom_u += dt * 2.0 * p * inverse_radius;
        } else if (grid.dim == 3) {
            const double theta = grid.SourceTheta(j);
            const double cot_theta = std::cos(theta) / std::sin(theta);
            delta.mom_u += dt * (rho * (v_y * v_y + v_z * v_z) + 2.0 * p) * inverse_radius;
            delta.mom_v += dt * (rho * v_z * v_z * cot_theta + p * cot_theta - rho * v_x * v_y) * inverse_radius;
            delta.mom_w += dt * (-rho * v_x * v_z - rho * v_y * v_z * cot_theta) * inverse_radius;
        }
    }
}

} // namespace TimeIntegration
