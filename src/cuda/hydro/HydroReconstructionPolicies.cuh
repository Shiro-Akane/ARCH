#pragma once

#include "../common/CudaCommon.cuh"
#include "../../numerics/reconstruction/Reconstruction.h"

namespace arch::cuda
{
struct CudaPcmReconstruction
{
    static constexpr int ghost_depth = 1;

    template <typename EosView>
    static ARCH_INLINE void reconstruct(
        DeviceStateView state, int cell, int stride, const EosView&,
        FluidVector& left, FluidVector& right,
        double* species_left, double* species_right, double*)
    {
        PCMReconstruction::reconstruct(
            state.load(cell), state.load(cell + stride), left, right);
        for (int species = 0; species < state.n_species; ++species) {
            PCMReconstruction::reconstruct_species(
                state.species(species, cell),
                state.species(species, cell + stride),
                species_left[species], species_right[species]);
        }
    }
};

template <typename Limiter>
struct CudaMusclReconstruction
{
    static constexpr int ghost_depth = 2;

    template <typename EosView>
    static ARCH_INLINE void reconstruct(
        DeviceStateView state, int cell, int stride, const EosView&,
        FluidVector& left, FluidVector& right,
        double* species_left, double* species_right, double*)
    {
        const FluidVector previous = state.load(cell - stride);
        const FluidVector center = state.load(cell);
        const FluidVector next = state.load(cell + stride);
        const FluidVector following = state.load(cell + 2 * stride);
        MusclReconstruction<Limiter>::reconstruct(
            previous, center, next, following, left, right);
        for (int species = 0; species < state.n_species; ++species) {
            MusclReconstruction<Limiter>::reconstruct_species(
                state.species(species, cell - stride),
                state.species(species, cell),
                state.species(species, cell + stride),
                state.species(species, cell + 2 * stride),
                species_left[species], species_right[species]);
        }
    }
};

struct CudaPpmReconstruction
{
    static constexpr int ghost_depth = 3;

    template <typename EosView>
    static ARCH_INLINE void reconstruct(
        DeviceStateView state, int cell, int stride, const EosView& eos,
        FluidVector& left, FluidVector& right,
        double* species_left, double* species_right, double* species_cell)
    {
        const int cells[6] = {
            cell - 2 * stride, cell - stride, cell,
            cell + stride, cell + 2 * stride, cell + 3 * stride};
        double rho[6];
        double velocity_x[6];
        double velocity_y[6];
        double velocity_z[6];
        double pressure[6];

        for (int species = 0; species < state.n_species; ++species) {
            double values[6];
            for (int stencil = 0; stencil < 6; ++stencil)
                values[stencil] = state.species(species, cells[stencil]);
            PPMReconstruction::reconstruct_species_value(
                values, species_left[species], species_right[species]);
        }
        PPMReconstruction::normalize_species_faces(
            state.n_species, species_left, species_right);

        for (int stencil = 0; stencil < 6; ++stencil) {
            const FluidVector value = state.load(cells[stencil]);
            for (int species = 0; species < state.n_species; ++species)
                species_cell[species] = state.species(species, cells[stencil]);
            PPMReconstruction::gather_eos_stencil_point(
                value, state.n_species > 0 ? species_cell : nullptr, eos,
                rho[stencil], velocity_x[stencil], velocity_y[stencil],
                velocity_z[stencil], pressure[stencil]);
        }
        PPMReconstruction::reconstruct_eos(
            rho, velocity_x, velocity_y, velocity_z, pressure,
            species_left, species_right, eos, left, right);
    }
};
} // namespace arch::cuda
