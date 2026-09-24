/**
 * @file HydroStageKernels.cuh
 * @brief Apply one shared hydro stage update to device interiors.
 *
 * The scheduler supplies old/current states and stage weights. The kernel calls
 * TimeIntegration::update_stage_cell for fluid and species values; it does not
 * rotate logical slots or publish ghosts. Runtime control owns those transitions.
 * Workflow:
 * 1. Receive stage views, face geometry and device state arrays.
 * 2. Launch the shared hydro face, source or state work on CUDA.
 * 3. Publish stage output only after the backend stream orders writes.
 */

#pragma once

#include "cuda/hydro/kernels/HydroFaceKernel.cuh"
#include "cuda/hydro/kernels/HydroStateKernels.cuh"

namespace arch::cuda
{
namespace detail
{
static __device__ inline void hydro_single_stage_update_kernel_work(
    DeviceStateView old_state, DeviceStateView current_state,
    DeviceStateView destination, DeviceStateView delta, DeviceGridView grid,
    double old_weight, double flux_weight, double density_floor,
    double minimum_internal_energy, double maximum_internal_energy,
    int* status = nullptr, state::RepairView repairs = {})
{
    const int linear = blockIdx.x * blockDim.x + threadIdx.x;
    if (linear >= grid.active_cell_count())
        return;
    const int cell = grid.active_cell(linear);
    FluidVector updated;
    const auto accepted = TimeIntegration::update_stage_cell(
        old_state.load(cell), current_state.load(cell), delta.load(cell),
        old_state.n_species > 0 ? old_state.mass_fractions + cell : nullptr,
        current_state.n_species > 0 ? current_state.mass_fractions + cell : nullptr,
        delta.n_species > 0 ? delta.mass_fractions + cell : nullptr,
        old_state.n_species, old_state.total_size,
        old_weight, flux_weight, density_floor, minimum_internal_energy,
        maximum_internal_energy,
        updated,
        destination.n_species > 0 ? destination.mass_fractions + cell : nullptr,
        repairs, GridMetrics::CellVolume(make_grid_geometry_view(grid),
            cell % grid.stride_y, (cell % grid.stride_z) / grid.stride_y, cell / grid.stride_z), cell);
    if (!state::accepted(accepted) && status) atomicExch(status, 100 + static_cast<int>(accepted));
    destination.store(cell, updated);
}


static __global__ void hydro_single_stage_update_kernel(
    DeviceStateView old_state, DeviceStateView current_state,
    DeviceStateView destination, DeviceStateView delta, DeviceGridView grid,
    double old_weight, double flux_weight, double density_floor,
    double minimum_internal_energy, double maximum_internal_energy,
    int* status = nullptr, state::RepairView repairs = {})
{
    hydro_single_stage_update_kernel_work(old_state, current_state, destination, delta, grid, old_weight, flux_weight, density_floor, minimum_internal_energy, maximum_internal_energy, status, repairs);
}
} // namespace detail

inline cudaError_t launch_hydro_single_stage_update(
    DeviceStateView old_state, DeviceStateView current_state,
    DeviceStateView destination, DeviceStateView delta, DeviceGridView grid,
    double old_weight, double flux_weight, double density_floor,
    double minimum_internal_energy, double maximum_internal_energy,
    cudaStream_t stream, int* status = nullptr, state::RepairView repairs = {})
{
    if (!valid_hydro_view(old_state)
        || !valid_hydro_view(current_state)
        || !valid_hydro_view(destination)
        || !valid_hydro_view(delta)
        || !valid_hydro_grid(grid)
        || old_state.n_species != current_state.n_species
        || old_state.n_species != destination.n_species
        || old_state.n_species != delta.n_species
        || old_state.total_size != current_state.total_size
        || old_state.total_size != destination.total_size
        || old_state.total_size != delta.total_size
        || old_state.total_size != grid.total_size)
        return cudaErrorInvalidValue;
    constexpr int threads = 128;
    const int count = grid.active_cell_count();
    if (count <= 0)
        return cudaSuccess;
    detail::hydro_single_stage_update_kernel
        <<<detail::hydro_launch_blocks(count, threads), threads, 0, stream>>>(
            old_state, current_state, destination, delta, grid,
            old_weight, flux_weight, density_floor, minimum_internal_energy,
            maximum_internal_energy, status, repairs);
    return cudaGetLastError();
}
} // namespace arch::cuda
