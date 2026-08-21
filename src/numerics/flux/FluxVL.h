/**
 * @file FluxVL.h
 * @brief Vinokur-Von Leer Flux Scheme (Pure Flux Calculator).
 * Decoupled from time integration.
 * Responsibilities:
 * 1. Reconstruction (Cell -> Interface)
 * 2. Flux Splitting (Interface State -> Interface Flux)
 */

/**
 * Workflow:
 * 1. Reconstruct left and right face states using the configured limiter policy.
 * 2. Evaluate the named Riemann flux consistently in every active dimension.
 * 3. Register interface fluxes through the shared AMR path when a coarse-fine face is present.
 */

#pragma once

#include <vector>

#include "FluxFunctions.h"

#include "../reconstruction/AMRInterfaceReconstruction.h"

/**
 * @struct FluxVL
 * @tparam ReconstructPolicy Strategy for spatial reconstruction (e.g., MusclReconstruction<MinMod>).
 */
template <typename ReconstructPolicy>
struct FluxVL
{
    static std::string name() { return "VL-FVS + " + ReconstructPolicy::name(); }

    // Number of ghost cells required by the reconstruction scheme
    static constexpr int NG = ReconstructPolicy::NG;

    template <typename EosType>
    static ARCH_INLINE void compute_face_flux(
        const FluidVector& U_L, const FluidVector& U_R,
        const double* Xi_L, const double* Xi_R, int n_spec,
        const EosType& eos, int dir, double /* coefficient */,
        FluidVector& flux_out, double* species_flux_out)
    {
        // 2. Flux Splitting (Vinokur)
        // F+ (Forward moving waves)
        FluidVector F_plus = calc_vinokur_flux(U_L, Xi_L, eos, +1, dir);
        // F- (Backward moving waves)
        FluidVector F_minus = calc_vinokur_flux(U_R, Xi_R, eos, -1, dir);

        // 3. Store Total Interface Flux
        flux_out = F_plus + F_minus;

        // 4. Species Fluxes
        for (int s = 0; s < n_spec; ++s)
        {
            double spec_flux = F_plus.rho * Xi_L[s] + F_minus.rho * Xi_R[s];
            species_flux_out[s] = spec_flux;
        }
    }

    /**
     * @brief Computes fluxes at ALL cell interfaces.
     * @param state  Input fluid state (conservative variables).
     * @param eos    Equation of state.
     * @param grid   Grid information.
     * @param flux_out         [Output] Buffer for momentum/energy fluxes (size = total_size).
     * @param spec_flux_out    [Output] Buffer for species fluxes (size = n_spec * total_size).
     */
    template <typename EosType>
    static void compute_fluxes(const FluidState &state, const EosType &eos, const Grid &grid,
                               std::vector<FluidVector> &flux_out,
                               std::vector<double> &spec_flux_out,
                               int dir, double /* unused_entropy_coeff */ = 0.0)
    {
        int n_spec = state.GetNumSpecies();
        int total_size = grid.GetTotalSize();
        int stride = (dir == 0) ? 1 : ((dir == 1) ? grid.stride_y : grid.stride_z);

        int i_start = grid.Is();
        int i_end = grid.Ie();
        int j_start = grid.Js();
        int j_end = grid.Je();
        int k_start = grid.Ks();
        int k_end = grid.Ke();

        if (dir == 0)
            i_start -= 1;
        else if (dir == 1)
            j_start -= 1;
        else if (dir == 2)
            k_start -= 1;

        const int nk = k_end - k_start;
        const int nj = j_end - j_start;

#pragma omp parallel
        {
            std::vector<double> Xi_L(n_spec);
            std::vector<double> Xi_R(n_spec);
            std::vector<double> Xi_cell(n_spec);
            std::vector<double> face_species_flux(n_spec);

#pragma omp for schedule(static)
            for (int kj = 0; kj < nk * nj; ++kj)
            {
                int k = k_start + kj / nj;
                int j = j_start + kj % nj;
                for (int i = i_start; i < i_end; ++i)
                {
                    int idx = grid.GetIndex(i, j, k);
                    // 1. Reconstruction (Delegate to Policy)
                    // U_L is at left side of interface i+1/2
                    // U_R is at right side of interface i+1/2
                    FluidVector U_L, U_R;
                    AMRInterfaceReconstruction::reconstruct_face<ReconstructPolicy>(state, eos, grid, dir, i, j, k, idx, stride, n_spec, Xi_L.data(), Xi_R.data(), Xi_cell.data(), U_L, U_R);

                    compute_face_flux(
                        U_L, U_R, Xi_L.data(), Xi_R.data(), n_spec, eos, dir,
                        0.0, flux_out[idx + stride], face_species_flux.data());
                    for (int s = 0; s < n_spec; ++s)
                    {
                        spec_flux_out[s * total_size + (idx + stride)] = face_species_flux[s];
                    }
                }
            }
        } // end omp parallel
    }
};
