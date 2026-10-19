/**
 * @file GridGeometryView.h
 * @brief Allocation-free native-grid geometry shared by Host and device math.
 *
 * Workflow: derive cell centers, faces and widths from the configured spacing
 * and lend them to measures and physical consumers. The axisymmetric
 * cylindrical chart resolves (r,z), while retaining the physical phi component.
 * A geometry view grants no solver, backend or restart qualification.
 */
#pragma once

#include "core/ArchPortability.h"
#include "amr/topology/AmrDefines.h"
#include <limits>
#include <type_traits>
#include "physics/constant/PhysicalConstants.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace GridMetrics {

enum class Geometry : int {
    Cartesian = 0,
    Cylindrical = 1,
    Spherical = 2,
    Unsupported = 3,
};

/** Component order and resolved physical partials of one computational chart.
 * Missing derivatives remain zero-coordinate partials in a physical 3D basis.
 * This value describes coordinates/storage only, with no field or EOS ownership.
 */
struct CoordinateLayout {
    enum class Basis { Unsupported, Cartesian, CylindricalRZPhi,
        CylindricalRPhiZ, SphericalRThetaPhi };
    Basis basis = Basis::Unsupported;
    int radial = -1;
    int axial = -1;
    int theta = -1;
    int azimuth = -1;
    std::array<bool,3> resolved_derivatives{};
    bool valid = false;
};

/** Resolve a native layout without validating fields or enabling a solver.
 * Cylindrical charts use (r,z,phi); in 2D only the r,z derivatives are resolved.
 * Spherical 2D is the existing polar (r,phi,z) specialization, while spherical
 * 1D/3D retain (r,theta,phi). Unresolved derivatives do not remove vector slots.
 */
ARCH_HOST_DEVICE constexpr CoordinateLayout resolve_coordinate_layout(
    Geometry geometry, int dimension)
{
    if(dimension<1||dimension>3)return {};
    CoordinateLayout result{};
    result.resolved_derivatives={true,dimension>=2,dimension==3};
    if(geometry==Geometry::Cartesian) {
        result.basis=CoordinateLayout::Basis::Cartesian;
    } else if(geometry==Geometry::Cylindrical) {
        result.basis=CoordinateLayout::Basis::CylindricalRZPhi;
        result.radial=0;result.axial=1;result.azimuth=2;
    } else if(geometry==Geometry::Spherical) {
        result.radial=0;
        if(dimension==2) {
            result.basis=CoordinateLayout::Basis::CylindricalRPhiZ;
            result.azimuth=1;result.axial=2;
        } else {
            result.basis=CoordinateLayout::Basis::SphericalRThetaPhi;
            result.theta=1;result.azimuth=2;
        }
    } else return {};
    result.valid=true;
    return result;
}

struct GeometryView {
    Geometry geometry = Geometry::Cartesian;
    int dim = 1;
    int ng = 0;
    int stride_y = 0;
    int stride_z = 0;
    int total_size = 0;
    double dx1 = 0.0;
    double dx2 = 0.0;
    double dx3 = 0.0;
    double x1_min = 0.0;
    double x2_min = 0.0;
    double x3_min = 0.0;

    ARCH_HOST_DEVICE int GetIndex(int i, int j = 0, int k = 0) const
    {
        return k * stride_z + j * stride_y + i;
    }

    ARCH_HOST_DEVICE double GetCellCenterX(int i) const
    {
        return x1_min + (i - ng) * dx1 + 0.5 * dx1;
    }

    ARCH_HOST_DEVICE double GetCellCenterY(int j) const
    {
        return dim < 2 ? 0.0 : x2_min + (j - ng) * dx2 + 0.5 * dx2;
    }

    ARCH_HOST_DEVICE double GetCellCenterZ(int k) const
    {
        return dim < 3 ? 0.0 : x3_min + (k - ng) * dx3 + 0.5 * dx3;
    }

    ARCH_HOST_DEVICE double GetFacePosL(int i) const
    {
        return x1_min + (i - ng) * dx1;
    }

    ARCH_HOST_DEVICE double GetFacePosR(int i) const
    {
        return x1_min + (i - ng + 1) * dx1;
    }

    /** Axial cell bounds in x2 for the ordinary band/curvilinear metrics. */
    ARCH_HOST_DEVICE double GetAxialFacePosL(int j) const
    {
        return x2_min + (j - ng) * dx2;
    }
    ARCH_HOST_DEVICE double GetAxialFacePosR(int j) const
    {
        return x2_min + (j - ng + 1) * dx2;
    }

    /** Cell length along one axis; dx1/dx2/dx3 stay representative. */
    ARCH_HOST_DEVICE double CellWidth(int axis,int index) const
    {
        return axis==0?dx1:axis==1?dx2:axis==2?dx3:
            std::numeric_limits<double>::quiet_NaN();
    }

    // Preserve Grid::GetPhysicalCoords' spherical 1D/equatorial-2D
    // specialization without constructing Cartesian/trigonometric coordinates.
    ARCH_HOST_DEVICE double SourceTheta(int j) const
    {
        constexpr double half_pi = arch::constants::math::pi / 2.0;
        return geometry == Geometry::Spherical && dim <= 2
            ? half_pi : GetCellCenterY(j);
    }
};

/** Report the coordinate kind of an existing allocation-free view. */
ARCH_HOST_DEVICE inline Geometry geometry_kind(const GeometryView& grid)
{
    return grid.geometry;
}

/** Embed a native sample in Cartesian coordinates, without assigning mass.
 * An axisymmetric (r,z) sample uses phi=0. Gravity owns the ring distribution;
 * this representative point alone cannot define an isolated gravity boundary.
 */
ARCH_HOST_DEVICE inline std::array<double,3> PhysicalPosition(
    Geometry geometry, int dimension, const std::array<double,3>& native) {
    if (geometry == Geometry::Cartesian) return native;
    const double r = native[0];
    if (dimension == 1) return {r,0.,0.};
    if (dimension == 2) {
        if (geometry == Geometry::Cylindrical)
            return {r, 0., native[1]};
        return {r*std::cos(native[1]), r*std::sin(native[1]), 0.};
    }
    if (geometry == Geometry::Cylindrical)
        return {r*std::cos(native[2]), r*std::sin(native[2]), native[1]};
    if (geometry == Geometry::Spherical)
        return {r*std::sin(native[1])*std::cos(native[2]),
                r*std::sin(native[1])*std::sin(native[2]), r*std::cos(native[1])};
    return native;
}


static_assert(std::is_standard_layout_v<GeometryView>);
static_assert(std::is_trivially_copyable_v<GeometryView>);

} // namespace GridMetrics
