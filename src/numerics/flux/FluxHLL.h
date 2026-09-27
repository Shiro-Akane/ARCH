/**
 * @file FluxHLL.h
 * @brief HLL two-wave flux with Einfeldt signal-speed estimates.
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

#include <algorithm>
#include <vector>

#include "numerics/flux/FluxFunctions.h"
#include "numerics/flux/InvariantDomainFlux.h"

#include "numerics/flux/FluxSweep.h"

template <typename ReconstructPolicy>
struct FluxHLL
{
    static std::string name() { return "HLL (Einfeldt) + " + ReconstructPolicy::name(); }
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
        // Recover thermodynamic values before estimating wave speeds.
        // Left State
        double rho_L = U_L.rho;
        double un_L = get_un(U_L, dir);
        double e_L = arch::state::recover(U_L).internal;
        double P_L, c_L;
        FluxAdmissibility::face_thermo(U_L,e_L,Xi_L,n_spec,eos,mean_view,left_cell,P_L,c_L);
        double H_L = (U_L.eng + P_L) / rho_L;

        // Right State
        double rho_R = U_R.rho;
        double un_R = get_un(U_R, dir);
        double e_R = arch::state::recover(U_R).internal;
        double P_R, c_R;
        FluxAdmissibility::face_thermo(U_R,e_R,Xi_R,n_spec,eos,mean_view,right_cell,P_R,c_R);
        double H_R = (U_R.eng + P_R) / rho_R;

        // Compute physical fluxes with the known-pressure overload.
        // Reuse P_L and P_R rather than evaluating the EOS twice.
        FluidVector F_L = get_flux(U_L, P_L, dir);
        FluidVector F_R = get_flux(U_R, P_R, dir);

        // Endpoint sound speeds were recovered with their pressures.

        double S_L=std::min(un_L-c_L,un_R-c_R), S_R=std::max(un_L+c_L,un_R+c_R);
        if (!mean_view || mean_view->roe_wave_speed) {
            const auto roe_state = calc_glaister_state(
                U_L,U_R,P_L,P_R,e_L,e_R,H_L,H_R,Xi_L,Xi_R,n_spec,species_flux_out,eos);
            calc_hll_wave_speeds(un_L,c_L,un_R,c_R,roe_state,dir,S_L,S_R);
        }

        // Assemble the HLL flux.
        FluidVector hll_flux = calc_hll_flux_hydro(F_L, F_R, U_L, U_R, S_L, S_R);
        flux_out = hll_flux;

        // 6. Species Flux
        double mass_flux = hll_flux.rho;
        for (int s = 0; s < n_spec; ++s)
        {
            double transported_X = (mass_flux >= 0.0) ? Xi_L[s] : Xi_R[s];
            species_flux_out[s] = mass_flux * transported_X;
        }
    }

    /** Bind this mathematical policy to the common host face sweep. */
    template <typename EosType>
    static void compute_fluxes(const FluidState& state, const EosType& eos, const Grid& grid,
        std::vector<FluidVector>& flux, std::vector<double>& species_flux,
        int dir, double coefficient = 0.0, FluxAdmissibility::MeanThermoCache* means = nullptr)
    {
        FluxTraversal::compute_fluxes<FluxHLL,ReconstructPolicy>(
            state,eos,grid,flux,species_flux,dir,coefficient,means);
    }
};
