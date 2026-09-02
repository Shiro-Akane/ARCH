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
    EosView eos, int direction, double coefficient)
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
    int linear = blockIdx.x * blockDim.x + threadIdx.x;
    if (linear >= ni * nj * nk)
        return;
    const int i = i_begin + linear % ni;
    linear /= ni;
    const int j = j_begin + linear % nj;
    const int k = k_begin + linear / nj;
    const int cell = grid.index(i, j, k);
    const int stride = grid.stride(direction);

    double species_left[kMaxDeviceSpecies];
    double species_right[kMaxDeviceSpecies];
    double species_cell[kMaxDeviceSpecies];
    double face_species_flux[kMaxDeviceSpecies];
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
} // namespace detail

template <typename Reconstruction, typename Flux, typename EosView>
inline cudaError_t launch_hydro_faces(
    DeviceStateView state, DeviceStateView flux, DeviceGridView grid,
    const EosView& eos, int direction, double coefficient, cudaStream_t stream)
{
    const int directional_begin = direction == 0 ? grid.is
        : (direction == 1 ? grid.js : grid.ks);
    const int directional_end = direction == 0 ? grid.ie
        : (direction == 1 ? grid.je : grid.ke);
    const int directional_extent = direction == 0 ? grid.total_x
        : (direction == 1 ? grid.total_y : grid.total_z);
    if (!valid_hydro_view(state) || !valid_hydro_view(flux)
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
    constexpr int threads = 128;
    const int count = ni * nj * nk;
    if (count <= 0)
        return cudaSuccess;
    detail::hydro_face_kernel<Reconstruction, Flux>
        <<<detail::hydro_launch_blocks(count, threads), threads, 0, stream>>>(
            state, flux, grid, eos, direction, coefficient);
    return cudaGetLastError();
}
} // namespace arch::cuda
