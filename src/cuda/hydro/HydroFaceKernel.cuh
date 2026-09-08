#pragma once

#include "HydroFluxPolicies.cuh"
#include "HydroReconstructionPolicies.cuh"
#include "numerics/reconstruction/AMRInterfaceStencil.h"

namespace arch::cuda
{
namespace detail
{
template <typename Reconstruction, typename EosView>
ARCH_INLINE void reconstruct_amr_face(
    DeviceStateView state, DeviceGridView grid, int direction,
    int i, int j, int k, int cell, int stride, const EosView& eos,
    FluidVector& left, FluidVector& right,
    double* species_left, double* species_right, double* species_cell)
{
    const int normal_index = direction == 0 ? i : (direction == 1 ? j : k);
    const int normal_begin = direction == 0 ? grid.is
        : (direction == 1 ? grid.js : grid.ks);
    const int normal_end = direction == 0 ? grid.ie
        : (direction == 1 ? grid.je : grid.ke);
    if (AMRInterfaceReconstruction::needs_tvd_interface_stencil(
            Reconstruction::ghost_depth, grid.amr_coarse_fine_face,
            direction, normal_index, normal_begin, normal_end)) {
        CudaMusclReconstruction<MinMod>::reconstruct(
            state, cell, stride, eos, left, right,
            species_left, species_right, species_cell);
        return;
    }
    Reconstruction::reconstruct(
        state, cell, stride, eos, left, right,
        species_left, species_right, species_cell);
}

template <typename Reconstruction, typename Flux, typename EosView>
__global__ void hydro_face_kernel(
    DeviceStateView state, DeviceStateView flux, DeviceGridView grid,
    EosView eos, int direction, double coefficient,
    SpeciesWorkspaceView workspace = {})
{
    int i_begin = grid.is;
    int j_begin = grid.js;
    int k_begin = grid.ks;
    if (direction == 0)
        --i_begin;
    else if (direction == 1)
        --j_begin;
    else
        --k_begin;
    const int ni = grid.ie - i_begin;
    const int nj = grid.je - j_begin;
    const int nk = grid.ke - k_begin;
    const int lane = blockIdx.x * blockDim.x + threadIdx.x;
    SpeciesLaneScratch<4> scratch(workspace, lane);
    double* species_left = scratch.array(0);
    double* species_right = scratch.array(1);
    double* species_cell = scratch.array(2);
    double* face_species_flux = scratch.array(3);
    for (int linear = lane; linear < ni * nj * nk;
         linear += blockDim.x * gridDim.x) {
        const int i = i_begin + linear % ni;
        const int j = j_begin + (linear / ni) % nj;
        const int k = k_begin + linear / (ni * nj);
        const int cell = grid.index(i, j, k);
        const int stride = grid.stride(direction);

        FluidVector left;
        FluidVector right;
        reconstruct_amr_face<Reconstruction>(
            state, grid, direction, i, j, k, cell, stride, eos, left, right,
            species_left, species_right, species_cell);
        FluidVector face_flux;
        Flux::compute(
            left, right, species_left, species_right, state.n_species, eos,
            direction, coefficient, face_flux, face_species_flux);
        const int face = cell + stride;
        flux.store(face, face_flux);
        for (int species = 0; species < state.n_species; ++species)
            flux.set_species(species, face, face_species_flux[species]);
    }
}
} // namespace detail

template <typename Reconstruction, typename Flux, typename EosView>
inline cudaError_t launch_hydro_faces(
    DeviceStateView state, DeviceStateView flux, DeviceGridView grid,
    const EosView& eos, int direction, double coefficient, cudaStream_t stream,
    SpeciesWorkspaceView workspace = {})
{
    const int directional_begin = direction == 0 ? grid.is
        : (direction == 1 ? grid.js : grid.ks);
    const int directional_end = direction == 0 ? grid.ie
        : (direction == 1 ? grid.je : grid.ke);
    const int directional_extent = direction == 0 ? grid.total_x
        : (direction == 1 ? grid.total_y : grid.total_z);
    if (!valid_hydro_view(state) || !valid_hydro_view(flux)
        || !valid_species_workspace(workspace, state.n_species, 4)
        || state.n_species != flux.n_species
        || state.total_size != flux.total_size
        || state.total_size != grid.total_size
        || !valid_hydro_grid(grid)
        || grid.ng < Reconstruction::ghost_depth
        || direction < 0 || direction >= grid.dim
        || directional_begin < Reconstruction::ghost_depth
        || directional_extent - directional_end
            < Reconstruction::ghost_depth)
        return cudaErrorInvalidValue;
    int ni = grid.ie - grid.is;
    int nj = grid.je - grid.js;
    int nk = grid.ke - grid.ks;
    if (direction == 0)
        ++ni;
    else if (direction == 1)
        ++nj;
    else
        ++nk;
    const int threads = detail::species_launch_threads(workspace);
    const int count = ni * nj * nk;
    if (count <= 0)
        return cudaSuccess;
    detail::hydro_face_kernel<Reconstruction, Flux>
        <<<detail::species_launch_blocks(count, workspace), threads, 0, stream>>>(
            state, flux, grid, eos, direction, coefficient, workspace);
    return cudaGetLastError();
}
} // namespace arch::cuda
