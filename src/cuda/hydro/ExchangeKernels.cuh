/**
 * @file ExchangeKernels.cuh
 * @brief Bounded CUDA lowering for a compiled same-level exchange plan.
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
            field = static_cast<int>(local / cells);
            cell = local % cells;
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

inline cudaError_t launch_same_level_exchange_phase(
    const DeviceExchangeBlock* blocks,
    const DeviceExchangeOperation* operations, int operation_count,
    int field_count, std::uint64_t phase_cells, double* scratch,
    cudaStream_t stream)
{
    if (operation_count == 0) return cudaSuccess;
    if (blocks == nullptr || operations == nullptr || scratch == nullptr
        || field_count < 6 || phase_cells == 0)
        return cudaErrorInvalidValue;
    const std::uint64_t work = phase_cells * field_count;
    constexpr int threads = 256;
    const std::uint64_t block_count = (work + threads - 1) / threads;
    if (block_count > static_cast<std::uint64_t>(
            std::numeric_limits<unsigned int>::max()))
        return cudaErrorInvalidValue;
    gather_same_level_exchange_kernel
        <<<static_cast<unsigned int>(block_count), threads, 0, stream>>>(
            blocks, operations, operation_count, field_count, work, scratch);
    cudaError_t result = cudaGetLastError();
    if (result != cudaSuccess) return result;
    scatter_same_level_exchange_kernel
        <<<static_cast<unsigned int>(block_count), threads, 0, stream>>>(
            blocks, operations, operation_count, field_count, work, scratch);
    return cudaGetLastError();
}

} // namespace arch::cuda
