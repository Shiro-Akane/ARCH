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
                               std::vector<FluidVector> &flux_out,
                               std::vector<double> &spec_flux_out,
                               int dir, double entropy_fix_coeff = 0.1) // <--- 统一接口参数
    {
        int n_spec = state.GetNumSpecies();
        int total_size = grid.GetTotalSize();
        int stride = (dir == 0) ? 1 : ((dir == 1) ? grid.stride_y : grid.stride_z);

        std::vector<double> Yi_L(n_spec);
        std::vector<double> Yi_R(n_spec);
        for (int k = grid.Ks(); k < grid.Ke(); ++k)
        {
            for (int j = grid.Js(); j < grid.Je(); ++j)
            {
                for (int i = grid.Is() - 1; i < grid.Ie(); ++i)
                {
                    int idx = grid.GetIndex(i, j, k);
                    // 1. Reconstruction
                    auto [U_L, U_R] = ReconstructPolicy::run(state, idx, stride);
                    if (n_spec > 0)
                        ReconstructPolicy::run_species(state, idx, n_spec, Yi_L.data(), Yi_R.data(), stride);

                    // ======================================================
                    // 2. 准备热力学变量 (Thermodynamics First)
                    // ======================================================
                    // Left State
                    double rho_L = std::max(U_L.rho, 1e-12);
                    double un_L = get_un(U_L, dir);
                    double ut1_L = get_ut1(U_L, dir);
                    double ut2_L = get_ut2(U_L, dir);
                    double e_L = std::max((U_L.eng / rho_L) - 0.5 * (un_L * un_L + ut1_L * ut1_L + ut2_L * ut2_L), 1e-8);
                    double P_L = eos.get_pressure(U_L, Yi_L.data());
                    double H_L = (U_L.eng + P_L) / rho_L;

                    // Right State
                    double rho_R = std::max(U_R.rho, 1e-12);
                    double un_R = get_un(U_R, dir);
                    double ut1_R = get_ut1(U_R, dir);
                    double ut2_R = get_ut2(U_R, dir);
                    double e_R = std::max((U_R.eng / rho_R) - 0.5 * (un_R * un_R + ut1_R * ut1_R + ut2_R * ut2_R), 1e-8);
                    double P_R = eos.get_pressure(U_R, Yi_R.data());
                    double H_R = (U_R.eng + P_R) / rho_R;

                    // ======================================================
                    // 3. 计算物理通量 (调用高效重载版本)
                    // ======================================================
                    FluidVector F_L = get_flux(U_L, P_L, dir);
                    FluidVector F_R = get_flux(U_R, P_R, dir);

                    // ======================================================
                    // 4. Roe 平均状态计算
                    // ======================================================
                    RoeGlaisterState roe_state = calc_glaister_state(
                        U_L, U_R, P_L, P_R, e_L, e_R, H_L, H_R, Yi_L.data(), eos);

                    // ======================================================
                    // 5. Roe 通量组装
                    // ======================================================
                    FluidVector roe_flux = calc_roe_flux_hydro(
                        F_L, F_R, U_L, U_R, P_L, P_R, roe_state, entropy_fix_coeff, dir);

                    flux_out[i + 1] = roe_flux;

                    // 6. Species Flux
                    double mass_flux = roe_flux.rho;
                    for (int s = 0; s < n_spec; ++s)
                    {
                        double transported_Y = (mass_flux >= 0.0) ? Yi_L[s] : Yi_R[s];
                        spec_flux_out[s * total_size + (idx + stride)] = mass_flux * transported_Y;
                    }
                }
            }
        }
    }
};