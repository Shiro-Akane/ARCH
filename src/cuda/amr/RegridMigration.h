/** CUDA memory executors for the Host-authoritative regrid migration plans. */
#pragma once

#include "amr/RegridTransferMath.h"
#include "cuda/common/CudaCommon.cuh"

#include <cuda_runtime.h>
#include <cstddef>

namespace arch::cuda {

struct DeviceRegridBlock {
    DeviceStateView state{};
    DeviceGridView grid{};
};

struct DeviceRegridChildren {
    // Geometric child order: bit 0/1/2 is the upper X/Y/Z half.
    DeviceRegridBlock blocks[8]{};
};

// Both operations write ONLY the staged destination interior. The caller
// owns migration-plan validation/grouping, device bindings, and a status int
// initialized to zero once per transaction. Kernels record the first shared
// regrid_math::Status failure; the caller must quiesce/check it before staged
// ghost completion or publication. Old active sources are never modified.
//
// Workspace is reusable across same-stream launches. Prolongation requires
// (destination active cells / 2^dim) * 17 * species_count doubles; restriction
// requires destination active cells * species_count doubles. Zero-species
// routes need no workspace. No fixed mathematical species ceiling is imposed.
cudaError_t launch_cuda_regrid_prolongation(
    DeviceRegridBlock source, DeviceRegridBlock destination, int child_index,
    double density_floor, double min_eint, double* workspace,
    std::size_t workspace_scalars, int* status, cudaStream_t stream);

cudaError_t launch_cuda_regrid_restriction(
    const DeviceRegridChildren& children, DeviceRegridBlock destination,
    double density_floor, double min_eint, double* workspace,
    std::size_t workspace_scalars, int* status, cudaStream_t stream);

} // namespace arch::cuda
