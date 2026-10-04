/**
 * @file GridMetrics.h
 * @brief Shared finite-volume measures for Cartesian and curvilinear grids.
 *
 * These Host/device leaves are the metric authority for flux divergence,
 * transfer, sources, and stability estimates. Inactive-coordinate measures
 * are omitted consistently: spherical 1D uses volume per solid angle, while
 * default curved 2D geometries describe a polar plane, not an (r,theta) slice.
 * An explicit internal AxisymmetricRz view uses full-ring r/z measures.
 * Face fluxes and vector components use the local orthonormal basis.
 */

#pragma once

#include <array>
#include <cmath>
#include <stdexcept>

#include "grid/Grid.h"
#include "grid/GridGeometryView.h"

namespace GridMetrics {

inline Geometry geometry_from_name(const std::string& geometry) {
    if (geometry == "cartesian") return Geometry::Cartesian;
    if (geometry == "cylindrical") return Geometry::Cylindrical;
    if (geometry == "spherical") return Geometry::Spherical;
    return Geometry::Unsupported;
}

/** Native Grid retains its legacy semantics until the full production migration. */
inline bool is_axisymmetric_rz(const Grid&) { return false; }

inline Geometry geometry_kind(const Grid& grid) {
    return geometry_from_name(grid.geometry);
}

inline GeometryView make_geometry_view(const Grid& grid) {
    return {geometry_kind(grid), grid.dim, grid.ng, grid.stride_y,
            grid.stride_z, grid.GetTotalSize(), grid.dx1, grid.dx2, grid.dx3,
            grid.x1_min, grid.x2_min, grid.x3_min};
}

/** Describe one logical finite-volume fragment without allocating a native Grid. */
inline GeometryView make_geometry_view(Geometry geometry, int dimension,
    const std::array<double,3>& lower, const std::array<double,3>& width) {
    return {geometry, dimension, 0, 0, 0, 0, width[0], width[1], width[2],
            lower[0], lower[1], lower[2]};
}

/**
 * Opt-in chart identity for callers migrating the whole RZ math path.
 * Does not change geometry_from_name, runtime capabilities or native Grid.
 * Width/coordinate validity remains the caller's existing grid contract.
 */
inline GeometryView make_rz_geometry_view(GeometryView grid)
{
    if (grid.geometry != Geometry::Cylindrical || grid.dim != 2)
        throw std::invalid_argument("RZ geometry view requires cylindrical dimension 2");
    grid.semantics = GeometrySemantics::AxisymmetricRz;
    return grid;
}

/** Bind one native storage layout to an explicit validated internal chart. */
inline GeometryView make_geometry_view(const Grid& grid, GeometrySemantics semantics)
{
    const auto view=make_geometry_view(grid);
    return semantics==GeometrySemantics::AxisymmetricRz ? make_rz_geometry_view(view) : view;
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

/**
 * Explicit axisymmetric (r,z) measures of a full rotating cell.
 *
 * Workflow: callers validate 0 <= r_left < r_right and dz > 0, then use
 * these same volume/face measures for divergence, transfer and diagnostics.
 * Explicit AxisymmetricRz GeometryView dispatch consumes these same leaves.
 * Default 2-D cylindrical still represents the polar plane until all consumers
 * and runtime geometry identity migrate together.
 * Units are cm^3, cm^2 and cm for CGS inputs. No unit-azimuth normalization
 * or inactive-direction measure is mixed into the full 2*pi volume.
 */
namespace Rz {

/** Integral of r dr dphi dz; reuse the factored radial integral for thin cells. */
ARCH_HOST_DEVICE inline double CellVolume(
    double r_left, double r_right, double dz)
{
    return arch::constants::math::two_pi
        * cylindrical_annulus_volume(r_left, r_right) * dz;
}

/** Radial face integrates r_face dphi dz; the regular axis has zero area. */
ARCH_HOST_DEVICE inline double RadialFaceArea(double r_face, double dz)
{
    return arch::constants::math::two_pi * r_face * dz;
}

/** Axial face integrates r dr dphi, identical on lower and upper z faces. */
ARCH_HOST_DEVICE inline double AxialFaceArea(double r_left, double r_right)
{
    return arch::constants::math::two_pi
        * cylindrical_annulus_volume(r_left, r_right);
}

/** Full-volume average of 1/r; common 2*pi and dz cancel exactly. */
ARCH_HOST_DEVICE inline double InverseRadiusVolumeAverage(
    double r_left, double r_right)
{
    return cylindrical_inverse_radius_average(r_left,r_right);
}

/**
 * Volume centroid <r>_V, distinct from a midpoint and from the W centroid.
 * Normalize by the upper radius so thin cells and large radii do not require
 * subtracting adjacent powers or forming r^2 merely to obtain a length.
 * Preconditions match CellVolume: 0 <= left < right, finite positive measure.
 */
ARCH_HOST_DEVICE inline double VolumeCentroidRadius(double left, double right)
{
    const double t=left/right;
    return (2.0/3.0)*right*((1.0+t+t*t)/(1.0+t));
}

/** W = integral r dV; sole angular-momentum measure for m_phi = J/W. */
ARCH_HOST_DEVICE inline double AngularMomentumMeasure(
    double left, double right, double dz)
{
    return CellVolume(left,right,dz)*VolumeCentroidRadius(left,right);
}

/** <r>_W = integral r^3 dr / integral r^2 dr, for m_phi reconstruction. */
ARCH_HOST_DEVICE inline double AngularReconstructionRadius(double left, double right)
{
    const double t=left/right;
    return .75*right*((1.0+t)*(1.0+t*t)/(1.0+t+t*t));
}

/** Integral r dA on a radial face; exact zero at the regular axis. */
ARCH_HOST_DEVICE inline double RadialTorqueMeasure(double radius, double dz)
{
    return RadialFaceArea(radius,dz)*radius;
}

/** Integral r dA on a z face, not its ordinary annulus area. */
ARCH_HOST_DEVICE inline double AxialTorqueMeasure(double left, double right)
{
    return AxialFaceArea(left,right)*VolumeCentroidRadius(left,right);
}

/** Orthonormal r/z lengths for active direction 0 or 1; no angle factor. */
ARCH_HOST_DEVICE inline double PhysicalSpacing(int direction, double dr, double dz)
{
    return direction == 0 ? dr : dz;
}

} // namespace Rz

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
    if (grid.semantics == GeometrySemantics::AxisymmetricRz)
        return Rz::CellVolume(r_left,r_right,grid.dx2);
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
    if (grid.semantics == GeometrySemantics::AxisymmetricRz)
        return dir == 0 ? Rz::RadialFaceArea(r_face,grid.dx2)
                        : Rz::AxialFaceArea(r_left,r_right);
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
    if (grid.semantics == GeometrySemantics::AxisymmetricRz)
        return Rz::PhysicalSpacing(direction,grid.dx1,grid.dx2);
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
    return cylindrical_inverse_radius_average(left, right);
}

inline double CellVolume(const Grid& grid, int i, int j, int k) {
    return CellVolume(make_geometry_view(grid), i, j, k);
}

inline double FaceArea(const Grid& grid, int dir, int i, int j, int k, bool high_face) {
    return FaceArea(make_geometry_view(grid), dir, i, j, k, high_face);
}

} // namespace GridMetrics
