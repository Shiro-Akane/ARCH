#pragma once

#include "cuda/common/CudaCommon.cuh"
#include "grid/GridMetrics.h"

namespace arch::cuda {

// Backend-specific binding only: all metric and source arithmetic remains in
// GridMetrics and the common numerical helpers.
ARCH_HOST_DEVICE inline GridMetrics::GeometryView make_grid_geometry_view(
    const DeviceGridView& grid)
{
    const auto kind = grid.geometry == static_cast<int>(DeviceGeometry::Cartesian)
        ? GridMetrics::Geometry::Cartesian
        : grid.geometry == static_cast<int>(DeviceGeometry::Cylindrical)
            ? GridMetrics::Geometry::Cylindrical
            : grid.geometry == static_cast<int>(DeviceGeometry::Spherical)
                ? GridMetrics::Geometry::Spherical : GridMetrics::Geometry::Unsupported;
    return {kind, grid.dim, grid.ng, grid.stride_y, grid.stride_z,
            grid.total_size, grid.dx1, grid.dx2, grid.dx3,
            grid.x1_min, grid.x2_min, grid.x3_min};
}

} // namespace arch::cuda
