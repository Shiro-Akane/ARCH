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
    const double* candidates, int count, double cfl, double* result,
    int* status)
{
    if (blockIdx.x != 0 || threadIdx.x != 0)
        return;
    auto spec = arch::reduction::minimum_spec(
        cfl_inactive_cell_dt());
    // Host tabular free-energy evaluation throws on an invalid
    // thermodynamic state.  Device views cannot throw, so they return NaNs;
    // the CFL boundary must convert that sentinel back into a checked backend
    // failure rather than ignoring the offending cell.
    spec.nan = arch::reduction::ReductionNanPolicy::Error;
    auto state = arch::reduction::begin_reduction(spec);
    for (int cell = 0; cell < count; ++cell) {
        amr::CellLogicalKey key{};
        key.logical_i = cell;
        arch::reduction::combine_candidate(
            spec, state, {candidates[cell], key, true});
    }
    const auto reduced = arch::reduction::finalize_reduction(spec, state);
    *status = static_cast<int>(reduced.status);
    *result = reduced.status == arch::reduction::ReductionStatus::Ok
        ? finalize_cfl_dt(cfl, reduced.value)
        : std::numeric_limits<double>::quiet_NaN();
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
    const int directional_end = direction == 0 ? grid.ie
        : (direction == 1 ? grid.je : grid.ke);
    const int directional_extent = direction == 0 ? grid.total_x
        : (direction == 1 ? grid.total_y : grid.total_z);
    if (!valid_hydro_view(flux) || !valid_hydro_view(delta)
        || flux.n_species != delta.n_species
        || flux.total_size != delta.total_size
        || flux.total_size != grid.total_size
        || !valid_hydro_grid(grid)
        || direction < 0 || direction >= grid.dim
        || directional_end >= directional_extent
        || grid.cell_volume == nullptr
        || grid.face_area_lower[direction] == nullptr
        || grid.face_area_upper[direction] == nullptr)
        return cudaErrorInvalidValue;
    constexpr int threads = 128;
    const int count = grid.active_cell_count();
    if (count <= 0)
        return cudaSuccess;
    detail::hydro_divergence_kernel
        <<<detail::hydro_launch_blocks(count, threads), threads, 0, stream>>>(
            flux, delta, grid, dt, direction);
    return cudaGetLastError();
}

template <typename EosView>
inline cudaError_t launch_compute_hydro_dt(
    DeviceStateView state, DeviceGridView grid, const EosView& eos, double cfl,
    CudaHydroWorkspaceView workspace, cudaStream_t stream)
{
    if (!valid_hydro_view(state) || !valid_hydro_grid(grid)
        || state.total_size != grid.total_size
        || workspace.cfl_candidates == nullptr
        || workspace.cfl_result == nullptr
        || workspace.cfl_status == nullptr)
        return cudaErrorInvalidValue;
    constexpr int threads = 128;
    const int count = grid.active_cell_count();
    if (count <= 0)
        return cudaErrorInvalidValue;
    detail::hydro_cfl_candidates_kernel
        <<<detail::hydro_launch_blocks(count, threads), threads, 0, stream>>>(
            state, grid, eos, workspace.cfl_candidates);
    cudaError_t error = cudaGetLastError();
    if (error != cudaSuccess)
        return error;
    detail::hydro_cfl_reduce_kernel<<<1, 1, 0, stream>>>(
        workspace.cfl_candidates, count, cfl, workspace.cfl_result,
        workspace.cfl_status);
    return cudaGetLastError();
}
} // namespace arch::cuda
