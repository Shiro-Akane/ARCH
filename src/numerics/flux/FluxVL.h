/**
 * @file FluxVL.h
 * @brief Vinokur-Von Leer Flux Scheme (Pure Flux Calculator).
 * * Decoupled from time integration.
 * * Responsibilities:
 * * 1. Reconstruction (Cell -> Interface)
 * * 2. Flux Splitting (Interface State -> Interface Flux)
 */

#pragma once

#include <vector>

#include "FluxFunctions.h"

#include "../reconstruction/Reconstruction.h"

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

    /**
     * @brief Computes fluxes at ALL cell interfaces.
     * * @param state  Input fluid state (conservative variables).
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

        const int nk = grid.Ke() - grid.Ks();
        const int nj = grid.Je() - grid.Js();

#pragma omp parallel
        {
            std::vector<double> Xi_L(n_spec);
            std::vector<double> Xi_R(n_spec);

#pragma omp for schedule(static)
            for (int kj = 0; kj < nk * nj; ++kj)
            {
                int k = grid.Ks() + kj / nj;
                int j = grid.Js() + kj % nj;
                for (int i = grid.Is() - 1; i < grid.Ie(); ++i)
                {
                    int idx = grid.GetIndex(i, j, k);
                    // 1. Reconstruction (Delegate to Policy)
                    // U_L is at left side of interface i+1/2
                    // U_R is at right side of interface i+1/2
                    auto [U_L, U_R] = ReconstructPolicy::run(state, idx, stride);

                    if (n_spec > 0)
                    {
                        ReconstructPolicy::run_species(state, idx, n_spec, Xi_L.data(), Xi_R.data(), stride);
                    }

                    // 2. Flux Splitting (Vinokur)
                    // F+ (Forward moving waves)
                    FluidVector F_plus = calc_vinokur_flux(U_L, Xi_L.data(), eos, +1, dir);
                    // F- (Backward moving waves)
                    FluidVector F_minus = calc_vinokur_flux(U_R, Xi_R.data(), eos, -1, dir);

                    // 3. Store Total Interface Flux
                    flux_out[idx + stride] = F_plus + F_minus;

                    // 4. Species Fluxes
                    for (int s = 0; s < n_spec; ++s)
                    {
                        double spec_flux = F_plus.rho * Xi_L[s] + F_minus.rho * Xi_R[s];
                        spec_flux_out[s * total_size + (idx + stride)] = spec_flux;
                    }
                }
            }
        } // end omp parallel
    }
};