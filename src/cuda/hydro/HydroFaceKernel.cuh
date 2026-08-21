#pragma once

#include "HydroFluxPolicies.cuh"
#include "HydroReconstructionPolicies.cuh"

namespace arch::cuda
{
namespace detail
{
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
    Reconstruction::reconstruct(
        state, cell, stride, eos, left, right,
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
    if (state.n_species < 0 || state.n_species > kMaxDeviceSpecies
        || direction < 0 || direction >= grid.dim)
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
        <<<(count + threads - 1) / threads, threads, 0, stream>>>(
            state, flux, grid, eos, direction, coefficient);
    return cudaGetLastError();
}
} // namespace arch::cuda
