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
#include "numerics/reconstruction/RzCellPolynomial.h"
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
                             GridMetrics::GeometrySemantics semantics=GridMetrics::GeometrySemantics::Existing)
{
    // RZ fields have different conservative measures. Physical radial traces
    // must come from those moments before any solver sees a point-state EOS.
    if(semantics==GridMetrics::GeometrySemantics::AxisymmetricRz && dir==0
       && ReconstructPolicy::NG>1) {
        if(grid.ng<3)
            throw std::invalid_argument("RZ moment face reconstruction requires three halo cells");
        const auto geometry=GridMetrics::make_geometry_view(grid,semantics);
        const auto read=[&state](int c){return state.get(c);};
        const auto fraction=[&state](int k,int c){return state.X(k,c);};
        const double radius=grid.GetFacePosR(i);
        const auto left_cell=RzReconstruction::radial_cell(geometry,i);
        const auto right_cell=RzReconstruction::radial_cell(geometry,i+1);
        const auto left_closure=RzThermodynamics::make_cell(read,idx,geometry,i);
        const auto right_closure=RzThermodynamics::make_cell(read,idx+1,geometry,i+1);
        const auto low=RzReconstruction::limited_profile(read,fraction,idx,n_spec,left_cell,left_closure);
        const auto high=RzReconstruction::limited_profile(read,fraction,idx+1,n_spec,right_cell,right_closure);
        if(!low.valid||!high.valid)
            throw std::runtime_error("RZ face reconstruction requires admissible stage stencils");
        U_L=low.at(radius);U_R=high.at(radius);
        for(int k=0;k<n_spec;++k) {
            Xi_L[k]=RzReconstruction::limited_fraction(read,fraction,idx,k,left_cell,radius,low,U_L.rho);
            Xi_R[k]=RzReconstruction::limited_fraction(read,fraction,idx+1,k,right_cell,radius,high,U_R.rho);
        }
        // The native profile uses one conservative ray for fluid and rho*X.
        // Validate the actual query points; normalization would change that ray.
        if(arch::state::validate(U_L,Xi_L,n_spec,1,0.,0.,
               std::numeric_limits<double>::max())!=arch::state::Status::valid
           ||arch::state::validate(U_R,Xi_R,n_spec,1,0.,0.,
               std::numeric_limits<double>::max())!=arch::state::Status::valid)
            throw std::runtime_error("RZ face has an inadmissible point composition/state");
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
