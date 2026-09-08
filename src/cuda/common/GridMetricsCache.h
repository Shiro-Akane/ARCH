#pragma once

#include "cuda/common/CudaCommon.cuh"

#include <cstddef>

namespace arch::cuda {

// Mutable bindings for the immutable geometry caches in DeviceGridView.
// Each nonoverlapping device array owns at least capacity doubles. The launch
// writes [0, grid.total_size), leaving any extra capacity untouched.
struct GridMetricsCacheView {
    double* cell_volume = nullptr;
    double* face_area_lower[3]{};
    double* face_area_upper[3]{};
    std::size_t capacity = 0;
};

// Initialize caches on the caller's stream with the shared GridMetrics leaves.
// Only active cells receive metrics; all ghost/padding slots and inactive-axis
// face areas are positive zero, matching the Host cache initialization.
//
// The caller supplies a grid from the shared, physical-domain-validated Grid
// authority and device allocations on the stream's device. This memory/setup
// executor checks layout, supported geometry, finite active coordinates and
// positive active spacing, and nonnull/aligned/nonoverlapping output ranges;
// it does not duplicate Grid's radial/angular domain policy or query allocation
// ownership. Inactive-coordinate values and existing const metric pointers in
// grid are ignored. Invalid arguments are rejected before any work is queued.
//
// This call does not synchronize. The caller must keep the arrays alive and
// check its stream fence before publishing or retiring their owner.
cudaError_t launch_cuda_grid_metrics_cache(
    DeviceGridView grid, GridMetricsCacheView cache, cudaStream_t stream);

} // namespace arch::cuda
