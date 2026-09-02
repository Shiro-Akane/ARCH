/**
 * @file CoarseFineExchangeKernels.cuh
 * @brief CUDA memory execution for Host-lowered coarse/fine ghost cells.
 */

#pragma once

#include "amr/ConservativeRestriction.h"
#include "cuda/common/DeviceStateFields.cuh"
#include "cuda/runtime/CudaBackendExchange.h"

#include <cuda_runtime.h>

#include <cstdint>
#include <limits>

namespace arch::cuda {

__global__ void gather_coarse_fine_exchange_kernel(
    const DeviceExchangeBlock* blocks,
    const DeviceCoarseFineTransfer* transfers, int field_count,
    std::uint64_t work_count, double* scratch)
{
    const std::uint64_t linear =
        static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (linear >= work_count) return;
    const int transfer_index = static_cast<int>(linear / field_count);
    const int field = static_cast<int>(linear % field_count);
    const DeviceCoarseFineTransfer& transfer = transfers[transfer_index];
    const DeviceStateView source = blocks[transfer.source_block].state;
    if (transfer.source_count == 1) {
        scratch[linear] =
            device_state_field(source, field)[transfer.source_cells[0]];
        return;
    }
    if (field < 6) {
        double integral = 0.0;
        for (int cell = 0; cell < transfer.source_count; ++cell)
            integral += amr::restriction_math::weighted_conserved_value(
                device_state_field(source, field)[transfer.source_cells[cell]],
                1.0);
        scratch[linear] = amr::restriction_math::restricted_average(
            integral, static_cast<double>(transfer.source_count));
        return;
    }

    double density_integral = 0.0;
    double species_density_integral = 0.0;
    for (int cell = 0; cell < transfer.source_count; ++cell) {
        const int source_cell = transfer.source_cells[cell];
        density_integral +=
            amr::restriction_math::weighted_conserved_value(
                source.rho[source_cell], 1.0);
        species_density_integral +=
            amr::restriction_math::weighted_species_density(
                source.rho[source_cell],
                device_state_field(source, field)[source_cell], 1.0);
    }
    scratch[linear] = amr::restriction_math::restricted_mass_fraction(
        species_density_integral, density_integral);
}

__global__ void scatter_coarse_fine_exchange_kernel(
    const DeviceExchangeBlock* blocks,
    const DeviceCoarseFineTransfer* transfers, int field_count,
    std::uint64_t work_count, const double* scratch)
{
    const std::uint64_t linear =
        static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (linear >= work_count) return;
    const int transfer_index = static_cast<int>(linear / field_count);
    const int field = static_cast<int>(linear % field_count);
    const DeviceCoarseFineTransfer& transfer = transfers[transfer_index];
    const DeviceStateView destination =
        blocks[transfer.destination_block].state;
    device_state_field(destination, field)[transfer.destination_cell] =
        scratch[linear];
}

inline cudaError_t launch_coarse_fine_exchange(
    const DeviceExchangeBlock* blocks,
    const DeviceCoarseFineTransfer* transfers, int transfer_count,
    int field_count, double* scratch, cudaStream_t stream)
{
    if (transfer_count == 0) return cudaSuccess;
    if (blocks == nullptr || transfers == nullptr || scratch == nullptr
        || transfer_count < 0 || field_count < 6)
        return cudaErrorInvalidValue;
    const std::uint64_t work = static_cast<std::uint64_t>(transfer_count)
        * static_cast<std::uint64_t>(field_count);
    constexpr int threads = 256;
    const std::uint64_t block_count = (work + threads - 1) / threads;
    if (work == 0
        || block_count > static_cast<std::uint64_t>(
            std::numeric_limits<unsigned int>::max()))
        return cudaErrorInvalidValue;
    gather_coarse_fine_exchange_kernel
        <<<static_cast<unsigned int>(block_count), threads, 0, stream>>>(
            blocks, transfers, field_count, work, scratch);
    cudaError_t result = cudaGetLastError();
    if (result != cudaSuccess) return result;
    scatter_coarse_fine_exchange_kernel
        <<<static_cast<unsigned int>(block_count), threads, 0, stream>>>(
            blocks, transfers, field_count, work, scratch);
    return cudaGetLastError();
}

} // namespace arch::cuda
