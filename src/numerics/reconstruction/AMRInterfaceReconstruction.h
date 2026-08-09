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

#include "Reconstruction.h"
#include "../../grid/Grid.h"

/**
 * A wide uniform-grid stencil is not valid across a 2:1 coarse-fine
 * interface: ghost samples represent prolonged/restricted cell averages with
 * a different physical width. For those few interface fluxes, use a
 * conservative second-order MUSCL reconstruction with MinMod limiting.
 *
 * This is an explicit TVD interface policy, not a failure fallback: it is
 * active only on faces marked coarse-fine by AmrTree and leaves all other
 * PPM or other wide-stencil fluxes untouched.
 */
namespace AMRInterfaceReconstruction
{
template <typename ReconstructPolicy>
inline bool needs_tvd_interface_reconstruction(const Grid& grid, int dir, int i, int j, int k)
{
    if constexpr (ReconstructPolicy::NG <= MusclReconstruction<MinMod>::NG)
    {
        return false;
    }

    const int normal_index = (dir == 0) ? i : ((dir == 1) ? j : k);
    const int normal_begin = (dir == 0) ? grid.Is() : ((dir == 1) ? grid.Js() : grid.Ks());
    const int normal_end = (dir == 0) ? grid.Ie() : ((dir == 1) ? grid.Je() : grid.Ke());

    const bool touches_lower_coarse_fine =
        grid.amr_coarse_fine_face[2 * dir] && normal_index < normal_begin + 2;
    const bool touches_upper_coarse_fine =
        grid.amr_coarse_fine_face[2 * dir + 1] && normal_index >= normal_end - 3;

    return touches_lower_coarse_fine || touches_upper_coarse_fine;
}

template <typename ReconstructPolicy>
inline void reconstruct_face(const FluidState& state, const Grid& grid,
                             int dir, int i, int j, int k, int idx, int stride,
                             int n_spec, double* Xi_L, double* Xi_R,
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

    auto reconstructed = ReconstructPolicy::run(state, idx, stride);
    U_L = reconstructed.first;
    U_R = reconstructed.second;
    if (n_spec > 0)
    {
        ReconstructPolicy::run_species(state, idx, n_spec, Xi_L, Xi_R, stride);
    }
}
} // namespace AMRInterfaceReconstruction
