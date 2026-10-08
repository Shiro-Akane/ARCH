/**
 * @file GridMetrics.h
 * @brief Shared finite-volume measures for Cartesian and curvilinear grids.
 *
 * These Host/device leaves are the metric authority for flux divergence,
 * transfer, sources, and stability estimates. Inactive-coordinate measures
 * are omitted consistently: spherical 1D uses volume per solid angle, while
 * Existing curved 2D math views describe a polar plane, not an (r,theta) slice.
 * Public configuration selects full-ring AxisymmetricRz for cylindrical 2D;
 * explicit internal views retain their own semantics and qualification gates.
 * Face fluxes and vector components use the local orthonormal basis.
 */

#pragma once

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "grid/Grid.h"
#include "grid/GridGeometryView.h"
#include "numerics/reconstruction/RzPolynomialMoments.h"

namespace GridMetrics {

inline Geometry geometry_from_name(const std::string& geometry) {
    if (geometry == "cartesian") return Geometry::Cartesian;
    if (geometry == "cylindrical") return Geometry::Cylindrical;
    if (geometry == "spherical") return Geometry::Spherical;
    return Geometry::Unsupported;
}

/** Resolve the public computational chart after typed configuration validation.
 * Workflow: reject unknown canonical geometry/dimension; select full-ring (r,z)
 * only for cylindrical 2D; keep all other public charts on the Existing leaves.
 * This derived identity selects coordinates and measures. It grants no backend,
 * AMR, source-field or scientific capability and changes no internal Grid default.
 */
inline GeometrySemantics resolve_public_chart(const std::string& geometry, int dimension)
{
    const auto kind=geometry_from_name(geometry);
    if (kind==Geometry::Unsupported || dimension<1 || dimension>3)
        throw std::invalid_argument("Public chart requires a known canonical geometry and dimension 1..3");
    return kind==Geometry::Cylindrical && dimension==2
        ? GeometrySemantics::AxisymmetricRz : GeometrySemantics::Existing;
}

/** Internal Grid defaults stay explicit; public owners use resolve_public_chart. */
inline bool is_axisymmetric_rz(const Grid&) { return false; }

inline Geometry geometry_kind(const Grid& grid) {
    return geometry_from_name(grid.geometry);
}

/** Borrow actual geometry and its native generation provenance.
 * Workflow: preserve ordinary fields; authenticate bound native root counts,
 * local endpoints and spacing; copy the complete value-only context. Chart
 * selection remains explicit in the semantics overload below. A fragment view
 * cannot obtain hierarchy authority by merely setting its bound flag.
 */
inline GeometryView make_geometry_view(const Grid& grid) {
    if (grid.dyadic_identity.bound
        && (grid.dim != 2 || geometry_kind(grid) != Geometry::Cylindrical
            || grid.nblockx1 != grid.dyadic_identity.root_blocks[0]
            || grid.nblockx2 != grid.dyadic_identity.root_blocks[1]
            || !matches_identity(grid.dyadic_identity,
                {grid.x1_min,grid.x2_min},{grid.x1_max,grid.x2_max},{grid.dx1,grid.dx2})))
        throw std::invalid_argument("Native RZ geometry view does not match actual generated Grid identity");
    return {geometry_kind(grid), grid.dim, grid.ng, grid.stride_y,
            grid.stride_z, grid.GetTotalSize(), grid.dx1, grid.dx2, grid.dx3,
            grid.x1_min, grid.x2_min, grid.x3_min,GeometrySemantics::Existing,
            {grid.x1_max,grid.x2_max},grid.dyadic_identity};
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
    if (semantics!=GeometrySemantics::Existing
        && semantics!=GeometrySemantics::AxisymmetricRz)
        throw std::invalid_argument("Unknown geometry semantics cannot select a grid chart");
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
 * Existing internal 2-D cylindrical views retain the polar-plane convention;
 * public cylindrical 2-D entry owners select this explicit RZ chart together.
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

/** Actual native cell W=integral r dV, using the same real face width as V.
 * The scalar measure leaf remains the sole integral formula for both backends.
 */
ARCH_HOST_DEVICE inline double AngularMomentumMeasure(const GeometryView& grid,int i,int j)
{
    return AngularMomentumMeasure(grid.GetFacePosL(i),grid.GetFacePosR(i),grid.CellWidth(1,j));
}

/** <r>_W = integral r^3 dr / integral r^2 dr, for m_phi reconstruction. */
ARCH_HOST_DEVICE inline double AngularReconstructionRadius(double left, double right)
{
    const double t=left/right;
    return .75*right*((1.0+t)*(1.0+t*t)/(1.0+t+t*t));
}

/**
 * Radial Gauss-4 times axial Gauss-2 cell samples for V and W averages.
 * Reuse the same radial nodes/weights as native density and inertia closure.
 * This is a numerical
 * integration rule, not an extra physical model or a user accuracy control.
 * Preconditions: finite 0<=left<right and finite z_lower<z_upper.
 * r dr and r^2 dr use separate normalized weights; do not exchange them.
 */
struct CellAverageSample {
    double radius, axial, volume_weight, angular_weight;
};
ARCH_HOST_DEVICE inline std::array<CellAverageSample,8> CellAverageSamples(
    double left,double right,double z_lower,double z_upper)
{
    constexpr double inverse_sqrt_three=.577350269189625764509148780501957456;
    const double t=left/right;
    std::array<CellAverageSample,8> result{};
    int index=0;
    for (int j=0;j<2;++j)
        for (int i=0;i<4;++i) {
            const double radial_fraction=.5+.5*RzReconstruction::quadrature_node(i);
            const double axial_fraction=.5+(j ? .5 : -.5)*inverse_sqrt_three;
            const double radius=left+radial_fraction*(right-left);
            const double q=radius/right;
            result[index++]={radius,z_lower+axial_fraction*(z_upper-z_lower),
                (.5*RzReconstruction::quadrature_weight(i))*q/(1.+t),
                (.75*RzReconstruction::quadrature_weight(i))*q*q/(1.+t+t*t)};
        }
    return result;
}

/** Mirrored coordinate for an axis ghost cell; never a negative V/W measure. */
ARCH_HOST_DEVICE inline double AngularReconstructionCoordinate(double left, double right)
{
    return right<=0.0 ? -AngularReconstructionRadius(-right,-left)
                      : AngularReconstructionRadius(left,right);
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

/** Physical integral r dA, borrowed by hydro, viscosity and reflux owners. */
/** Actual r/z torque face with explicit axial-cell identity.
 * Radial integral is 2*pi*r_face^2*dz; dz is the real cell's face width.
 */
ARCH_HOST_DEVICE inline double FaceTorqueMeasure(
    const GeometryView& grid,int direction,int i,int j,bool upper)
{
    const double left=grid.GetFacePosL(i),right=grid.GetFacePosR(i);
    return direction==0 ? RadialTorqueMeasure(upper?right:left,grid.CellWidth(1,j))
                        : AxialTorqueMeasure(left,right);
}

/** Preserve the existing unbound API/arithmetic. Bound radial torque requires
 * its actual j and fails closed here rather than inventing an axial cell.
 */
ARCH_HOST_DEVICE inline double FaceTorqueMeasure(
    const GeometryView& grid,int direction,int i,bool upper)
{
    if(grid.dyadic_identity.bound&&direction==0)
        return std::numeric_limits<double>::quiet_NaN();
    const double left=grid.GetFacePosL(i),right=grid.GetFacePosR(i);
    return direction==0 ? RadialTorqueMeasure(upper?right:left,grid.dx2)
                        : AxialTorqueMeasure(left,right);
}

/** Orthonormal r/z lengths for active direction 0 or 1; no angle factor. */
ARCH_HOST_DEVICE inline double PhysicalSpacing(int direction, double dr, double dz)
{
    return direction == 0 ? dr : dz;
}

/** Conditioned native J/W flux increment from the actual cell endpoints.
 * Workflow: validate finite physical fluxes and the explicit (r,z) chart;
 * factor the radial torque difference before dividing by W; publish only a
 * finite increment. Failure leaves output untouched and grants no stage
 * acceptance. Caller retains the mandatory accepted-state/EOS gate.
 * With t=l/h and dr=h-l, the exact radial integral ratio is
 * (dt/dr)*3/(1+t+t*t)*[(FL-FR)*t*t-FR*(dr/h)*(1+t)].
 * This is the same two-lever torque/W divergence with 2*pi cancelled;
 * axial torque and W share the annulus factor, giving dt/dz*(FL-FR).
 */
ARCH_HOST_DEVICE inline bool AngularFluxIncrement(
    const GeometryView& grid,int direction,int i,int j,
    double lower_flux,double upper_flux,double dt,double& output)
{
    if(grid.semantics!=GeometrySemantics::AxisymmetricRz
        ||grid.geometry!=Geometry::Cylindrical||grid.dim!=2
        ||(direction!=0&&direction!=1)
        ||!std::isfinite(lower_flux)||!std::isfinite(upper_flux)
        ||!std::isfinite(dt)||dt<0.0) return false;
    const double left=grid.GetFacePosL(i),right=grid.GetFacePosR(i);
    const double dr=right-left,dz=grid.CellWidth(1,j);
    if(!std::isfinite(left)||!std::isfinite(right)||left<0.0
        ||!std::isfinite(dr)||!(dr>0.0)||!std::isfinite(dz)||!(dz>0.0))
        return false;
    double candidate=0.0;
    if(dt!=0.0) {
        const double scaled_dt=dt/(direction==0?dr:dz);
        // A positive timestep ratio cannot silently erase a representable
        // later flux product. Extreme ranges fail closed, without a floor.
        if(!std::isfinite(scaled_dt)||scaled_dt==0.0)return false;
        if(direction==0) {
            const double t=left/right;
            candidate=scaled_dt*(3.0/(1.0+t+t*t))
                *((lower_flux-upper_flux)*t*t
                  -upper_flux*(dr/right)*(1.0+t));
        } else candidate=scaled_dt*(lower_flux-upper_flux);
    }
    if(!std::isfinite(candidate)) return false;
    output=candidate;
    return true;
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
        return Rz::CellVolume(r_left,r_right,grid.CellWidth(1,j));
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
        return dir == 0 ? Rz::RadialFaceArea(r_face,grid.CellWidth(1,j))
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
        return Rz::PhysicalSpacing(direction,grid.CellWidth(0,i),grid.CellWidth(1,j));
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
