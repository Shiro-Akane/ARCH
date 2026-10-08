/**
 * @file GridGeometryView.h
 * @brief Allocation-free native-grid geometry shared by Host and device math.
 *
 * Workflow: borrow authenticated grid identity; resolve bound RZ coordinates
 * with the shared canonical face owner; derive actual midpoint/width from faces;
 * lend these values to measures and physical consumers. Ordinary/unbound local
 * formulas and chart interpretation retain their existing branches.
 */
#pragma once

#include "core/ArchPortability.h"
#include "grid/DyadicGridIdentity.h"
#include "physics/constant/PhysicalConstants.h"

#include <array>
#include <cmath>
#include <limits>
#include <type_traits>

namespace GridMetrics {

enum class Geometry : int {
    Cartesian = 0,
    Cylindrical = 1,
    Spherical = 2,
    Unsupported = 3,
};

/** Explicit storage chart: cylindrical 2D requires AxisymmetricRz.
 * Existing applies to Cartesian, spherical and cylindrical 1D/3D only.
 */
enum class GeometrySemantics : int { Existing = 0, AxisymmetricRz = 1 };

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
    GeometrySemantics semantics = GeometrySemantics::Existing;

    // Actual stored upper endpoints are used only to authenticate a bound
    // native generator; never recompute them from the already rounded dx.
    std::array<double,2> actual_block_upper{};
    DyadicGridIdentity dyadic_identity{};

    ARCH_HOST_DEVICE int GetIndex(int i, int j = 0, int k = 0) const
    {
        return k * stride_z + j * stride_y + i;
    }

    /** Bound face wrapper; an invalid chart/index returns nonfinite geometry,
     * never an ordinary-grid fallback. Both backends borrow the same leaf.
     */
    ARCH_HOST_DEVICE double CanonicalFace(int axis,std::int64_t local_face) const
    {
        double result=0.;
        if(geometry!=Geometry::Cylindrical||dim!=2||!GridMetrics::canonical_axis_face(dyadic_identity,axis,local_face,result))
            return std::numeric_limits<double>::quiet_NaN();
        return result;
    }

    /** Resolve one axial/radial cell endpoint with its represented cell owner.
     * Periodic axial ghosts select both source-domain endpoints together; the
     * upper face of the last alias cell stays root_upper. Descriptor faces use
     * CanonicalFace separately and never wrap their root/block identities.
     */
    ARCH_HOST_DEVICE double CanonicalCellFace(int axis,std::int64_t local_cell,bool upper) const
    {
        double left=0.,right=0.,middle=0.,width=0.;
        if(geometry!=Geometry::Cylindrical||dim!=2||!GridMetrics::canonical_axis_cell(dyadic_identity,axis,
            local_cell,left,right,middle,width))return std::numeric_limits<double>::quiet_NaN();
        return upper?right:left;
    }

    /** Bound cell width/center wrapper with actual face representability. */
    ARCH_HOST_DEVICE double CanonicalCellValue(int axis,std::int64_t local_cell,bool center) const
    {
        double left=0.,right=0.,middle=0.,width=0.;
        if(geometry!=Geometry::Cylindrical||dim!=2||!GridMetrics::canonical_axis_cell(dyadic_identity,axis,
            local_cell,left,right,middle,width))return std::numeric_limits<double>::quiet_NaN();
        return center?middle:width;
    }

    ARCH_HOST_DEVICE double GetCellCenterX(int i) const
    {
        if(dyadic_identity.bound)
            return CanonicalCellValue(0,std::int64_t(i)-ng,true);
        return x1_min + (i - ng) * dx1 + 0.5 * dx1;
    }

    ARCH_HOST_DEVICE double GetCellCenterY(int j) const
    {
        if(dyadic_identity.bound)
            return CanonicalCellValue(1,std::int64_t(j)-ng,true);
        return dim < 2 ? 0.0 : x2_min + (j - ng) * dx2 + 0.5 * dx2;
    }

    ARCH_HOST_DEVICE double GetCellCenterZ(int k) const
    {
        return dim < 3 ? 0.0 : x3_min + (k - ng) * dx3 + 0.5 * dx3;
    }

    ARCH_HOST_DEVICE double GetFacePosL(int i) const
    {
        if(dyadic_identity.bound)return CanonicalFace(0,std::int64_t(i)-ng);
        return x1_min + (i - ng) * dx1;
    }

    ARCH_HOST_DEVICE double GetFacePosR(int i) const
    {
        if(dyadic_identity.bound)return CanonicalFace(0,std::int64_t(i)-ng+1);
        return x1_min + (i - ng + 1) * dx1;
    }

    /** Axial CELL bounds in the same represented chart as center and width.
     * Paired-periodic ghosts are source-domain aliases; real descriptor faces
     * remain unwrapped. Unbound grids retain their original local formula.
     */
    ARCH_HOST_DEVICE double GetAxialFacePosL(int j) const
    {
        if(dyadic_identity.bound)return CanonicalCellFace(1,std::int64_t(j)-ng,false);
        return x2_min + (j - ng) * dx2;
    }
    ARCH_HOST_DEVICE double GetAxialFacePosR(int j) const
    {
        if(dyadic_identity.bound)return CanonicalCellFace(1,std::int64_t(j)-ng,true);
        return x2_min + (j - ng + 1) * dx2;
    }

    /** Actual cell length for finite-volume metrics; dx stays representative. */
    ARCH_HOST_DEVICE double CellWidth(int axis,int index) const
    {
        if(dyadic_identity.bound)
            return CanonicalCellValue(axis,std::int64_t(index)-ng,false);
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

/** Explicit view identity used by mathematical consumers during migration. */
ARCH_HOST_DEVICE inline bool is_axisymmetric_rz(const GeometryView& grid)
{
    return grid.semantics == GeometrySemantics::AxisymmetricRz;
}

ARCH_HOST_DEVICE inline Geometry geometry_kind(const GeometryView& grid)
{
    return grid.geometry;
}

/** Convert native coordinates to the Cartesian position used by gravity kernels. */
ARCH_HOST_DEVICE inline std::array<double,3> PhysicalPosition(
    Geometry geometry, int dimension, const std::array<double,3>& native) {
    if (geometry == Geometry::Cartesian) return native;
    const double r = native[0];
    if (dimension == 1) return {r,0.,0.};
    if (dimension == 2) {
        if (geometry == Geometry::Cylindrical) return {r,0.,native[1]};
        return {r*std::cos(native[1]), r*std::sin(native[1]), 0.};
    }
    if (geometry == Geometry::Cylindrical)
        return {r*std::cos(native[2]), r*std::sin(native[2]), native[1]};
    if (geometry == Geometry::Spherical)
        return {r*std::sin(native[1])*std::cos(native[2]),
                r*std::sin(native[1])*std::sin(native[2]), r*std::cos(native[1])};
    return native;
}


/** Representative Cartesian meridian position, not a point-source gravity model. */
ARCH_HOST_DEVICE inline std::array<double,3> PhysicalPosition(
    const GeometryView& grid, const std::array<double,3>& native)
{
    if (grid.semantics == GeometrySemantics::AxisymmetricRz)
        return {native[0], 0., native[1]};
    return PhysicalPosition(grid.geometry, grid.dim, native);
}

static_assert(std::is_standard_layout_v<GeometryView>);
static_assert(std::is_trivially_copyable_v<GeometryView>);

} // namespace GridMetrics
