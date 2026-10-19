/**
 * @file HydroSourceKernels.cuh
 * @brief Accumulate shared geometric and external-gravity sources on device.
 *
 * Read the stage state and add source increments to its borrowed delta buffer.
 * GeometricSources and ExternalGravitySource remain the mathematical owners;
 * the runtime supplies the stream, scratch lifetime and completion boundary.
 * Workflow:
 * 1. Receive stage views, face geometry and device state arrays.
 * 2. Launch the shared hydro face, source or state work on CUDA.
 * 3. Publish stage output only after the backend stream orders writes.
 */

#pragma once

#include "cuda/hydro/GridGeometryAdapter.cuh"
#include "numerics/curvilinear/GeometricSources.h"
#include "physics/gravity/GravitySource.h"

namespace arch::cuda {
namespace detail {

/** Accumulate the common source on actual stage readers. The borrowed wall
 * view controls only native axisymmetric torque/work quadrature; retained
 * charts re-enter their original source arithmetic. An empty view denotes
 * open radial faces, not an inferred reflecting boundary. Production stages
 * borrow the flags authenticated by their Runtime topology/boundary owner.
 */
template <typename EosView>
__device__ inline void hydro_source_kernel_work(
    DeviceStateView state, DeviceStateView delta, DeviceGridView grid,
    EosView eos, double dt, SpeciesWorkspaceView workspace,
    Physical::Gravity::ExternalGravityView gravity,
    arch::boundary::HydroBoundaryView hydro_boundary = {})
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
        bool lower_open=true,upper_open=true;
        if(!hydro_boundary.advective_faces_open(0,i,grid.is,grid.ie,lower_open,upper_open)) {
            // The existing stage admissibility/status path rejects this NaN;
            // an invalid borrowed view must not become an open-face default.
            value.mom_u=arch::state::invalid();delta.store(cell,value);continue;
        }
        TimeIntegration::add_axisymmetric_work_source_cell(
            state.load(cell), state.n_species > 0 ? composition : nullptr,
            eos, make_grid_geometry_view(grid), i, j, dt,
            lower_open, upper_open, value);
        Physical::Gravity::add_external_gravity_source_cell(state.load(cell), gravity, dt, value);
        delta.store(cell, value);
    }
}

template <typename EosView>
__global__ void hydro_source_kernel(
    DeviceStateView state, DeviceStateView delta, DeviceGridView grid,
    EosView eos, double dt, SpeciesWorkspaceView workspace,
    Physical::Gravity::ExternalGravityView gravity,
    arch::boundary::HydroBoundaryView hydro_boundary = {})
{
    hydro_source_kernel_work(state, delta, grid, eos, dt, workspace, gravity,
        hydro_boundary);
}

} // namespace detail

template <typename EosView>
cudaError_t launch_hydro_sources(
    DeviceStateView state, DeviceStateView delta, DeviceGridView grid,
    const EosView& eos, double dt, cudaStream_t stream,
    SpeciesWorkspaceView workspace = {}, Physical::Gravity::ExternalGravityView gravity = {},
    arch::boundary::HydroBoundaryView hydro_boundary = {})
{
    if (!valid_hydro_view(state) || !valid_hydro_view(delta)
        || !valid_species_workspace(workspace, state.n_species, 1)
        || !valid_hydro_grid(grid)
        || state.total_size != grid.total_size
        || delta.total_size != state.total_size || delta.n_species != state.n_species
        || make_grid_geometry_view(grid).geometry == GridMetrics::Geometry::Unsupported)
        return cudaErrorInvalidValue;
    if (grid.geometry == static_cast<int>(DeviceGeometry::Cartesian) && !gravity.enabled)
        return cudaSuccess;
    const int threads = detail::species_launch_threads(workspace);
    detail::hydro_source_kernel
        <<<detail::species_launch_blocks(grid.active_cell_count(), workspace), threads, 0, stream>>>(
            state, delta, grid, eos, dt, workspace, gravity, hydro_boundary);
    return cudaGetLastError();
}

} // namespace arch::cuda
