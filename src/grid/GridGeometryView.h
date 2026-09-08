/**
 * @file GridGeometryView.h
 * @brief Allocation-free native-grid geometry shared by Host and device math.
 */
#pragma once

#include "../core/ArchPortability.h"
#include "../physics/constant/PhysicalConstants.h"

#include <type_traits>

namespace GridMetrics {

enum class Geometry : int {
    Cartesian = 0,
    Cylindrical = 1,
    Spherical = 2,
    Unsupported = 3,
};

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

    // Preserve Grid::GetPhysicalCoords' spherical 1D/equatorial-2D
    // specialization without constructing Cartesian/trigonometric coordinates.
    ARCH_HOST_DEVICE double SourceTheta(int j) const
    {
        constexpr double half_pi = arch::constants::math::pi / 2.0;
        return geometry == Geometry::Spherical && dim <= 2
            ? half_pi : GetCellCenterY(j);
    }
};

ARCH_HOST_DEVICE inline Geometry geometry_kind(const GeometryView& grid)
{
    return grid.geometry;
}

static_assert(std::is_standard_layout_v<GeometryView>);
static_assert(std::is_trivially_copyable_v<GeometryView>);

} // namespace GridMetrics
