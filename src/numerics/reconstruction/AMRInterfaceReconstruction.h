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
#include "numerics/flux/InvariantDomainFlux.h"

#include "numerics/reconstruction/AMRInterfaceStencil.h"
#include "numerics/reconstruction/Reconstruction.h"
#include "numerics/reconstruction/RzSelectedReconstruction.h"
#include "grid/GridMetrics.h"
#include "grid/Grid.h"

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
                             FluidVector& U_L, FluidVector& U_R,
                             GridMetrics::GeometrySemantics semantics=GridMetrics::GeometrySemantics::Existing,
                             const arch::state::Bounds* physical_bounds=nullptr)
{
    // Native full faces use the selected bundle owner in RzNativeFaceFlux.
    // This scalar interface can expose a genuine radial trace only; an axial
    // face needs four V/W Gauss points and must never pretend to be one mean.
    if(semantics==GridMetrics::GeometrySemantics::AxisymmetricRz) {
        if(dir!=0)throw std::invalid_argument("Native axial reconstruction requires complete V/W face integration");
        if(!physical_bounds||!arch::state::valid_bounds(*physical_bounds))
            throw std::invalid_argument("Native radial trace requires explicit physical bounds");
        const auto geometry=GridMetrics::make_geometry_view(grid,semantics);
        const RzSelectedReconstruction::Context context{
            geometry,grid.Ie()+grid.ng,grid.Je()+grid.ng,i,j,dir,n_spec,*physical_bounds};
        const auto count=static_cast<std::size_t>(n_spec);
        if(count>std::numeric_limits<std::size_t>::max()/35)
            throw std::length_error("Native reconstruction species workspace overflow");
        std::vector<double> scratch(35*count);
        const auto read=[&state](int cell){return state.get(cell);};
        const auto fraction=[&state](int species,int cell){return state.X(species,cell);};
        const auto selected=needs_tvd_interface_reconstruction<ReconstructPolicy>(grid,dir,i,j,k)
            ?RzSelectedReconstruction::reconstruct_face<MusclReconstruction<MinMod>>(
                read,fraction,context,eos,count?scratch.data():nullptr,
                count?scratch.data()+16*count:nullptr,19*count)
            :RzSelectedReconstruction::reconstruct_face<ReconstructPolicy>(
                read,fraction,context,eos,count?scratch.data():nullptr,
                count?scratch.data()+16*count:nullptr,19*count);
        if(selected.status!=arch::state::Status::valid)
            throw std::runtime_error("Native selected radial donor failed required acceptance");
        U_L=selected.donor[0].point[1];U_R=selected.donor[1].point[0];
        for(int species=0;species<n_spec;++species) {
            Xi_L[species]=scratch[count+species]/U_L.rho;
            Xi_R[species]=scratch[8*count+species]/U_R.rho;
        }
        return;
    }
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
    }
    else {
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
    FluxAdmissibility::limit_reconstruction(state.get(idx), U_L);
    FluxAdmissibility::limit_reconstruction(state.get(idx + stride), U_R);
}
} // namespace AMRInterfaceReconstruction
