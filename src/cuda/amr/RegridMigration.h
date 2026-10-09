/**
 * @file RegridMigration.h
 * @brief Device transfer interface for host-authoritative regrid plans.
 *
 * State, geometry and scratch pointers are borrowed from the transaction owner.
 * Workflow: authenticate borrowed chart/layout and actual parent/child identity;
 * gather immutable old-state families; invoke the existing shared ordinary or
 * native RZ transfer; write provisional destination interiors and sticky status.
 * RegridTransferMath and NativeRzRegridTransfer supply all numerical operations.
 * Actual completed ghosts/EOS and topology publication belong to the caller.
 */
#pragma once

#include "amr/transfer/RegridTransferMath.h"
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
    state::Bounds bounds, double* workspace,
    std::size_t workspace_scalars, int* status, cudaStream_t stream);

// No enqueue: share the complete restriction layout/workspace preflight with
// callers that must validate before enqueueing metric/setup work.
cudaError_t validate_cuda_regrid_restriction(
    const DeviceRegridChildren& children, DeviceRegridBlock destination,
    state::Bounds bounds, double* workspace,
    std::size_t workspace_scalars, int* status);

cudaError_t launch_cuda_regrid_restriction(
    const DeviceRegridChildren& children, DeviceRegridBlock destination,
    state::Bounds bounds, double* workspace,
    std::size_t workspace_scalars, int* status, cudaStream_t stream);

} // namespace arch::cuda
