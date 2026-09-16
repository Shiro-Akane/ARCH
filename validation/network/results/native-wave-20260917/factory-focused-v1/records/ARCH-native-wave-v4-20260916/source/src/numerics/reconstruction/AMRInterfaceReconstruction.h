/**
 * @file AMRInterfaceReconstruction.h
 * @brief Shared reconstruction policy for coarse-fine AMR interfaces.
 */

/**
 * Workflow:
 * 1. Identify whether a face is uniform-grid or crosses a 2:1 AMR interface.
 * 2. Use the mature TVD MUSCL interface stencil only where wide PPM stencils are invalid.
 * 3. Keep the configured high-order reconstruction unchanged away from the interface.
 */

#pragma once

#include <type_traits>

#include "AMRInterfaceStencil.h"
#include "Reconstruction.h"
#include "../../grid/Grid.h"

/**
 * Faces marked as 2:1 coarse-fine interfaces use conservative second-order
 * MUSCL reconstruction with MinMod limiting. Their ghost samples represent
 * prolonged or restricted cell averages with different physical widths.
 * Other faces retain the configured reconstruction policy.
 */
namespace AMRInterfaceReconstruction
{
template <typename ReconstructPolicy>
inline bool needs_tvd_interface_reconstruction(const Grid& grid, int dir, int i, int j, int k)
{
    const int normal_index = (dir == 0) ? i : ((dir == 1) ? j : k);
    const int normal_begin = (dir == 0) ? grid.Is() : ((dir == 1) ? grid.Js() : grid.Ks());
    const int normal_end = (dir == 0) ? grid.Ie() : ((dir == 1) ? grid.Je() : grid.Ke());
    return needs_tvd_interface_stencil(
        ReconstructPolicy::NG, grid.amr_coarse_fine_face, dir,
        normal_index, normal_begin, normal_end);
}

template <typename ReconstructPolicy, typename EosType>
inline void reconstruct_face(const FluidState& state, const EosType& eos, const Grid& grid,
                             int dir, int i, int j, int k, int idx, int stride,
                             int n_spec, double* Xi_L, double* Xi_R, double* Xi_cell,
                             FluidVector& U_L, FluidVector& U_R)
{
    if (needs_tvd_interface_reconstruction<ReconstructPolicy>(grid, dir, i, j, k))
    {
        auto reconstructed = MusclReconstruction<MinMod>::run(state, idx, stride);
        U_L = reconstructed.first;
        U_R = reconstructed.second;
        if (n_spec > 0)
        {
            MusclReconstruction<MinMod>::run_species(
                state, idx, n_spec, Xi_L, Xi_R, stride);
        }
        return;
    }

    if (n_spec > 0)
    {
        ReconstructPolicy::run_species(state, idx, n_spec, Xi_L, Xi_R, stride);
    }

    if constexpr (std::is_same_v<ReconstructPolicy, PPMReconstruction>)
    {
        auto reconstructed = PPMReconstruction::run_eos(
            state, eos, idx, n_spec, Xi_L, Xi_R, Xi_cell, stride);
        U_L = reconstructed.first;
        U_R = reconstructed.second;
    }
    else
    {
        auto reconstructed = ReconstructPolicy::run(state, idx, stride);
        U_L = reconstructed.first;
        U_R = reconstructed.second;
    }
}
} // namespace AMRInterfaceReconstruction
