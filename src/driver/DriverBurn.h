/**
 * @file DriverBurn.h
 * @brief Operator splitting integration of the nuclear reaction network.
 * *
 * * Workflow:
 * * 1. Filters cells by a minimum density threshold to skip vacuums.
 * * 2. Extracts cell composition and calculates cell temperature.
 * * 3. Calls the underlying ODE solver (e.g. BE_NR) to integrate species abundances.
 * * 4. Applies a nuclear energy (enuc) limiter to safely restrict the global CFL timestep.
 */

#pragma once

#include <iostream>
#include <cmath>
#include <algorithm>
#include "../data/FluidState.h"
#include "../grid/Grid.h"
#include "../core/RuntimeParams.h"
#include "../numerics/burnsolver/Networks.h"

#ifdef _OPENMP
#include <omp.h>
#endif

template <typename EosPolicy, typename BurnerPolicy>
void execute_burn_step(FluidState &current_state, double burn_dt, const EosPolicy &eos, 
                       BurnerPolicy &burn, const Grid &grid, const SimConfig &config, 
                       double &dt_burn_global)
{
    if (!config.physics.burn.use_burn)
        return; // 如果没开燃烧，直接跳过

    double local_dt_burn_min = 1e99;
    const int n_spec = current_state.GetNumSpecies();

    int invalid_composition_count = 0;
    int first_invalid_cell = -1;
    double first_invalid_sum = 0.0;
    double first_invalid_min = 0.0;
    double first_invalid_max = 0.0;

    int total_cells = grid.GetTotalSize();
    // Each cell owns its ODE state, network evaluation and LU factorization.
    // Dynamic scheduling is important because stiff substep counts vary strongly
    // across the reaction front; nested teams inside a 22x22 LU are counterproductive.
#pragma omp parallel for schedule(dynamic, 1) reduction(min : local_dt_burn_min)
    for (int i = 0; i < total_cells; ++i)
    {
        double rho = current_state.rho[i];

        // 跳过低密度真空区（保护机制）
        if (rho < config.physics.burn.nuclearDensMin)
            continue;

        // 1. 提取当前单元的组分到 X_ODE
        double X_ODE[BurnLimits::MAX_ODE_NEQ]{};
        current_state.get_species_to_buffer(i, X_ODE);

        // A network state is empty only when the complete composition is
        // invalid.  Testing X_ODE[0] and X_ODE[1] is incorrect: H1/He3
        // are normally zero in aprox19/21 helium/carbon fuel, and He4/C12
        // can both be depleted in an evolved alpha-chain state.
        double composition_sum = 0.0;
        double composition_min = X_ODE[0];
        double composition_max = X_ODE[0];
        bool composition_is_finite = true;
        bool composition_has_negative = false;
        for (int k = 0; k < n_spec; ++k)
        {
            const double xk = X_ODE[k];
            composition_is_finite = composition_is_finite && std::isfinite(xk);
            composition_has_negative = composition_has_negative || xk < -10.0 * config.physics.burn.smallx;
            composition_sum += xk;
            composition_min = std::min(composition_min, xk);
            composition_max = std::max(composition_max, xk);
        }

        const bool composition_is_valid = composition_is_finite && !composition_has_negative && std::isfinite(composition_sum) && composition_sum > 1.0e-13 && std::abs(composition_sum - 1.0) <= 1.0e-6;
        if (!composition_is_valid)
        {
#pragma omp critical(burn_invalid_composition)
            {
                ++invalid_composition_count;
                if (first_invalid_cell < 0)
                {
                    first_invalid_cell = i;
                    first_invalid_sum = composition_sum;
                    first_invalid_min = composition_min;
                    first_invalid_max = composition_max;
                }
            }
            continue;
        }

        // 2. 计算内能并从 EOS 获取当前温度
        double mx = current_state.mom_u[i];
        double my = current_state.mom_v[i];
        double mz = current_state.mom_w[i];
        double e_kin = 0.5 * (mx * mx + my * my + mz * mz) / rho;
        double e_int = (current_state.eng[i] - e_kin) / rho; // 比内能

        // 假设你的 EOS 提供了这个接口：根据 rho, e_int, X_k 求 T
        double T = eos.get_temperature(rho, e_int, X_ODE);
        if (T < 1e7)
            continue;      // 温度太低（< 1e7 K），核反应速率在物理上可忽略，且极低温会引起严重数值溢出
        X_ODE[n_spec] = T; // 将温度放在数组末尾

        // 3. 呼叫底层的 ODE 求解器执行燃烧
        double dt_rec = burn_dt;
        bool success = burn.integrate(X_ODE, rho, burn_dt, eos, config.physics.burn, dt_rec);

        if (!success)
        {
            // Note: `t_current` was used in the error message, but we can just say "at current step" or pass it in.
            // For simplicity, we omit the exact time to keep the signature clean, or just log cell failure.
            std::cerr << "[Fatal Error] Burn failed at cell " << i << std::endl;
            exit(EXIT_FAILURE);
        }

        // 4. 将燃烧后的新组分和新温度写回流体状态
        current_state.set_species_from_buffer(i, X_ODE);
        double T_new = X_ODE[n_spec];

        // 5. 根据新温度和新组分，重新计算内能并更新总能量
        double e_int_new = eos.get_eint_from_T(rho, T_new, X_ODE);
        current_state.eng[i] = rho * e_int_new + e_kin;

        // 6. 核能限制器 (Enuc Limiter)
        // 仅当 enucDtFactor > 0 时才启用限制器。
        if (config.physics.burn.enucDtFactor > 0.0)
        {
            double delta_e = std::abs(e_int_new - e_int);
            if (burn_dt > 0.0)
            {
                double enuc_rate = delta_e / burn_dt;

                // 【防除 0 保护】模仿 FLASH: 计算倒数 (enuc / eint)
                double energyRatioInv = enuc_rate / std::max(e_int_new, 1e-20);

                // 仅当能量变化率显著时，才将其纳入时间步限制
                if (energyRatioInv > 1e-30)
                {
                    // 相当于 dt = enucDtFactor * (eint / enuc)
                    double dt_enuc_limit = config.physics.burn.enucDtFactor / energyRatioInv;
                    local_dt_burn_min = std::min(local_dt_burn_min, dt_enuc_limit);
                }
            }
        }
    }

    if (invalid_composition_count > 0)
    {
        std::cerr << "[Fatal Error] Invalid complete composition before burn: "
                  << invalid_composition_count << " cell(s); first cell="
                  << first_invalid_cell << ", sum(X)=" << first_invalid_sum
                  << ", min(X)=" << first_invalid_min
                  << ", max(X)=" << first_invalid_max
                  << ", network=" << config.physics.burn.network_name
                  << std::endl;
        throw std::runtime_error("Invalid complete composition before burn");
    }

    // 汇总全局最新的燃烧建议步长
    dt_burn_global = std::min(dt_burn_global, local_dt_burn_min);
}
