/**
 * @file HydroSourceKernels.cuh
 * @brief Accumulate shared geometric and external-gravity sources on device.
 *
 * Read the stage state and add source increments to its borrowed delta buffer.
 * GeometricSources and ExternalGravitySource remain the mathematical owners;
 * the runtime supplies the stream, scratch lifetime and completion boundary.
 */

#pragma once

#include "GridGeometryAdapter.cuh"
#include "numerics/integrator/GeometricSources.h"
#include "physics/gravity/ExternalGravitySource.h"

namespace arch::cuda {
namespace detail {

template <typename EosView>
__global__ void hydro_source_kernel(
    DeviceStateView state, DeviceStateView delta, DeviceGridView grid,
    EosView eos, double dt, SpeciesWorkspaceView workspace,
    Physical::Gravity::ExternalGravityView gravity)
{
    const int lane = blockIdx.x * blockDim.x + threadIdx.x;
    SpeciesLaneScratch<1> scratch(workspace, lane);
    double* composition = scratch.array(0);
    for (int linear = lane; linear < grid.active_cell_count();
         linear += blockDim.x * gridDim.x) {
        const int ni = grid.ie - grid.is;
        const int nj = grid.je - grid.js;
        const int i = grid.is + linear % ni;
        const int j = grid.js + (linear / ni) % nj;
        const int cell = grid.active_cell(linear);
        for (int species = 0; species < state.n_species; ++species)
            composition[species] = state.species(species, cell);
        FluidVector value = delta.load(cell);
        TimeIntegration::add_geometric_source_cell(
            state.load(cell), state.n_species > 0 ? composition : nullptr,
            eos, make_grid_geometry_view(grid), i, j, dt, value);
        Physical::Gravity::add_external_gravity_source_cell(state.load(cell), gravity, dt, value);
        delta.store(cell, value);
    }
}

} // namespace detail

template <typename EosView>
cudaError_t launch_hydro_sources(
    DeviceStateView state, DeviceStateView delta, DeviceGridView grid,
    const EosView& eos, double dt, cudaStream_t stream,
    SpeciesWorkspaceView workspace = {}, Physical::Gravity::ExternalGravityView gravity = {})
{
    if (!valid_hydro_view(state) || !valid_hydro_view(delta)
        || !valid_species_workspace(workspace, state.n_species, 1)
        || !valid_hydro_grid(grid) || state.total_size != grid.total_size
        || delta.total_size != state.total_size || delta.n_species != state.n_species
        || make_grid_geometry_view(grid).geometry == GridMetrics::Geometry::Unsupported)
        return cudaErrorInvalidValue;
    if (grid.geometry == static_cast<int>(DeviceGeometry::Cartesian) && !gravity.enabled)
        return cudaSuccess;
    const int threads = detail::species_launch_threads(workspace);
    detail::hydro_source_kernel
        <<<detail::species_launch_blocks(grid.active_cell_count(), workspace), threads, 0, stream>>>(
            state, delta, grid, eos, dt, workspace, gravity);
    return cudaGetLastError();
}

} // namespace arch::cuda
