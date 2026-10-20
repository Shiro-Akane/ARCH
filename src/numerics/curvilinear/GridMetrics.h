/**
 * @file GridMetrics.h
 * @brief Shared finite-volume measures for Cartesian and curvilinear grids.
 *
 * These Host/device leaves are the metric authority for flux divergence,
 * transfer, sources, and stability estimates. Inactive-coordinate measures
 * are omitted consistently: spherical 1D uses volume per solid angle, while
 * Spherical 2D describes a polar plane, not an (r,theta) slice. Every
 * cylindrical two-dimensional cell uses volume per unit azimuth. Native
 * storage views do not grant field, solver, AMR or backend qualification.
 * Face fluxes and vector components use the local orthonormal basis.
 */

#pragma once

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "grid/Grid.h"
#include "numerics/curvilinear/GridGeometryView.h"




namespace GridMetrics {

inline Geometry geometry_from_name(const std::string& geometry) {
    if (geometry == "cartesian") return Geometry::Cartesian;
    if (geometry == "cylindrical") return Geometry::Cylindrical;
    if (geometry == "spherical") return Geometry::Spherical;
    return Geometry::Unsupported;
}

/** Resolve the public computational chart after typed configuration validation.
 * Workflow: reject unknown canonical geometry/dimension; return the validated
 * native geometry kind for coordinates, measures and boundary construction. This derived
 * identity grants no backend, AMR, source-field or scientific capability and
 * changes no internal Grid default.
 */
inline Geometry resolve_public_chart(const std::string& geometry, int dimension)
{
    const auto kind=geometry_from_name(geometry);
    if (kind==Geometry::Unsupported || dimension<1 || dimension>3)
        throw std::invalid_argument("Public chart requires a known canonical geometry and dimension 1..3");
    return kind;
}

inline Geometry geometry_kind(const Grid& grid) {
    return geometry_from_name(grid.geometry);
}

/** Borrow actual geometry and configured spacing, including inactive dx3=0.
 * A view owns no state, field or AMR authority; it only lends native geometry.
 * Public chart selection and each solver retain their own qualification gate.
 */
inline GeometryView make_geometry_view(const Grid& grid) {
    return {geometry_kind(grid), grid.dim, grid.ng, grid.stride_y,
            grid.stride_z, grid.GetTotalSize(), grid.dx1, grid.dx2, grid.dx3,
            grid.x1_min, grid.x2_min, grid.x3_min};
}

/** Describe one logical finite-volume fragment without allocating a native Grid.
 * Widths belong to the caller; no fake unit width is assigned to an inactive axis.
 */
inline GeometryView make_geometry_view(Geometry geometry, int dimension,
    const std::array<double,3>& lower, const std::array<double,3>& width) {
    return {geometry, dimension, 0, 0, 0, 0, width[0], width[1], width[2],
            lower[0], lower[1], lower[2]};
}

ARCH_HOST_DEVICE inline double radial_shell_volume(double r_left, double r_right) {
    // Integral r^2 dr, factored before evaluation to retain thin-shell digits.
    return (r_right - r_left)
        * (r_right * r_right + r_right * r_left + r_left * r_left) / 3.0;
}

ARCH_HOST_DEVICE inline double cylindrical_annulus_volume(double r_left, double r_right) {
    // Integral r dr = (r_right^2 - r_left^2)/2, factored for thin annuli.
    return 0.5 * (r_right - r_left) * (r_right + r_left);
}

/** Integral (1/r) r dr divided by integral r dr; finite at an axis cell. */
ARCH_HOST_DEVICE inline double cylindrical_inverse_radius_average(
    double r_left, double r_right)
{
    return (r_right-r_left)/cylindrical_annulus_volume(r_left,r_right);
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
    if (grid.geometry==Geometry::Cylindrical && grid.dim==2 &&
        (dir<0 || dir>=grid.dim))
        return std::numeric_limits<double>::quiet_NaN();
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
        if (dir == 1) return annulus * (grid.dim == 2 ? 1.0 : grid.dx3);
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
// gradients/stability. Two-dimensional spherical uses (r,phi); in 3-D
// cylindrical uses (r,z,phi) and spherical uses (r,theta,phi).
ARCH_HOST_DEVICE inline double PhysicalSpacing(
    Geometry geometry, int dim, int direction, double dx1, double dx2,
    double dx3, double radius, double theta)
{
    if (geometry == Geometry::Cartesian)
        return direction == 0 ? dx1 : (direction == 1 ? dx2 : dx3);
    if (geometry == Geometry::Cylindrical) {
        if (direction == 0) return dx1;
        if (direction == 1) return dx2;
        // Axisymmetry has no phi distance/CFL contribution, irrespective of
        // any unused raw-view width supplied by a caller.
        if (dim == 2) return 0.0;
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

/** Distance between the two represented cells of a right-indexed face.
 * Retained charts keep their original physical spacing; invalid or refused
 * geometry returns NaN, never a representative-width fallback.
 */
ARCH_HOST_DEVICE inline double CellPairDistance(
    const GeometryView& grid,int direction,int i,int j)
{
    return PhysicalSpacing(grid,direction,i,j);
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
    return cylindrical_inverse_radius_average(left, right);
}

inline double CellVolume(const Grid& grid, int i, int j, int k) {
    return CellVolume(make_geometry_view(grid), i, j, k);
}

inline double FaceArea(const Grid& grid, int dir, int i, int j, int k, bool high_face) {
    return FaceArea(make_geometry_view(grid), dir, i, j, k, high_face);
}

/** Moment W=integral r dV for a PWC azimuthal momentum coefficient.
 * Only cylindrical and polar orthonormal charts share this lever arm. W has
 * one extra length factor relative to V; no new evolved J/ell field is needed.
 * A full axisymmetric physical integral multiplies both V and W by 2*pi.
 */
ARCH_HOST_DEVICE inline double AngularMomentumVolumeMoment(
    const GeometryView& grid, int i, int /*j*/, int /*k*/)
{
    const auto layout=resolve_coordinate_layout(grid.geometry,grid.dim);
    using Basis=CoordinateLayout::Basis;
    if (grid.dim<2 || (layout.basis!=Basis::CylindricalRZPhi &&
                      layout.basis!=Basis::CylindricalRPhiZ))
        return std::numeric_limits<double>::quiet_NaN();
    // Integral r^2 dr uses the existing factored polynomial for thin rings.
    double moment=radial_shell_volume(grid.GetFacePosL(i),grid.GetFacePosR(i))*grid.dx2;
    if (grid.dim==3) moment*=grid.dx3;
    return moment;
}

/** Face moment T=integral r dA for transport/traction of azimuthal momentum.
 * In (r,z), Tr=r_face^2*dz and Tz=integral r^2 dr. The polar phi face
 * integrates r dr, while a resolved cylindrical phi face also includes dz.
 * Multiplying an ordinary face area by r_mid is not this integral. Unsupported
 * bases or unresolved face directions return NaN, not a plausible fallback.
 */
ARCH_HOST_DEVICE inline double AngularMomentumFaceMoment(
    const GeometryView& grid, int dir, int i, int /*j*/, int /*k*/, bool high_face)
{
    const auto layout=resolve_coordinate_layout(grid.geometry,grid.dim);
    using Basis=CoordinateLayout::Basis;
    if (grid.dim<2 || dir<0 || dir>=grid.dim ||
        (layout.basis!=Basis::CylindricalRZPhi && layout.basis!=Basis::CylindricalRPhiZ))
        return std::numeric_limits<double>::quiet_NaN();
    const double left=grid.GetFacePosL(i),right=grid.GetFacePosR(i);
    if (dir==0) {
        const double face=high_face?right:left;
        double moment=face*face*grid.dx2;
        if (grid.dim==3) moment*=grid.dx3;
        return moment;
    }
    if (dir==layout.axial)
        return radial_shell_volume(left,right)*(grid.dim==3?grid.dx3:1.0);
    return cylindrical_annulus_volume(left,right)*(grid.dim==3?grid.dx2:1.0);
}

inline double AngularMomentumVolumeMoment(const Grid& grid,int i,int j,int k) {
    return AngularMomentumVolumeMoment(make_geometry_view(grid),i,j,k);
}
inline double AngularMomentumFaceMoment(const Grid& grid,int dir,int i,int j,int k,bool high_face) {
    return AngularMomentumFaceMoment(make_geometry_view(grid),dir,i,j,k,high_face);
}

} // namespace GridMetrics
