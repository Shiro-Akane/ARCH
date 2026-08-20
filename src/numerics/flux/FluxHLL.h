/**
 * @file FluxHLL.h
 * @brief HLL Flux Scheme (Refactored to use get_flux)
 */

/**
 * Workflow:
 * 1. Reconstruct left and right face states using the configured limiter policy.
 * 2. Evaluate the named Riemann flux consistently in every active dimension.
 * 3. Register interface fluxes through the shared AMR path when a coarse-fine face is present.
 */

#pragma once

#include <algorithm>
#include <vector>

#include "FluxFunctions.h"

#include "../reconstruction/AMRInterfaceReconstruction.h"

template <typename ReconstructPolicy>
struct FluxHLL
{
    static std::string name() { return "HLL (Einfeldt) + " + ReconstructPolicy::name(); }
    static constexpr int NG = ReconstructPolicy::NG;

    template <typename EosType>
    static void compute_fluxes(const FluidState &state, const EosType &eos, const Grid &grid,
                               std::vector<FluidVector> &flux_out,
                               std::vector<double> &spec_flux_out,
                               int dir,
                               double /* unused_entropy_coeff */ = 0.0)
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

#pragma omp for schedule(static)
            for (int kj = 0; kj < nk * nj; ++kj)
            {
                int k = k_start + kj / nj;
                int j = j_start + kj % nj;
                for (int i = i_start; i < i_end; ++i)
                {
                    int idx = grid.GetIndex(i, j, k);
                    FluidVector U_L, U_R;
                    AMRInterfaceReconstruction::reconstruct_face<ReconstructPolicy>(state, eos, grid, dir, i, j, k, idx, stride, n_spec, Xi_L.data(), Xi_R.data(), Xi_cell.data(), U_L, U_R);

                    // Recover thermodynamic values before estimating wave speeds.
                    // Left State
                    double rho_L = std::max(U_L.rho, 1e-12);
                    double un_L = get_un(U_L, dir);
                    double ut1_L = get_ut1(U_L, dir);
                    double ut2_L = get_ut2(U_L, dir);
                    double v2_L = un_L * un_L + ut1_L * ut1_L + ut2_L * ut2_L;
                    double e_L = std::max((U_L.eng / rho_L) - 0.5 * v2_L, 1e-8);
                    // Pressure
                    double P_L = eos.get_pressure(U_L, Xi_L.data());
                    double H_L = (U_L.eng + P_L) / rho_L;

                    // Right State
                    double rho_R = std::max(U_R.rho, 1e-12);
                    double un_R = get_un(U_R, dir);
                    double ut1_R = get_ut1(U_R, dir);
                    double ut2_R = get_ut2(U_R, dir);
                    double v2_R = un_R * un_R + ut1_R * ut1_R + ut2_R * ut2_R;
                    double e_R = std::max((U_R.eng / rho_R) - 0.5 * v2_R, 1e-8);
                    // Pressure
                    double P_R = eos.get_pressure(U_R, Xi_R.data());
                    double H_R = (U_R.eng + P_R) / rho_R;

                    // Compute physical fluxes with the known-pressure overload.
                    // Reuse P_L and P_R rather than evaluating the EOS twice.
                    FluidVector F_L = get_flux(U_L, P_L, dir);
                    FluidVector F_R = get_flux(U_R, P_R, dir);

                    // Estimate the two HLL wave speeds.
                    double c_L = calc_sound_speed_thermo(rho_L, P_L, e_L, Xi_L.data(), eos);
                    double c_R = calc_sound_speed_thermo(rho_R, P_R, e_R, Xi_R.data(), eos);

                    // Reuse the Roe average required by the Einfeldt bounds.
                    RoeGlaisterState roe_state = calc_glaister_state(
                        U_L, U_R, P_L, P_R, e_L, e_R, H_L, H_R, Xi_L.data(), eos);

                    double S_L, S_R;
                    calc_hll_wave_speeds(un_L, c_L, un_R, c_R, roe_state, dir, S_L, S_R);

                    // Assemble the HLL flux.
                    FluidVector hll_flux = calc_hll_flux_hydro(F_L, F_R, U_L, U_R, S_L, S_R);
                    flux_out[idx + stride] = hll_flux;

                    // 6. Species Flux
                    double mass_flux = hll_flux.rho;
                    for (int s = 0; s < n_spec; ++s)
                    {
                        double transported_X = (mass_flux >= 0.0) ? Xi_L[s] : Xi_R[s];
                        spec_flux_out[s * total_size + (idx + stride)] = mass_flux * transported_X;
                    }
                }
            }
        } // end omp parallel
    }
};
