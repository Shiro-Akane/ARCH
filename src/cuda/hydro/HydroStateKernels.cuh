#pragma once

#include <cstddef>

#include "../common/CudaCommon.cuh"
#include "../../driver/DriverUtils.h"
#include "../../numerics/integrator/TimeIntegratorHelper.h"

namespace arch::cuda
{
namespace detail
{
template <typename EosView>
__global__ void hydro_cfl_candidates_kernel(
    DeviceStateView state, DeviceGridView grid, EosView eos,
    double* candidates)
{
    const int linear = blockIdx.x * blockDim.x + threadIdx.x;
    const int count = grid.active_cell_count();
    if (linear >= count)
        return;
    const int cell = grid.active_cell(linear);
    const FluidVector value = state.load(cell);
    if (!is_cfl_cell_active(value)) {
        candidates[linear] = cfl_inactive_cell_dt();
        return;
    }
    double composition[kMaxDeviceSpecies];
    for (int species = 0; species < state.n_species; ++species)
        composition[species] = state.species(species, cell);
    candidates[linear] = evaluate_cfl_cell_dt(
        value, state.n_species > 0 ? composition : nullptr, eos,
        grid.dim, grid.dx1, grid.dx2, grid.dx3);
}

static __global__ void hydro_cfl_reduce_kernel(
    const double* candidates, int count, double cfl, double* result)
{
    if (blockIdx.x != 0 || threadIdx.x != 0)
        return;
    double minimum = cfl_inactive_cell_dt();
    for (int cell = 0; cell < count; ++cell)
        minimum = combine_cfl_minimum(minimum, candidates[cell]);
    *result = finalize_cfl_dt(cfl, minimum);
}

static __global__ void hydro_divergence_kernel(
    DeviceStateView flux, DeviceStateView delta, DeviceGridView grid,
    double dt, int direction)
{
    const int linear = blockIdx.x * blockDim.x + threadIdx.x;
    if (linear >= grid.active_cell_count())
        return;
    const int cell = grid.active_cell(linear);
    const int stride = grid.stride(direction);
    FluidVector cell_delta = delta.load(cell);
    TimeIntegration::accumulate_cell_divergence(
        flux.load(cell), flux.load(cell + stride),
        flux.n_species > 0 ? flux.mass_fractions + cell : nullptr,
        flux.n_species > 0 ? flux.mass_fractions + cell + stride : nullptr,
        flux.n_species, flux.total_size,
        grid.face_area_lower[direction][cell],
        grid.face_area_upper[direction][cell], grid.cell_volume[cell], dt,
        cell_delta,
        delta.n_species > 0 ? delta.mass_fractions + cell : nullptr);
    delta.store(cell, cell_delta);
}
} // namespace detail

inline cudaError_t clear_hydro_buffer(
    DeviceStateView buffer, cudaStream_t stream)
{
    const std::size_t bytes = buffer.total_size * sizeof(double);
    cudaError_t error = cudaMemsetAsync(buffer.rho, 0, bytes, stream);
    if (error == cudaSuccess)
        error = cudaMemsetAsync(buffer.mom_u, 0, bytes, stream);
    if (error == cudaSuccess)
        error = cudaMemsetAsync(buffer.mom_v, 0, bytes, stream);
    if (error == cudaSuccess)
        error = cudaMemsetAsync(buffer.mom_w, 0, bytes, stream);
    if (error == cudaSuccess)
        error = cudaMemsetAsync(buffer.eng, 0, bytes, stream);
    if (error == cudaSuccess && buffer.n_species > 0)
        error = cudaMemsetAsync(
            buffer.mass_fractions, 0, buffer.n_species * bytes, stream);
    return error;
}

inline cudaError_t launch_hydro_divergence(
    DeviceStateView flux, DeviceStateView delta, DeviceGridView grid,
    double dt, int direction, cudaStream_t stream)
{
    if (direction < 0 || direction >= grid.dim)
        return cudaErrorInvalidValue;
    constexpr int threads = 128;
    const int count = grid.active_cell_count();
    if (count <= 0)
        return cudaSuccess;
    detail::hydro_divergence_kernel
        <<<(count + threads - 1) / threads, threads, 0, stream>>>(
            flux, delta, grid, dt, direction);
    return cudaGetLastError();
}

template <typename EosView>
inline cudaError_t launch_compute_hydro_dt(
    DeviceStateView state, DeviceGridView grid, const EosView& eos, double cfl,
    CudaHydroWorkspaceView workspace, cudaStream_t stream)
{
    if (state.n_species < 0 || state.n_species > kMaxDeviceSpecies)
        return cudaErrorInvalidValue;
    constexpr int threads = 128;
    const int count = grid.active_cell_count();
    if (count <= 0)
        return cudaErrorInvalidValue;
    detail::hydro_cfl_candidates_kernel
        <<<(count + threads - 1) / threads, threads, 0, stream>>>(
            state, grid, eos, workspace.cfl_candidates);
    cudaError_t error = cudaGetLastError();
    if (error != cudaSuccess)
        return error;
    detail::hydro_cfl_reduce_kernel<<<1, 1, 0, stream>>>(
        workspace.cfl_candidates, count, cfl, workspace.cfl_result);
    return cudaGetLastError();
}
} // namespace arch::cuda
