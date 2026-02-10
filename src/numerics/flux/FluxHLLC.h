/**
 * @file FluxHLLC.h
 * @brief HLLC Flux Scheme (Toro's Algorithm).
 * * Restores the contact discontinuity (Star Wave S*).
 */

#pragma once

#include <vector>
#include <algorithm>
#include "FluxFunctions.h"
#include "../reconstruction/Reconstruction.h"

template <typename ReconstructPolicy>
struct FluxHLLC
{
    static std::string name() { return "HLLC + " + ReconstructPolicy::name(); }
    static constexpr int NG = ReconstructPolicy::NG;

    // ----------------------------------------------------------------------
    // Internal Helper: Calculate Star State U* (中间态)
    // Ref: Toro, "Riemann Solvers and Numerical Methods for Fluid Dynamics"
    // U_K^* = rho_K * ((S_K - u_K) / (S_K - S_*)) * [1, S_*, E_K/rho_K + ...]
    // ----------------------------------------------------------------------
    static inline FluidVector3 calc_star_state(
        const FluidVector3 &U_K, double rho_K, double u_K, double p_K, double E_K,
        double S_K, double S_star)
    {
        // scaling factor: omega = (S_K - u_K) / (S_K - S_*)
        double denom = S_K - S_star;
        if (std::abs(denom) < 1e-12)
            return U_K; // 防除零保护

        double omega = (S_K - u_K) / denom;

        FluidVector3 U_star;

        // 1. Density*: rho^* = rho * omega
        double rho_star = rho_K * omega;
        U_star.rho = rho_star;

        // 2. Momentum*: (rho * u)^* = rho^* * S_*
        U_star.mom = rho_star * S_star;

        // 3. Energy*: (rho * E)^*
        // Formula: E^* = E + (S_* - u) * (S_* + p / (rho * (S - u)))
        // Note: U_star.eng is total energy density (rho * E)
        double term = (S_star - u_K) * (S_star + p_K / (rho_K * (S_K - u_K)));
        double specific_E_K = E_K / rho_K; // E_total per unit mass

        U_star.eng = rho_star * (specific_E_K + term);

        return U_star;
    }

    template <typename EosType>
    static void compute_fluxes(const FluidState &state, const EosType &eos, const Grid &grid,
                               std::vector<FluidVector3> &flux_out,
                               std::vector<double> &spec_flux_out,
                               double /* unused_entropy_coeff */ = 0.0)
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
            // 2. Thermodynamics Preparation
            // ======================================================
            // Left
            double rho_L = std::max(U_L.rho, 1e-12);
            double u_L = U_L.mom / rho_L;
            double p_L = eos.get_pressure(rho_L, U_L.mom, U_L.eng, Yi_L.data());
            double e_L = (U_L.eng / rho_L) - 0.5 * u_L * u_L;

            // Right
            double rho_R = std::max(U_R.rho, 1e-12);
            double u_R = U_R.mom / rho_R;
            double p_R = eos.get_pressure(rho_R, U_R.mom, U_R.eng, Yi_R.data());
            double e_R = (U_R.eng / rho_R) - 0.5 * u_R * u_R;

            // ======================================================
            // 3. Physical Fluxes (F_L, F_R)
            // ======================================================
            FluidVector3 F_L = get_flux(U_L, p_L);
            FluidVector3 F_R = get_flux(U_R, p_R);

            // ======================================================
            // 4. Wave Speed Estimates (S_L, S_R, S_*)
            // ======================================================
            double c_L = calc_sound_speed_thermo(rho_L, p_L, e_L, Yi_L.data(), eos);
            double c_R = calc_sound_speed_thermo(rho_R, p_R, e_R, Yi_R.data(), eos);
            double H_L = (U_L.eng + p_L) / rho_L;
            double H_R = (U_R.eng + p_R) / rho_R;

            // Roe Average (Used for S_L, S_R estimates)
            RoeGlaisterState roe_state = calc_glaister_state(
                U_L, U_R, p_L, p_R, e_L, e_R, H_L, H_R, Yi_L.data(), eos);

            double S_L, S_R;
            calc_hll_wave_speeds(u_L, c_L, u_R, c_R, roe_state, S_L, S_R);

            // Contact Wave Speed (Star Speed) - NEW Helper call
            double S_star = calc_hllc_star_speed(u_L, rho_L, p_L, S_L,
                                                 u_R, rho_R, p_R, S_R);

            // ======================================================
            // 5. HLLC Flux Assembly & Species Logic
            // ======================================================
            FluidVector3 hllc_flux;
            const double *chosen_Yi = nullptr; // 用于标记组分来源

            // Branch 1: Supersonic L (Flow is all L)
            if (S_L >= 0.0)
            {
                hllc_flux = F_L;
                chosen_Yi = Yi_L.data();
            }
            // Branch 4: Supersonic R (Flow is all R)
            else if (S_R <= 0.0)
            {
                hllc_flux = F_R;
                chosen_Yi = Yi_R.data();
            }
            // Subsonic Region (Need Star Fluxes)
            else
            {
                // Branch 2: Left Star Region (S_L < 0 <= S_*)
                if (S_star >= 0.0)
                {
                    FluidVector3 U_L_star = calc_star_state(U_L, rho_L, u_L, p_L, U_L.eng, S_L, S_star);
                    // Formula: F_L* = F_L + S_L * (U_L* - U_L)
                    hllc_flux = F_L + S_L * (U_L_star - U_L);

                    // 核心逻辑：在 Left Star 区域，组分依然来自 Left
                    chosen_Yi = Yi_L.data();
                }
                // Branch 3: Right Star Region (S_* < 0 < S_R)
                else
                {
                    FluidVector3 U_R_star = calc_star_state(U_R, rho_R, u_R, p_R, U_R.eng, S_R, S_star);
                    // Formula: F_R* = F_R + S_R * (U_R* - U_R)
                    hllc_flux = F_R + S_R * (U_R_star - U_R);

                    // 核心逻辑：在 Right Star 区域，组分依然来自 Right
                    chosen_Yi = Yi_R.data();
                }
            }

            // Output Hydro Flux
            flux_out[i + 1] = hllc_flux;

            // 6. Species Flux (Upwind based on Star Region)
            // HLLC Mass Flux 已经包含了接触间断的修正
            double mass_flux = hllc_flux.rho;

            for (int k = 0; k < n_spec; ++k)
            {
                // 简单原则：如果我们在 S* 左边，用 Yi_L；如果在 S* 右边，用 Yi_R。
                // 这和上面的 chosen_Yi 逻辑是一致的。
                // 即使在 Star Region，质量通量发生了变化 (rho* u*)，但组分质量分数 Y 保持不变
                spec_flux_out[k * total_size + (i + 1)] = mass_flux * chosen_Yi[k];
            }
        }
    }
};