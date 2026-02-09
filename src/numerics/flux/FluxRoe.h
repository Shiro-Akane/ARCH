/**
 * @file FluxRoe.h
 * @brief Roe-Glaister Flux Scheme.
 * * Decoupled from time integration.
 * * Responsibilities:
 * * 1. Reconstruction (Cell -> Interface)
 * * 2. Flux Splitting (Interface State -> Interface Flux)
 */

#pragma once

#include <vector>

#include "FluxFunctions.h"

#include "../reconstruction/Reconstruction.h"

template <typename ReconstructPolicy>
struct FluxRoe
{
    static std::string name() { return "Roe-Glaister + " + ReconstructPolicy::name(); }
    static constexpr int NG = ReconstructPolicy::NG;

    template <typename EosType>
    static void compute_fluxes(const FluidState &state, const EosType &eos, const Grid &grid,
                               std::vector<FluidVector3> &flux_out,
                               std::vector<double> &spec_flux_out,
                               double entropy_fix_coeff = 0.1) // <--- 统一接口参数
    {
        int n_spec = state.GetNumSpecies();
        int total_size = grid.GetTotalSize();

        std::vector<double> Yi_L(n_spec);
        std::vector<double> Yi_R(n_spec);

        for (int i = grid.Is() - 1; i < grid.Ie(); i++)
        {
            // 1. Reconstruction
            auto [U_L, U_R] = ReconstructPolicy::run(state, i);
            if (n_spec > 0)
                ReconstructPolicy::run_species(state, i, n_spec, Yi_L.data(), Yi_R.data());

            // 2. Prepare Variables (P, H, e...)
            double rho_L = U_L.rho;
            double u_L = (std::abs(rho_L) > 1e-12) ? U_L.mom / rho_L : 0.0;
            double rho_R = U_R.rho;
            double u_R = (std::abs(rho_R) > 1e-12) ? U_R.mom / rho_R : 0.0;

            double e_L = 0.0;
            if (std::abs(rho_L) > 1e-12)
            {
                e_L = (U_L.eng - 0.5 * rho_L * u_L * u_L) / rho_L;
                // [Critical] 强制非负内能保护
                // 数值误差可能导致动能略大于总能，导致内能为负，进而导致声速计算 sqrt(负数) = NaN
                if (e_L < 0.0)
                    e_L = 1e-8;
            }

            double e_R = 0.0;
            if (std::abs(rho_R) > 1e-12)
            {
                e_R = (U_R.eng - 0.5 * rho_R * u_R * u_R) / rho_R;
                if (e_R < 0.0)
                    e_R = 1e-8;
            }

            double P_L = eos.get_pressure(U_L.rho, U_L.mom, U_L.eng, Yi_L.data());
            double P_R = eos.get_pressure(U_R.rho, U_R.mom, U_R.eng, Yi_R.data());

            double H_L = (std::abs(rho_L) > 1e-12) ? (U_L.eng + P_L) / rho_L : 0.0;
            double H_R = (std::abs(rho_R) > 1e-12) ? (U_R.eng + P_R) / rho_R : 0.0;

            FluidVector3 F_L, F_R;
            F_L.rho = U_L.mom;
            F_L.mom = U_L.mom * u_L + P_L;
            F_L.eng = U_L.mom * H_L;
            F_R.rho = U_R.mom;
            F_R.mom = U_R.mom * u_R + P_R;
            F_R.eng = U_R.mom * H_R;

            // 3. Glaister State
            // 这里为了简化假设 Yi 对导数影响不大或者传其中一个
            RoeGlaisterState roe_state = calc_glaister_state(
                U_L, U_R, P_L, P_R, e_L, e_R, H_L, H_R, Yi_L.data(), eos);

            // 4. Flux Calculation (Passing the coeff!)
            // 这里调用修正后的函数，传入 entropy_fix_coeff
            FluidVector3 roe_flux = calc_roe_flux_hydro(F_L, F_R, U_L, U_R, P_L, P_R, roe_state, entropy_fix_coeff);
            flux_out[i + 1] = roe_flux;

            // 5. Species Flux (Upwind with u_hat)
            double mass_flux = roe_flux.rho;
            for (int k = 0; k < n_spec; ++k)
            {
                double transported_Y = (mass_flux >= 0.0) ? Yi_L[k] : Yi_R[k];

                spec_flux_out[k * total_size + (i + 1)] = mass_flux * transported_Y;
            }
        }
    }
};