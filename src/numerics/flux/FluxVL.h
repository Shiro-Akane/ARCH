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
                               std::vector<FluidVector3> &flux_out,
                               std::vector<double> &spec_flux_out,
                               double /* unused_entropy_coeff */ = 0.0)
    {
        int n_spec = state.GetNumSpecies();
        int total_size = grid.GetTotalSize();

        // Temporary buffers for species reconstruction
        std::vector<double> Yi_L(n_spec);
        std::vector<double> Yi_R(n_spec);

        // ---------------------------------------------------------
        // Loop over interfaces i+1/2
        // We compute fluxes for physical domain + necessary ghosts
        // Range: typically from Is-1 to Ie
        // ---------------------------------------------------------
        for (int i = grid.Is() - 1; i < grid.Ie(); i++)
        {
            // 1. Reconstruction (Delegate to Policy)
            // U_L is at left side of interface i+1/2
            // U_R is at right side of interface i+1/2
            auto [U_L, U_R] = ReconstructPolicy::run(state, i);

            if (n_spec > 0)
            {
                ReconstructPolicy::run_species(state, i, n_spec, Yi_L.data(), Yi_R.data());
            }

            // 2. Flux Splitting (Vinokur)
            // F+ (Forward moving waves)
            FluidVector3 F_plus = calc_vinokur_flux(U_L, Yi_L.data(), eos, +1);
            // F- (Backward moving waves)
            FluidVector3 F_minus = calc_vinokur_flux(U_R, Yi_R.data(), eos, -1);

            // 3. Store Total Interface Flux
            flux_out[i + 1] = F_plus + F_minus;

            // 4. Species Fluxes
            for (int k = 0; k < n_spec; ++k)
            {
                double spec_flux = F_plus.rho * Yi_L[k] + F_minus.rho * Yi_R[k];
                spec_flux_out[k * total_size + (i + 1)] = spec_flux;
            }
        }
    }
};