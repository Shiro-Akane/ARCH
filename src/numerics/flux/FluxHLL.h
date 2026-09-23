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

#include "numerics/reconstruction/AMRInterfaceReconstruction.h"

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
        FluidVector& flux_out, double* species_flux_out)
    {
        // Recover thermodynamic values before estimating wave speeds.
        // Left State
        double rho_L = U_L.rho;
        double un_L = get_un(U_L, dir);
        double e_L = arch::state::recover(U_L).internal;
        double P_L, c_L;
        calc_endpoint_thermo(U_L, e_L, Xi_L, eos, P_L, c_L);
        double H_L = (U_L.eng + P_L) / rho_L;

        // Right State
        double rho_R = U_R.rho;
        double un_R = get_un(U_R, dir);
        double e_R = arch::state::recover(U_R).internal;
        double P_R, c_R;
        calc_endpoint_thermo(U_R, e_R, Xi_R, eos, P_R, c_R);
        double H_R = (U_R.eng + P_R) / rho_R;

        // Compute physical fluxes with the known-pressure overload.
        // Reuse P_L and P_R rather than evaluating the EOS twice.
        FluidVector F_L = get_flux(U_L, P_L, dir);
        FluidVector F_R = get_flux(U_R, P_R, dir);

        // Endpoint sound speeds were recovered with their pressures.

        // Reuse the Roe average required by the Einfeldt bounds.
        RoeGlaisterState roe_state = calc_glaister_state(
            U_L, U_R, P_L, P_R, e_L, e_R, H_L, H_R,
            Xi_L, Xi_R, n_spec, species_flux_out, eos);

        double S_L, S_R;
        calc_hll_wave_speeds(un_L, c_L, un_R, c_R, roe_state, dir, S_L, S_R);

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

    template <typename EosType>
    static void compute_fluxes(const FluidState &state, const EosType &eos, const Grid &grid,
                               std::vector<FluidVector> &flux_out,
                               std::vector<double> &spec_flux_out,
                               int dir,
                               double /* unused_entropy_coeff */ = 0.0)
    {
        arch::state::HostFailure failure;
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
                try {
                    int k = k_start + kj / nj;
                    int j = j_start + kj % nj;
                    for (int i = i_start; i < i_end; ++i)
                    {
                        int idx = grid.GetIndex(i, j, k);
                        FluidVector U_L, U_R;
                        AMRInterfaceReconstruction::reconstruct_face<ReconstructPolicy>(state, eos, grid, dir, i, j, k, idx, stride, n_spec, Xi_L.data(), Xi_R.data(), Xi_cell.data(), U_L, U_R);

                        FluxAdmissibility::compute_candidate([&] {
                            compute_face_flux(
                                U_L, U_R, Xi_L.data(), Xi_R.data(), n_spec, eos, dir,
                                0.0, flux_out[idx + stride], face_species_flux.data());
                        }, flux_out[idx + stride], face_species_flux.data(), n_spec);
                        state.get_species_to_buffer(idx, Xi_L.data());
                        state.get_species_to_buffer(idx + stride, Xi_R.data());
                        FluxAdmissibility::limit_face(state.get(idx), state.get(idx + stride),
                            Xi_L.data(), Xi_R.data(), n_spec, eos, dir,
                            flux_out[idx + stride], face_species_flux.data());
                        for (int s = 0; s < n_spec; ++s)
                        {
                            spec_flux_out[s * total_size + (idx + stride)] = face_species_flux[s];
                        }
                    }

                } catch (...) { failure.capture_current(); }
            }
        } // end omp parallel

        failure.rethrow();
    }
};
