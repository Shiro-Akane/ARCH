/**
 * @file FluxHLLC.h
 * @brief HLLC Flux Scheme (Toro's Algorithm).
 * * Restores the contact discontinuity (Star Wave S*).
 */

/**
 * Workflow:
 * 1. Reconstruct left and right face states using the configured limiter policy.
 * 2. Evaluate the named Riemann flux consistently in every active dimension.
 * 3. Register interface fluxes through the shared AMR path when a coarse-fine face is present.
 */

#pragma once

#include <vector>
#include <algorithm>
#include "FluxFunctions.h"
#include "../reconstruction/AMRInterfaceReconstruction.h"

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
    static inline FluidVector calc_star_state(
        const FluidVector &U_K, double rho_K, double un_K, double ut1_K, double ut2_K,
        double p_K, double E_K,
        double S_K, double S_star, int dir)
    {
        // scaling factor: omega = (S_K - u_K) / (S_K - S_*)
        double denom = S_K - S_star;
        if (std::abs(denom) < 1e-12)
            return U_K; // 防除零保护

        double omega = (S_K - un_K) / denom;

        // 1. Density*
        double rho_star = rho_K * omega;

        // 2. Momentum*  法向速度变为 S_star，切向速度保持不变 (ut1_K, ut2_K)
        double un_star = S_star;

        // 3. Energy*
        double specific_E_K = E_K / rho_K;

        double sk_minus_u = S_K - un_K;
        double p_term = 0.0;
        if (std::abs(sk_minus_u) > 1e-12)
        {
            p_term = p_K / (rho_K * sk_minus_u);
        }
        double term = (S_star - un_K) * (S_star + p_term);

        double eng_star = rho_star * (specific_E_K + term);

        // 使用 FluxFunctions.h 中的工具函数组装 3D 向量
        return set_flux_vector(rho_star, rho_star * un_star, rho_star * ut1_K, rho_star * ut2_K, eng_star, dir);
    }

    template <typename EosType>
    static void compute_fluxes(const FluidState &state, const EosType &eos, const Grid &grid,
                               std::vector<FluidVector> &flux_out,
                               std::vector<double> &spec_flux_out,
                               int dir, // 0=x, 1=y, 2=z
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
                    AMRInterfaceReconstruction::reconstruct_face<ReconstructPolicy>(state, grid, dir, i, j, k, idx, stride, n_spec, Xi_L.data(), Xi_R.data(), U_L, U_R);

                    // ======================================================
                    // 2. Thermodynamics Preparation
                    // ======================================================
                    // Left
                    double rho_L = std::max(U_L.rho, 1e-12);
                    double un_L = get_un(U_L, dir);
                    double ut1_L = get_ut1(U_L, dir);
                    double ut2_L = get_ut2(U_L, dir);
                    double v2_L = un_L * un_L + ut1_L * ut1_L + ut2_L * ut2_L; // Full 3D kinetic energy

                    double p_L = eos.get_pressure(U_L, Xi_L.data()); // 假设你更新了 EOS 的签名
                    double e_L = (U_L.eng / rho_L) - 0.5 * v2_L;

                    // Right
                    double rho_R = std::max(U_R.rho, 1e-12);
                    double un_R = get_un(U_R, dir);
                    double ut1_R = get_ut1(U_R, dir);
                    double ut2_R = get_ut2(U_R, dir);
                    double v2_R = un_R * un_R + ut1_R * ut1_R + ut2_R * ut2_R;

                    double p_R = eos.get_pressure(U_R, Xi_R.data());
                    double e_R = (U_R.eng / rho_R) - 0.5 * v2_R;

                    // ======================================================
                    // 3. Physical Fluxes (F_L, F_R)
                    // ======================================================
                    FluidVector F_L = get_flux(U_L, p_L, dir);
                    FluidVector F_R = get_flux(U_R, p_R, dir);

                    // ======================================================
                    // 4. Wave Speed Estimates (S_L, S_R, S_*)
                    // ======================================================
                    double c_L = calc_sound_speed_thermo(rho_L, p_L, e_L, Xi_L.data(), eos);
                    double c_R = calc_sound_speed_thermo(rho_R, p_R, e_R, Xi_R.data(), eos);
                    double H_L = (U_L.eng + p_L) / rho_L;
                    double H_R = (U_R.eng + p_R) / rho_R;

                    // Roe Average (Used for S_L, S_R estimates)
                    RoeGlaisterState roe_state = calc_glaister_state(
                        U_L, U_R, p_L, p_R, e_L, e_R, H_L, H_R, Xi_L.data(), eos);

                    double S_L, S_R;
                    calc_hll_wave_speeds(un_L, c_L, un_R, c_R, roe_state, dir, S_L, S_R);

                    // Contact Wave Speed (Star Speed) - NEW Helper call
                    double S_star = calc_hllc_star_speed(un_L, rho_L, p_L, S_L,
                                                         un_R, rho_R, p_R, S_R);

                    // ======================================================
                    // 5. HLLC Flux Assembly & Species Logic
                    // ======================================================
                    FluidVector hllc_flux;
                    const double *chosen_Xi = nullptr; // 用于标记组分来源

                    // Branch 1: Supersonic L (Flow is all L)
                    if (S_L >= 0.0)
                    {
                        hllc_flux = F_L;
                        chosen_Xi = Xi_L.data();
                    }
                    // Branch 4: Supersonic R (Flow is all R)
                    else if (S_R <= 0.0)
                    {
                        hllc_flux = F_R;
                        chosen_Xi = Xi_R.data();
                    }
                    // Subsonic Region (Need Star Fluxes)
                    else
                    {
                        // Branch 2: Left Star Region (S_L < 0 <= S_*)
                        if (S_star >= 0.0)
                        {
                            FluidVector U_L_star = calc_star_state(U_L, rho_L, un_L, ut1_L, ut2_L, p_L, U_L.eng, S_L, S_star, dir);
                            // Formula: F_L* = F_L + S_L * (U_L* - U_L)
                            hllc_flux = F_L + S_L * (U_L_star - U_L);

                            // 核心逻辑：在 Left Star 区域，组分依然来自 Left
                            chosen_Xi = Xi_L.data();
                        }
                        // Branch 3: Right Star Region (S_* < 0 < S_R)
                        else
                        {
                            FluidVector U_R_star = calc_star_state(U_R, rho_R, un_R, ut1_R, ut2_R, p_R, U_R.eng, S_R, S_star, dir);
                            // Formula: F_R* = F_R + S_R * (U_R* - U_R)
                            hllc_flux = F_R + S_R * (U_R_star - U_R);

                            // 核心逻辑：在 Right Star 区域，组分依然来自 Right
                            chosen_Xi = Xi_R.data();
                        }
                    }

                    // Output Hydro Flux
                    flux_out[idx + stride] = hllc_flux;

                    // 6. Species Flux (Upwind based on Star Region)
                    // HLLC Mass Flux 已经包含了接触间断的修正
                    double mass_flux = hllc_flux.rho;

                    for (int s = 0; s < n_spec; ++s)
                    {
                        // 简单原则：如果我们在 S* 左边，用 Xi_L；如果在 S* 右边，用 Xi_R。
                        // 这和上面的 chosen_Xi 逻辑是一致的。
                        // 即使在 Star Region，质量通量发生了变化 (rho* u*)，但组分质量分数 X 保持不变
                        spec_flux_out[s * total_size + (idx + stride)] = mass_flux * chosen_Xi[s];
                    }
                }
            }
        } // end omp parallel
    }
};