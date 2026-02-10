/**
 * @file FluxHLL.h
 * @brief HLL Flux Scheme (Refactored to use get_flux)
 */

#pragma once

#include <vector>
#include <algorithm>
#include "FluxFunctions.h"
#include "../reconstruction/Reconstruction.h"

template <typename ReconstructPolicy>
struct FluxHLL
{
    static std::string name() { return "HLL (Einfeldt) + " + ReconstructPolicy::name(); }
    static constexpr int NG = ReconstructPolicy::NG;

    template <typename EosType>
    static void compute_fluxes(const FluidState &state, const EosType &eos, const Grid &grid,
                               std::vector<FluidVector3> &flux_out,
                               std::vector<double> &spec_flux_out,
                               double entropy_fix_coeff = 0.1)
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

            // ======================================================
            // 2. 准备热力学变量 (Thermodynamics First)
            // ======================================================
            // Left State
            double rho_L = std::max(U_L.rho, 1e-12);
            double u_L = U_L.mom / rho_L;
            double e_L = std::max((U_L.eng / rho_L) - 0.5 * u_L * u_L, 1e-8);
            // Pressure
            double P_L = eos.get_pressure(rho_L, U_L.mom, U_L.eng, Yi_L.data());
            double H_L = (U_L.eng + P_L) / rho_L;

            // Right State
            double rho_R = std::max(U_R.rho, 1e-12);
            double u_R = U_R.mom / rho_R;
            double e_R = std::max((U_R.eng / rho_R) - 0.5 * u_R * u_R, 1e-8);
            // Pressure
            double P_R = eos.get_pressure(rho_R, U_R.mom, U_R.eng, Yi_R.data());
            double H_R = (U_R.eng + P_R) / rho_R;

            // ======================================================
            // 3. 计算物理通量 (调用高效重载版本)
            // ======================================================
            // 使用上面算好的 P_L, P_R，无需重复调用 EOS
            FluidVector3 F_L = get_flux(U_L, P_L);
            FluidVector3 F_R = get_flux(U_R, P_R);

            // ======================================================
            // 4. HLL 波速估算
            // ======================================================
            double c_L = calc_sound_speed_thermo(rho_L, P_L, e_L, Yi_L.data(), eos);
            double c_R = calc_sound_speed_thermo(rho_R, P_R, e_R, Yi_R.data(), eos);

            // 复用 Roe 平均状态
            RoeGlaisterState roe_state = calc_glaister_state(
                U_L, U_R, P_L, P_R, e_L, e_R, H_L, H_R, Yi_L.data(), eos);

            double S_L, S_R;
            calc_hll_wave_speeds(u_L, c_L, u_R, c_R, roe_state, S_L, S_R);

            // ======================================================
            // 5. HLL 通量求解
            // ======================================================
            FluidVector3 hll_flux = calc_hll_flux_hydro(F_L, F_R, U_L, U_R, S_L, S_R);
            flux_out[i + 1] = hll_flux;

            // 6. Species Flux
            double mass_flux = hll_flux.rho;
            for (int k = 0; k < n_spec; ++k)
            {
                double transported_Y = (mass_flux >= 0.0) ? Yi_L[k] : Yi_R[k];
                spec_flux_out[k * total_size + (i + 1)] = mass_flux * transported_Y;
            }
        }
    }
};