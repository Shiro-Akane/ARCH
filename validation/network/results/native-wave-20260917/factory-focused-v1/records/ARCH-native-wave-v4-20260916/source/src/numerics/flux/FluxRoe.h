/**
 * @file FluxRoe.h
 * @brief Roe-Glaister Flux Scheme.
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

#include "FluxFunctions.h"

#include "../reconstruction/AMRInterfaceReconstruction.h"

template <typename ReconstructPolicy>
struct FluxRoe
{
    static std::string name() { return "Roe-Glaister + " + ReconstructPolicy::name(); }
    static constexpr int NG = ReconstructPolicy::NG;

    template <typename EosType>
    static ARCH_INLINE void compute_face_flux(
        const FluidVector& U_L, const FluidVector& U_R,
        const double* Xi_L, const double* Xi_R, int n_spec,
        const EosType& eos, int dir, double coefficient,
        FluidVector& flux_out, double* species_flux_out)
    {
        // Recover thermodynamic values before Roe averaging.
        // Left State
        double rho_L = std::max(U_L.rho, 1e-12);
        double un_L = get_un(U_L, dir);
        double ut1_L = get_ut1(U_L, dir);
        double ut2_L = get_ut2(U_L, dir);
        double e_L = std::max((U_L.eng / rho_L) - 0.5 * (un_L * un_L + ut1_L * ut1_L + ut2_L * ut2_L), 1e-8);
        double P_L = eos.get_pressure(U_L, Xi_L);
        double H_L = (U_L.eng + P_L) / rho_L;

        // Right State
        double rho_R = std::max(U_R.rho, 1e-12);
        double un_R = get_un(U_R, dir);
        double ut1_R = get_ut1(U_R, dir);
        double ut2_R = get_ut2(U_R, dir);
        double e_R = std::max((U_R.eng / rho_R) - 0.5 * (un_R * un_R + ut1_R * ut1_R + ut2_R * ut2_R), 1e-8);
        double P_R = eos.get_pressure(U_R, Xi_R);
        double H_R = (U_R.eng + P_R) / rho_R;

        // Compute physical fluxes with the known-pressure overload.
        FluidVector F_L = get_flux(U_L, P_L, dir);
        FluidVector F_R = get_flux(U_R, P_R, dir);

        // Construct the Roe-Glaister averaged state.
        RoeGlaisterState roe_state = calc_glaister_state(
            U_L, U_R, P_L, P_R, e_L, e_R, H_L, H_R,
            Xi_L, Xi_R, n_spec, species_flux_out, eos);

        // Assemble the Roe flux and entropy-corrected dissipation.
        FluidVector roe_flux = calc_roe_flux_hydro(
            F_L, F_R, U_L, U_R, P_L, P_R, roe_state, coefficient, dir);

        flux_out = roe_flux;

        // 6. Species Flux
        double mass_flux = roe_flux.rho;
        for (int s = 0; s < n_spec; ++s)
        {
            double transported_X = (mass_flux >= 0.0) ? Xi_L[s] : Xi_R[s];
            species_flux_out[s] = mass_flux * transported_X;
        }
    }

    template <typename EosType>
    static void compute_fluxes(const FluidState &state, const EosType &eos, const Grid &grid,
                               std::vector<FluidVector> &flux_out,
                               std::vector<double> &spec_flux_out,
                               int dir, double entropy_fix_coeff = 0.1) // Shared entropy-fix interface; 0.1 scales the local spectral radius.
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
                    // 1. Reconstruction
                    FluidVector U_L, U_R;
                    AMRInterfaceReconstruction::reconstruct_face<ReconstructPolicy>(state, eos, grid, dir, i, j, k, idx, stride, n_spec, Xi_L.data(), Xi_R.data(), Xi_cell.data(), U_L, U_R);

                    compute_face_flux(
                        U_L, U_R, Xi_L.data(), Xi_R.data(), n_spec, eos, dir,
                        entropy_fix_coeff, flux_out[idx + stride], face_species_flux.data());
                    for (int s = 0; s < n_spec; ++s)
                    {
                        spec_flux_out[s * total_size + (idx + stride)] = face_species_flux[s];
                    }
                }
            }
        } // end omp parallel
    }
};
