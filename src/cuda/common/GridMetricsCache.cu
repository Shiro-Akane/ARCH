#include "cuda/common/GridMetricsCache.h"
#include "cuda/hydro/GridGeometryAdapter.cuh"

#include <cmath>
#include <cstdint>
#include <limits>

namespace arch::cuda {
namespace {

bool valid_cache_inputs(const DeviceGridView& grid, GridMetricsCacheView cache)
{
    if (!valid_hydro_grid(grid)
        || (grid.geometry != static_cast<int>(DeviceGeometry::Cartesian)
            && grid.geometry != static_cast<int>(DeviceGeometry::Cylindrical)
            && grid.geometry != static_cast<int>(DeviceGeometry::Spherical))
        || cache.capacity < static_cast<std::size_t>(grid.total_size))
        return false;

    const double spacing[]{grid.dx1, grid.dx2, grid.dx3};
    const double lower[]{grid.x1_min, grid.x2_min, grid.x3_min};
    const double upper[]{grid.x1_max, grid.x2_max, grid.x3_max};
    for (int axis = 0; axis < grid.dim; ++axis) {
        if (!std::isfinite(spacing[axis]) || spacing[axis] <= 0.0
            || !std::isfinite(lower[axis]) || !std::isfinite(upper[axis])
            || upper[axis] <= lower[axis])
            return false;
    }

    const double* pointers[]{cache.cell_volume,
        cache.face_area_lower[0], cache.face_area_lower[1],
        cache.face_area_lower[2], cache.face_area_upper[0],
        cache.face_area_upper[1], cache.face_area_upper[2]};
    constexpr auto address_max = std::numeric_limits<std::uintptr_t>::max();
    const auto count = static_cast<std::size_t>(grid.total_size);
    if (count > address_max / sizeof(double)) return false;
    const auto bytes = static_cast<std::uintptr_t>(count * sizeof(double));
    std::uintptr_t begins[7]{};
    for (int array = 0; array < 7; ++array) {
        begins[array] = reinterpret_cast<std::uintptr_t>(pointers[array]);
        if (pointers[array] == nullptr || begins[array] % alignof(double) != 0
            || begins[array] > address_max - bytes)
            return false;
        for (int previous = 0; previous < array; ++previous) {
            if (begins[array] < begins[previous] + bytes
                && begins[previous] < begins[array] + bytes)
                return false;
        }
    }
    return true;
}

__global__ void initialize_grid_metrics(
    DeviceGridView grid, GridMetricsCacheView cache)
{
    const auto geometry = make_grid_geometry_view(grid);
    const auto lane = static_cast<std::size_t>(blockIdx.x) * blockDim.x
        + threadIdx.x;
    const auto lanes = static_cast<std::size_t>(gridDim.x) * blockDim.x;
    for (std::size_t cell = lane;
         cell < static_cast<std::size_t>(grid.total_size); cell += lanes) {
        const int k = static_cast<int>(cell / grid.stride_z);
        const auto plane_cell = cell % grid.stride_z;
        const int j = static_cast<int>(plane_cell / grid.stride_y);
        const int i = static_cast<int>(plane_cell % grid.stride_y);
        double volume = 0.0;
        double lower[3]{};
        double upper[3]{};
        if (i >= grid.is && i < grid.ie
            && j >= grid.js && j < grid.je
            && k >= grid.ks && k < grid.ke) {
            volume = GridMetrics::CellVolume(geometry, i, j, k);
            for (int axis = 0; axis < grid.dim; ++axis) {
                lower[axis] = GridMetrics::FaceArea(
                    geometry, axis, i, j, k, false);
                upper[axis] = GridMetrics::FaceArea(
                    geometry, axis, i, j, k, true);
            }
        }
        cache.cell_volume[cell] = volume;
        for (int axis = 0; axis < 3; ++axis) {
            cache.face_area_lower[axis][cell] = lower[axis];
            cache.face_area_upper[axis][cell] = upper[axis];
        }
    }
}

} // namespace

cudaError_t launch_cuda_grid_metrics_cache(
    DeviceGridView grid, GridMetricsCacheView cache, cudaStream_t stream)
{
    if (!valid_cache_inputs(grid, cache)) return cudaErrorInvalidValue;

    // Ask the active device for this kernel's occupancy configuration, then
    // bound the launch to useful work. Grid-stride traversal covers any cache
    // size without a fixed thread count or an assumed device SM count.
    int minimum_grid = 0;
    int threads = 0;
    const auto occupancy = cudaOccupancyMaxPotentialBlockSize(
        &minimum_grid, &threads, initialize_grid_metrics, 0, 0);
    if (occupancy != cudaSuccess) return occupancy;
    if (minimum_grid <= 0 || threads <= 0) return cudaErrorInvalidConfiguration;
    const int requested_blocks = (grid.total_size - 1) / threads + 1;
    const int blocks = requested_blocks < minimum_grid
        ? requested_blocks : minimum_grid;
    initialize_grid_metrics<<<blocks, threads, 0, stream>>>(grid, cache);
    return cudaGetLastError();
}

} // namespace arch::cuda
