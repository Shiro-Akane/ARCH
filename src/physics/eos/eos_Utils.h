/**
 * @file EOS_Utils.h
 * @brief Common thermodynamic and kinematic utilities for EOS solvers.
 */
#pragma once

#include <cmath>
#include <algorithm>
#include "eos.h" // 确保包含 FluidVector 定义

#ifndef EOS_INLINE
#define EOS_INLINE inline
#endif

namespace eos_utils
{
    // ========================================================
    // 1. 流体运动学能量剥离 (三者通用)
    // ========================================================

    // 计算宏观动能
    EOS_INLINE double calc_kinetic_energy(double rho, double u, double v, double w)
    {
        return 0.5 * rho * (u * u + v * v + w * w);
    }

    // 从守恒量提取比内能 e
    EOS_INLINE double extract_specific_internal_energy(const FluidVector &U)
    {
        if (U.rho < 1e-12)
            return 0.0;
        double kinetic_density = 0.5 * (U.mom_u * U.mom_u + U.mom_v * U.mom_v + U.mom_w * U.mom_w) / U.rho;
        return (U.eng - kinetic_density) / U.rho;
    }

    // ========================================================
    // 2. 泛型牛顿迭代器求总能 (专供 3D/4D 表格类使用)
    // ========================================================

    // 模板参数 TEOSView 鸭子类型匹配，要求提供 get_pressure_from_rho_e 和 get_dp_de_rho
    template <typename TEOSView>
    EOS_INLINE double solve_total_energy(const TEOSView &eos_view,
                                         double rho, double u, double v, double w,
                                         double target_p, const double *Xi)
    {
        // 初始猜测：假设系统接近 gamma = 1.4 的理想气体
        double e_guess = target_p / ((1.4 - 1.0) * rho);

        for (int iter = 0; iter < 20; ++iter)
        {
            double p_guess = eos_view.get_pressure_from_rho_e(rho, e_guess, Xi);
            double dp_de = eos_view.get_dp_de_rho(rho, e_guess, Xi);

            if (std::abs(dp_de) < 1e-12)
                break;

            double delta_e = (target_p - p_guess) / dp_de;

            // 阻尼处理
            while (e_guess + delta_e <= 1e-12)
            {
                delta_e *= 0.5;
            }
            e_guess += delta_e;

            if (std::abs(delta_e) < 1e-6 * e_guess)
                break;
        }

        return rho * e_guess + calc_kinetic_energy(rho, u, v, w);
    }
}