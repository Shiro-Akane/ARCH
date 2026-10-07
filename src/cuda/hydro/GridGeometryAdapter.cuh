/**
 * @file GridGeometryAdapter.cuh
 * @brief Adapt device layout metadata to the common geometry view.
 *
 * This value-only adapter owns no arrays and defines no metric formulas.
 * Workflow: recover the numeric geometry kind and the explicitly lowered
 * chart; copy actual endpoints and dyadic identity into the complete shared
 * view; let common canonical coordinate/measure leaves perform all arithmetic.
 * Neither a copied identity nor this adapter grants native backend capability.
 */

#pragma once

#include "cuda/common/CudaCommon.cuh"
#include "grid/GridMetrics.h"

namespace arch::cuda {

/** Restore the complete non-owning mathematical geometry from an owned POD.
 * The launcher authenticates its input; this allocation-free Host/device
 * adapter only copies values and never repairs or reinterprets a chart.
 */
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
            grid.x1_min, grid.x2_min, grid.x3_min, grid.semantics,
            {grid.x1_max, grid.x2_max}, grid.dyadic_identity};
}

} // namespace arch::cuda
