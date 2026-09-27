/**
 * @file FluxVL.h
 * @brief Vinokur-Van Leer Flux Scheme (Pure Flux Calculator).
 * Decoupled from time integration.
 * Responsibilities:
 * 1. Reconstruction (Cell -> Interface)
 * 2. Flux Splitting (Interface State -> Interface Flux)
 */

/**
 * Workflow:
 * 1. Reconstruct left and right face states using the configured limiter policy.
 * 2. Evaluate the named Riemann flux consistently in every active dimension.
 * 3. Store fluid and species face fluxes for the caller's divergence update.
 * Time integration and coarse-fine flux registration belong to the caller,
 * not to this flux policy; the per-face mathematics is shared by backends.
 */

#pragma once

#include <vector>

#include "numerics/flux/FluxFunctions.h"
#include "numerics/flux/InvariantDomainFlux.h"

#include "numerics/flux/FluxSweep.h"

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
        FluidVector& flux_out, double* species_flux_out,
        const FluxAdmissibility::MeanThermoView* mean_view = nullptr,
        int left_cell = -1, int right_cell = -1)
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
    /** Bind this mathematical policy to the common host face sweep. */
    template <typename EosType>
    static void compute_fluxes(const FluidState& state, const EosType& eos, const Grid& grid,
        std::vector<FluidVector>& flux, std::vector<double>& species_flux,
        int dir, double coefficient = 0.0, FluxAdmissibility::MeanThermoCache* means = nullptr)
    {
        FluxTraversal::compute_fluxes<FluxVL,ReconstructPolicy>(
            state,eos,grid,flux,species_flux,dir,coefficient,means);
    }
};
