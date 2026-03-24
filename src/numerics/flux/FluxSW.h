/**
 * @file FluxSW.h
 * @brief Steger-Warming Flux Scheme (Pure Flux Calculator).
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
 * @struct FluxSW
 * @tparam ReconstructPolicy Strategy for spatial reconstruction (e.g., MusclReconstruction<MinMod>).
 */
template <typename ReconstructPolicy>
struct FluxSW
{
    static std::string name() { return "SW-FVS + " + ReconstructPolicy::name(); }

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
                               std::vector<double> &spec_flux_out, int dir, double smoothing_coeff = 0.1)
    {
        int n_spec = state.GetNumSpecies();
        int total_size = grid.GetTotalSize();
        int stride = (dir == 0) ? 1 : ((dir == 1) ? grid.stride_y : grid.stride_z);

        // Temporary buffers for species reconstruction
        std::vector<double> Yi_L(n_spec);
        std::vector<double> Yi_R(n_spec);

        // ---------------------------------------------------------
        // Loop over interfaces i+1/2
        // We compute fluxes for physical domain + necessary ghosts
        // Range: typically from Is-1 to Ie
        // ---------------------------------------------------------
        for (int k = grid.Ks(); k < grid.Ke(); ++k)
        {
            for (int j = grid.Js(); j < grid.Je(); ++j)
            {
                for (int i = grid.Is() - 1; i < grid.Ie(); ++i)
                {
                    int idx = grid.GetIndex(i, j, k);
                    // 1. Reconstruction (Delegate to Policy)
                    // U_L is at left side of interface i+1/2
                    // U_R is at right side of interface i+1/2
                    auto [U_L, U_R] = ReconstructPolicy::run(state, idx, stride);

                    if (n_spec > 0)
                    {
                        ReconstructPolicy::run_species(state, idx, n_spec, Yi_L.data(), Yi_R.data(), stride);
                    }

                    // 2. Flux Splitting (Vinokur)
                    // F+ (Forward moving waves)
                    FluidVector F_plus = calc_split_flux(U_L, Yi_L.data(), eos, +1, smoothing_coeff, dir);
                    // F- (Backward moving waves)
                    FluidVector F_minus = calc_split_flux(U_R, Yi_R.data(), eos, -1, smoothing_coeff, dir);

                    // 3. Store Total Interface Flux
                    flux_out[idx + stride] = F_plus + F_minus;

                    // 4. Species Fluxes
                    for (int s = 0; s < n_spec; ++s)
                    {
                        double spec_flux = F_plus.rho * Yi_L[s] + F_minus.rho * Yi_R[s];
                        spec_flux_out[s * total_size + (idx + stride)] = spec_flux;
                    }
                }
            }
        }
    }
};