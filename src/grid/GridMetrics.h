/**
 * @file GridMetrics.h
 * @brief Shared finite-volume measures for Cartesian and curvilinear grids.
 */

/**
 * Workflow:
 * 1. Derive active logical extents and physical coordinates from RuntimeParams.
 * 2. Expose consistent cell, face, and metric information to numerical operators.
 * 3. Preserve compact 1D/2D storage while retaining the common contiguous pool layout.
 */

#pragma once

#include <cmath>

#include "Grid.h"
#include "GridGeometryView.h"

namespace GridMetrics {

inline Geometry geometry_from_name(const std::string& geometry) {
    if (geometry == "cartesian") return Geometry::Cartesian;
    if (geometry == "cylindrical") return Geometry::Cylindrical;
    if (geometry == "spherical") return Geometry::Spherical;
    return Geometry::Unsupported;
}

inline Geometry geometry_kind(const Grid& grid) {
    return geometry_from_name(grid.geometry);
}

inline GeometryView make_geometry_view(const Grid& grid) {
    return {geometry_kind(grid), grid.dim, grid.ng, grid.stride_y,
            grid.stride_z, grid.GetTotalSize(), grid.dx1, grid.dx2, grid.dx3,
            grid.x1_min, grid.x2_min, grid.x3_min};
}

ARCH_HOST_DEVICE inline double radial_shell_volume(double r_left, double r_right) {
    // Integral r^2 dr, factored before evaluation to retain thin-shell digits.
    return (r_right - r_left)
        * (r_right * r_right + r_right * r_left + r_left * r_left) / 3.0;
}

ARCH_HOST_DEVICE inline double cylindrical_annulus_volume(double r_left, double r_right) {
    return 0.5 * (r_right - r_left) * (r_right + r_left);
}

ARCH_HOST_DEVICE inline double polar_angle_measure(double theta_left, double theta_right) {
    // Integral sin(theta) dtheta = 2 sin(midpoint) sin(half width).
    // Angle addition avoids rounding the midpoint onto the south pole for
    // narrow cells; subtracting cosines loses digits near either pole.
    const double half_width = 0.5 * (theta_right - theta_left);
    const double sine_half = std::sin(half_width);
    const double sine_midpoint = std::sin(theta_left) * std::cos(half_width)
                              + std::cos(theta_left) * sine_half;
    return 2.0 * sine_half * sine_midpoint;
}

ARCH_HOST_DEVICE inline double CellVolume(const GeometryView& grid, int i, int j, int /*k*/) {
    const double r_left = grid.GetFacePosL(i);
    const double r_right = grid.GetFacePosR(i);
    if (grid.geometry == Geometry::Cartesian) {
        double volume = grid.dx1;
        if (grid.dim >= 2) volume *= grid.dx2;
        if (grid.dim == 3) volume *= grid.dx3;
        return volume;
    }
    if (grid.geometry == Geometry::Cylindrical) {
        double volume = cylindrical_annulus_volume(r_left, r_right);
        if (grid.dim >= 2) volume *= grid.dx2;
        if (grid.dim == 3) volume *= grid.dx3;
        return volume;
    }

    // 2-D spherical grids are the project's polar (r, phi) specialization.
    if (grid.dim == 1) return radial_shell_volume(r_left, r_right);
    if (grid.dim == 2) return cylindrical_annulus_volume(r_left, r_right) * grid.dx2;

    const double theta_left = grid.x2_min + (j - grid.ng) * grid.dx2;
    const double theta_right = theta_left + grid.dx2;
    return radial_shell_volume(r_left, r_right)
         * polar_angle_measure(theta_left, theta_right) * grid.dx3;
}

ARCH_HOST_DEVICE inline double FaceArea(const GeometryView& grid, int dir, int i, int j, int /*k*/, bool high_face) {
    const double r_left = grid.GetFacePosL(i);
    const double r_right = grid.GetFacePosR(i);
    const double r_face = high_face ? r_right : r_left;
    if (grid.geometry == Geometry::Cartesian) {
        if (dir == 0) return (grid.dim >= 2 ? grid.dx2 : 1.0) * (grid.dim == 3 ? grid.dx3 : 1.0);
        if (dir == 1) return grid.dx1 * (grid.dim == 3 ? grid.dx3 : 1.0);
        return grid.dx1 * grid.dx2;
    }
    if (grid.geometry == Geometry::Cylindrical) {
        const double annulus = cylindrical_annulus_volume(r_left, r_right);
        if (dir == 0) return r_face * (grid.dim >= 2 ? grid.dx2 : 1.0) * (grid.dim == 3 ? grid.dx3 : 1.0);
        if (dir == 1) return grid.dim == 2 ? (r_right - r_left) : annulus * grid.dx3;
        return (r_right - r_left) * grid.dx2;
    }

    if (grid.dim == 1) return r_face * r_face;
    if (grid.dim == 2) return dir == 0 ? r_face * grid.dx2 : (r_right - r_left);

    const double theta_left = grid.x2_min + (j - grid.ng) * grid.dx2;
    const double theta_right = theta_left + grid.dx2;
    const double theta_face = high_face ? theta_right : theta_left;
    // Angular surfaces integrate r dr, not r^2 dr. These are physical areas
    // for orthonormal flux components; the radial face still integrates dOmega.
    const double radial_area = cylindrical_annulus_volume(r_left, r_right);
    if (dir == 0) return r_face * r_face * polar_angle_measure(theta_left, theta_right) * grid.dx3;
    if (dir == 1) return radial_area * std::sin(theta_face) * grid.dx3;
    return radial_area * grid.dx2;
}

// Orthonormal physical distances for both hyperbolic CFL and diffusive
// gradients/stability. In 2-D both curved systems use (r,phi); in 3-D
// cylindrical uses (r,z,phi) and spherical uses (r,theta,phi).
ARCH_HOST_DEVICE inline double PhysicalSpacing(
    Geometry geometry, int dim, int direction, double dx1, double dx2,
    double dx3, double radius, double theta)
{
    if (geometry == Geometry::Cartesian)
        return direction == 0 ? dx1 : (direction == 1 ? dx2 : dx3);
    if (geometry == Geometry::Cylindrical) {
        if (direction == 0) return dx1;
        if (direction == 1) return dim == 2 ? radius * dx2 : dx2;
        return radius * dx3;
    }
    if (geometry == Geometry::Spherical) {
        if (direction == 0) return dx1;
        if (direction == 1) return radius * dx2;
        return radius * std::sin(theta) * dx3;
    }
    return 0.0;
}

ARCH_HOST_DEVICE inline double PhysicalSpacing(
    const GeometryView& grid, int direction, int i, int j)
{
    return PhysicalSpacing(grid.geometry, grid.dim, direction,
        grid.dx1, grid.dx2, grid.dx3, grid.GetCellCenterX(i), grid.SourceTheta(j));
}

// Cell-volume average of 1/r for piecewise-constant orthonormal sources.
// In spherical 1D/3D this is integral(r dr)/integral(r^2 dr), not 1/r_mid.
// Using the same measure as flux divergence preserves constant-pressure rest.
ARCH_HOST_DEVICE inline double InverseRadiusVolumeAverage(
    const GeometryView& grid, int i)
{
    if (grid.geometry == Geometry::Cartesian) return 0.0;
    const double left = grid.GetFacePosL(i);
    const double right = grid.GetFacePosR(i);
    if (grid.geometry == Geometry::Spherical && grid.dim != 2)
        return cylindrical_annulus_volume(left, right) / radial_shell_volume(left, right);
    return (right - left) / cylindrical_annulus_volume(left, right);
}

inline double CellVolume(const Grid& grid, int i, int j, int k) {
    return CellVolume(make_geometry_view(grid), i, j, k);
}

inline double FaceArea(const Grid& grid, int dir, int i, int j, int k, bool high_face) {
    return FaceArea(make_geometry_view(grid), dir, i, j, k, high_face);
}

} // namespace GridMetrics
