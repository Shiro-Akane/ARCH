/**
 * @file ExchangeKernels.cuh
 * @brief Execute a compiled same-level exchange phase on CUDA.
 *
 * Gather snapshots source values into borrowed scratch before same-stream
 * scatter writes any destination. The host plan defines phase ordering and
 * the runtime owns buffers, completion and publication of ghost validity.
 * Workflow:
 * 1. Receive device-resident block state and boundary plans.
 * 2. Launch CUDA ghost and boundary transport for the active stage.
 * 3. Leave core hydro mathematics in the shared host/device policies.
 */

#pragma once

#include "cuda/runtime/amr/CudaBackendExchange.h"
#include "cuda/common/DeviceStateFields.cuh"

#include <cuda_runtime.h>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace arch::cuda {

__device__ inline bool locate_exchange_work(
    std::uint64_t linear, const DeviceExchangeOperation* operations,
    int operation_count, int field_count, int& operation_index,
    int& field, std::uint64_t& cell)
{
    for (int index = 0; index < operation_count; ++index) {
        const auto& operation = operations[index];
        const std::uint64_t cells =
            static_cast<std::uint64_t>(operation.extent[0])
            * operation.extent[1] * operation.extent[2];
        const std::uint64_t begin = operation.scratch_first * field_count;
        const std::uint64_t end = begin + cells * field_count;
        if (linear >= begin && linear < end) {
            operation_index = index;
            const std::uint64_t local = linear - begin;
            // Match GhostExchange's existing cell-major MPI payload:
            // rho, momentum, energy, enuc, then species for each cell.
            field = static_cast<int>(local % field_count);
            cell = local / field_count;
            return true;
        }
    }
    return false;
}

__device__ inline int exchange_cell_index(
    const DeviceExchangeBlock& block, const int first[3],
    const unsigned int extent[3], std::uint64_t linear)
{
    const int i = first[0] + static_cast<int>(linear % extent[0]);
    linear /= extent[0];
    const int j = first[1] + static_cast<int>(linear % extent[1]);
    const int k = first[2] + static_cast<int>(linear / extent[1]);
    return block.grid.index(
        block.grid.is + i, block.grid.js + j, block.grid.ks + k);
}

__global__ void gather_same_level_exchange_kernel(
    const DeviceExchangeBlock* blocks,
    const DeviceExchangeOperation* operations, int operation_count,
    int field_count, std::uint64_t work_count, double* scratch)
{
    const std::uint64_t linear =
        static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (linear >= work_count) return;
    int operation_index = 0;
    int field = 0;
    std::uint64_t cell = 0;
    if (!locate_exchange_work(
            linear, operations, operation_count, field_count,
            operation_index, field, cell))
        return;
    const auto& operation = operations[operation_index];
    const auto& source = blocks[operation.source_block];
    const int source_cell = exchange_cell_index(
        source, operation.source_first, operation.extent, cell);
    scratch[linear] = device_state_field(source.state, field)[source_cell];
}

__global__ void scatter_same_level_exchange_kernel(
    const DeviceExchangeBlock* blocks,
    const DeviceExchangeOperation* operations, int operation_count,
    int field_count, std::uint64_t work_count, const double* scratch)
{
    const std::uint64_t linear =
        static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (linear >= work_count) return;
    int operation_index = 0;
    int field = 0;
    std::uint64_t cell = 0;
    if (!locate_exchange_work(
            linear, operations, operation_count, field_count,
            operation_index, field, cell))
        return;
    const auto& operation = operations[operation_index];
    const auto& destination = blocks[operation.destination_block];
    const int destination_cell = exchange_cell_index(
        destination, operation.destination_first, operation.extent, cell);
    device_state_field(destination.state, field)[destination_cell] =
        scratch[linear];
}

// Both local exchange and Host-staged MPI use these same gather/scatter
// kernels. The caller validates the immutable lowering and owns storage.
// Gather only dereferences source_block; scatter only destination_block.
// The other endpoint may therefore be absent from this rank's device store.
inline cudaError_t same_level_exchange_launch_extent(
    const DeviceExchangeBlock* blocks,
    const DeviceExchangeOperation* operations, int operation_count,
    int field_count, std::uint64_t phase_cells, const double* scratch,
    std::uint64_t& work, unsigned int& block_count)
{
    work = 0;
    block_count = 0;
    if (operation_count < 0) return cudaErrorInvalidValue;
    if (operation_count == 0)
        return phase_cells == 0 ? cudaSuccess : cudaErrorInvalidValue;
    if (!blocks || !operations || !scratch || field_count < 6
        || phase_cells == 0
        || phase_cells > std::numeric_limits<std::uint64_t>::max()
            / static_cast<std::uint64_t>(field_count))
        return cudaErrorInvalidValue;
    work = phase_cells * static_cast<std::uint64_t>(field_count);
    constexpr unsigned int threads = 256;
    const std::uint64_t count = (work - 1) / threads + 1;
    if (count > std::numeric_limits<unsigned int>::max())
        return cudaErrorInvalidValue;
    block_count = static_cast<unsigned int>(count);
    return cudaSuccess;
}

/** Queue packing only. D2H must complete before MPI reads the Host buffer. */
inline cudaError_t launch_same_level_exchange_gather(
    const DeviceExchangeBlock* blocks,
    const DeviceExchangeOperation* operations, int operation_count,
    int field_count, std::uint64_t phase_cells, double* scratch,
    cudaStream_t stream)
{
    std::uint64_t work = 0;
    unsigned int block_count = 0;
    const auto status = same_level_exchange_launch_extent(
        blocks, operations, operation_count, field_count, phase_cells,
        scratch, work, block_count);
    if (status != cudaSuccess || block_count == 0) return status;
    gather_same_level_exchange_kernel<<<block_count, 256, 0, stream>>>(
        blocks, operations, operation_count, field_count, work, scratch);
    return cudaGetLastError();
}

/** Queue unpacking only, after MPI completion and same-stream H2D.
 * The owner must finish this stream before publishing valid ghosts or
 * reusing/releasing any borrowed buffer or block view.
 */
inline cudaError_t launch_same_level_exchange_scatter(
    const DeviceExchangeBlock* blocks,
    const DeviceExchangeOperation* operations, int operation_count,
    int field_count, std::uint64_t phase_cells, const double* scratch,
    cudaStream_t stream)
{
    std::uint64_t work = 0;
    unsigned int block_count = 0;
    const auto status = same_level_exchange_launch_extent(
        blocks, operations, operation_count, field_count, phase_cells,
        scratch, work, block_count);
    if (status != cudaSuccess || block_count == 0) return status;
    scatter_same_level_exchange_kernel<<<block_count, 256, 0, stream>>>(
        blocks, operations, operation_count, field_count, work, scratch);
    return cudaGetLastError();
}

inline cudaError_t launch_same_level_exchange_phase(
    const DeviceExchangeBlock* blocks,
    const DeviceExchangeOperation* operations, int operation_count,
    int field_count, std::uint64_t phase_cells, double* scratch,
    cudaStream_t stream)
{
    const auto result = launch_same_level_exchange_gather(
        blocks, operations, operation_count, field_count, phase_cells,
        scratch, stream);
    if (result != cudaSuccess) return result;
    return launch_same_level_exchange_scatter(
        blocks, operations, operation_count, field_count, phase_cells,
        scratch, stream);
}

} // namespace arch::cuda
