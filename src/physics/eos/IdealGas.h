/**
 * @file IdealGas.h
 * @brief Equation of State (EOS) solver for an Ideal Gas.
 */

#pragma once

#include <cmath>
#include <algorithm> // for std::max
#include "eos.h"
#include "../species/Species.h"

// 预留跨平台宏
#ifndef EOS_INLINE
#define EOS_INLINE inline
#endif

struct IdealGas : public EOSBase
{
    const SpeciesManager manager;
    double global_gamma;

    IdealGas(const SpeciesManager &m) : manager(m), global_gamma(1.4) {}
    IdealGas(double default_gamma, const SpeciesManager &m)
        : manager(m), global_gamma(default_gamma) {}

    // ========================================================
    // 1. 混合物属性计算
    // ========================================================

    EOS_INLINE double get_gamma(const double *Yi) const
    {
        if (manager.count() == 0)
            return global_gamma;

        double sum_Yi_Cv = 0.0;
        double sum_Yi_Cv_gm1 = 0.0;

        for (int k = 0; k < manager.count(); ++k)
        {
            if (Yi[k] > 1e-12)
            {
                // 使用修改后的 _ref 接口
                double gamma_i = manager.get_gamma_ref(k);
                double Cv_i = manager.get_Cv_ref(k);

                sum_Yi_Cv += Yi[k] * Cv_i;
                sum_Yi_Cv_gm1 += Yi[k] * Cv_i * (gamma_i - 1.0);
            }
        }

        if (sum_Yi_Cv < 1e-12)
            return global_gamma;
        return (sum_Yi_Cv_gm1 / sum_Yi_Cv) + 1.0;
    }

    EOS_INLINE double get_mixture_Cv(const double *Yi) const
    {
        if (manager.count() == 0)
            return 718.0; // 默认空气 Cv 兜底

        double cv_mix = 0.0;
        for (int k = 0; k < manager.count(); ++k)
        {
            cv_mix += Yi[k] * manager.get_Cv_ref(k);
        }
        return cv_mix;
    }

    // ========================================================
    // 2. 鸭子类型必须满足的接口规范 (同 TabularEOSView)
    // ========================================================

    EOS_INLINE double get_pressure_from_rho_e(double rho, double e, const double *Yi) const
    {
        return (get_gamma(Yi) - 1.0) * rho * e;
    }

    // --- 新增：温度接口 (核反应网络统一要求) ---
    EOS_INLINE double get_temperature(double rho, double e, const double *Yi) const
    {
        // 理想气体：e = Cv * T  =>  T = e / Cv
        double cv_mix = get_mixture_Cv(Yi);
        if (cv_mix < 1e-12)
            return 0.0;
        return e / cv_mix;
    }

    EOS_INLINE double get_pressure(const FluidVector &U, const double *Yi) const
    {
        if (U.rho < 1e-12)
            return 0.0;
        double e_kinetic = 0.5 * (U.mom_x * U.mom_x + U.mom_y * U.mom_y + U.mom_z * U.mom_z) / U.rho;
        double e_int = U.eng - e_kinetic;

        return std::max(0.0, (get_gamma(Yi) - 1.0) * e_int);
    }

    EOS_INLINE double get_sound_speed(const FluidVector &U, double p, const double *Yi) const
    {
        if (U.rho < 1e-12)
            return 0.0;
        return std::sqrt(get_gamma(Yi) * p / U.rho);
    }

    EOS_INLINE double get_total_energy_primitive(double rho, double u, double v, double w, double p, const double *Yi) const
    {
        double gamma_mix = get_gamma(Yi);
        double e_internal = p / (gamma_mix - 1.0);
        double e_kinetic = 0.5 * rho * (u * u + v * v + w * w);
        return e_internal + e_kinetic;
    }

    // ========================================================
    // 3. 偏导数 (Implicit Jacobian)
    // ========================================================

    EOS_INLINE double get_dp_drho_e(double rho, double e, const double *Yi) const
    {
        return (get_gamma(Yi) - 1.0) * e;
    }

    EOS_INLINE double get_dp_de_rho(double rho, double e, const double *Yi) const
    {
        return (get_gamma(Yi) - 1.0) * rho;
    }
};