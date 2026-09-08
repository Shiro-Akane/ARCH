/**
 * @file CoarseFineExchangeKernels.cuh
 * @brief Execute host-planned coarse/fine ghost transfers on CUDA.
 *
 * Gather evaluates the common conservative restriction or limited-linear
 * prolongation rules into borrowed scratch. A same-stream scatter writes
 * destinations only after a successful gather; runtime control checks the
 * completed status before publishing ghost validity.
 */

#pragma once

#include "amr/ConservativeRestriction.h"
#include "amr/LimitedLinearProlongation.h"
#include "cuda/common/DeviceStateFields.cuh"
#include "cuda/runtime/amr/CudaBackendExchange.h"

#include <cuda_runtime.h>

#include <cstdint>
#include <limits>

namespace arch::cuda {

__device__ inline amr::prolongation_math::CompositionStencilView
prolong_device_stencil(
    DeviceStateView source, const DeviceCoarseFineTransfer& transfer,
    int species_count)
{
    return {source.rho, source.mass_fractions,
            static_cast<std::size_t>(source.total_size),
            transfer.source_cells[0], transfer.slope_cells,
            transfer.prolongation_dimension, species_count};
}

__global__ void gather_coarse_fine_exchange_kernel(
    const DeviceExchangeBlock* blocks,
    const DeviceCoarseFineTransfer* transfers, int field_count,
    std::uint64_t work_count, double* scratch, int* status)
{
    const std::uint64_t linear =
        static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (linear >= work_count) return;
    const int transfer_index = static_cast<int>(linear / field_count);
    const int field = static_cast<int>(linear % field_count);
    const DeviceCoarseFineTransfer& transfer = transfers[transfer_index];
    const DeviceStateView source = blocks[transfer.source_block].state;
    if (transfer.prolongation_dimension != 0) {
        const auto stencil = prolong_device_stencil(
            source, transfer, field_count - 6);
        if (field > 0 && field < 6) {
            scratch[linear] = amr::prolongation_math::reconstruct_field(
                stencil, device_state_field(source, field), transfer.fine_position);
            return;
        }
        const auto family =
            amr::prolongation_math::classify_composition_family(stencil);
        if (family == amr::prolongation_math::CompositionFamily::InvalidDensity) {
            atomicExch(status, static_cast<int>(family));
            return;
        }
        const double density = amr::prolongation_math::reconstruct_field(
            stencil, source.rho, transfer.fine_position);
        scratch[linear] = field == 0 ? density
            : amr::prolongation_math::reconstruct_mass_fraction(
                stencil, family, density, field - 6, transfer.fine_position);
        return;
    }
    if (field < 6) {
        double integral = 0.0;
        for (int cell = 0; cell < transfer.source_count; ++cell)
            integral += amr::restriction_math::weighted_conserved_value(
                device_state_field(source, field)[transfer.source_cells[cell]],
                transfer.source_measures[cell]);
        scratch[linear] = amr::restriction_math::restricted_average(
            integral, transfer.source_measure_sum);
        return;
    }

    double density_integral = 0.0;
    double species_density_integral = 0.0;
    for (int cell = 0; cell < transfer.source_count; ++cell) {
        const int source_cell = transfer.source_cells[cell];
        density_integral +=
            amr::restriction_math::weighted_conserved_value(
                source.rho[source_cell], transfer.source_measures[cell]);
        species_density_integral +=
            amr::restriction_math::weighted_species_density(
                source.rho[source_cell],
                device_state_field(source, field)[source_cell], transfer.source_measures[cell]);
    }
    scratch[linear] = amr::restriction_math::restricted_mass_fraction(
        species_density_integral, density_integral);
}

__global__ void scatter_coarse_fine_exchange_kernel(
    const DeviceExchangeBlock* blocks,
    const DeviceCoarseFineTransfer* transfers, int field_count,
    std::uint64_t work_count, const double* scratch, const int* status)
{
    // The preceding gather is complete on this stream.  Reject the entire
    // plan before any destination write, just as the Host gather/throw path.
    if (*status != 0) return;
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
    int field_count, double* scratch, int* status, cudaStream_t stream)
{
    if (transfer_count == 0) return cudaSuccess;
    if (blocks == nullptr || transfers == nullptr || scratch == nullptr
        || status == nullptr
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
    cudaError_t result = cudaMemsetAsync(status, 0, sizeof(int), stream);
    if (result != cudaSuccess) return result;
    gather_coarse_fine_exchange_kernel
        <<<static_cast<unsigned int>(block_count), threads, 0, stream>>>(
            blocks, transfers, field_count, work, scratch, status);
    result = cudaGetLastError();
    if (result != cudaSuccess) return result;
    scatter_coarse_fine_exchange_kernel
        <<<static_cast<unsigned int>(block_count), threads, 0, stream>>>(
            blocks, transfers, field_count, work, scratch, status);
    return cudaGetLastError();
}

} // namespace arch::cuda
