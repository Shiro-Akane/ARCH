/**
 * @file CoarseFineExchangeKernels.cuh
 * @brief CUDA memory execution for Host-lowered coarse/fine ghost cells.
 */

#pragma once

#include "amr/ConservativeRestriction.h"
#include "amr/LimitedLinearProlongation.h"
#include "cuda/common/DeviceStateFields.cuh"
#include "cuda/runtime/CudaBackendExchange.h"

#include <cuda_runtime.h>

#include <cstdint>
#include <limits>

namespace arch::cuda {

__device__ inline double prolong_device_field(
    const double* values, const DeviceCoarseFineTransfer& transfer)
{
    double lower[3]{};
    double upper[3]{};
    for (int axis = 0; axis < transfer.prolongation_dimension; ++axis) {
        lower[axis] = values[transfer.slope_cells[2 * axis]];
        upper[axis] = values[transfer.slope_cells[2 * axis + 1]];
    }
    return amr::prolongation_math::limited_linear_value(
        values[transfer.source_cells[0]], lower, upper,
        transfer.fine_position, transfer.prolongation_dimension);
}

__device__ inline double prolong_device_species_density(
    DeviceStateView source, const DeviceCoarseFineTransfer& transfer,
    int species)
{
    const double* fractions = device_state_field(source, 6 + species);
    double lower[3]{};
    double upper[3]{};
    for (int axis = 0; axis < transfer.prolongation_dimension; ++axis) {
        const int lower_cell = transfer.slope_cells[2 * axis];
        const int upper_cell = transfer.slope_cells[2 * axis + 1];
        lower[axis] = source.rho[lower_cell] * fractions[lower_cell];
        upper[axis] = source.rho[upper_cell] * fractions[upper_cell];
    }
    const int center = transfer.source_cells[0];
    return amr::prolongation_math::limited_linear_value(
        source.rho[center] * fractions[center], lower, upper,
        transfer.fine_position, transfer.prolongation_dimension);
}

__device__ inline bool prolong_device_composition_is_admissible(
    DeviceStateView source, const DeviceCoarseFineTransfer& transfer,
    int species_count)
{
    const int sibling_count = 1 << transfer.prolongation_dimension;
    DeviceCoarseFineTransfer sibling_transfer = transfer;
    for (int sibling = 0; sibling < sibling_count; ++sibling) {
        for (int axis = 0; axis < transfer.prolongation_dimension; ++axis)
            sibling_transfer.fine_position[axis] =
                (sibling & (1 << axis)) != 0 ? 0.25 : -0.25;
        const double density = prolong_device_field(
            source.rho, sibling_transfer);
        bool admissible = amr::prolongation_math::finite_number(density)
            && density > 0.0;
        double partial = 0.0;
        for (int species = 0; species + 1 < species_count && admissible;
             ++species) {
            const double candidate = prolong_device_species_density(
                source, sibling_transfer, species);
            admissible = amr::prolongation_math::finite_number(candidate)
                && candidate >= 0.0;
            partial += candidate;
        }
        const double closure = density - partial;
        if (!admissible
            || !amr::prolongation_math::finite_number(closure)
            || closure < 0.0)
            return false;
    }
    return true;
}

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
    if (transfer.prolongation_dimension != 0) {
        if (field < 6) {
            scratch[linear] = prolong_device_field(
                device_state_field(source, field), transfer);
            return;
        }
        const double density = prolong_device_field(source.rho, transfer);
        const int species_count = field_count - 6;
        const int species = field - 6;
        const bool admissible =
            prolong_device_composition_is_admissible(
                source, transfer, species_count);
        double species_density = 0.0;
        if (species + 1 < species_count) {
            species_density = prolong_device_species_density(
                source, transfer, species);
        } else {
            species_density = density;
            for (int component = 0; component + 1 < species_count;
                 ++component)
                species_density -= prolong_device_species_density(
                    source, transfer, component);
        }
        scratch[linear] = admissible
            ? species_density / density
            : device_state_field(source, field)[transfer.source_cells[0]];
        return;
    }
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
