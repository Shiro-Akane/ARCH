/**
 * @file IdealGas.h
 * @brief Equation of State (EOS) solver for an Ideal Gas.
 */

#pragma once

#include <cmath>
#include <algorithm> // for std::max

#include "eos.h"
#include "eos_Utils.h"

#include "../species/Species.h"

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

    double get_gamma(const double *Xi) const
    {
        if (manager.count() == 0)
            return global_gamma;

        double sum_Xi_Cv = 0.0;
        double sum_Xi_Cv_gm1 = 0.0;

        for (int k = 0; k < manager.count(); ++k)
        {
            if (Xi[k] > 1e-12)
            {
                // 使用修改后的 _ref 接口
                double gamma_i = manager.get_gamma_ref(k);
                double Cv_i = manager.get_Cv_ref(k);

                sum_Xi_Cv += Xi[k] * Cv_i;
                sum_Xi_Cv_gm1 += Xi[k] * Cv_i * (gamma_i - 1.0);
            }
        }

        if (sum_Xi_Cv < 1e-12)
            return global_gamma;
        return (sum_Xi_Cv_gm1 / sum_Xi_Cv) + 1.0;
    }

    double get_mixture_Cv(const double *Xi) const
    {
        if (manager.count() == 0)
            return 718.0; // 默认空气 Cv 兜底

        double cv_mix = 0.0;
        for (int k = 0; k < manager.count(); ++k)
        {
            cv_mix += Xi[k] * manager.get_Cv_ref(k);
        }
        return cv_mix;
    }

    // ========================================================
    // 2. 鸭子类型必须满足的接口规范 (同 TabularEOSView)
    // ========================================================
    const IdealGas &get_view() const
    {
        return *this;
    }

    double get_pressure_from_rho_T(double rho, double T, const double *Xi) const
    {
        double cv_mix = get_mixture_Cv(Xi);
        return (get_gamma(Xi) - 1.0) * rho * cv_mix * T;
    }

    double get_pressure_from_rho_e(double rho, double e, const double *Xi) const
    {
        return (get_gamma(Xi) - 1.0) * rho * e;
    }

    // --- 温度接口 (核反应网络统一要求) ---
    double get_temperature(double rho, double e, const double *Xi) const
    {
        // 理想气体：e = Cv * T  =>  T = e / Cv
        double cv_mix = get_mixture_Cv(Xi);
        if (cv_mix < 1e-12)
            return 0.0;
        return e / cv_mix;
    }

    double get_eint_from_T(double rho, double T, const double *Xi) const
    {
        // 理想气体解析公式：e = Cv * T
        double cv_mix = get_mixture_Cv(Xi);
        return cv_mix * T;
    }

    double get_cv(double rho, double T, const double *Xi) const
    {
        return get_mixture_Cv(Xi);
    }

    double get_pressure(const FluidVector &U, const double *Xi) const
    {
        if (U.rho < 1e-12)
            return 0.0;
        double e_int = eos_utils::extract_specific_internal_energy(U);
        return std::max(0.0, get_pressure_from_rho_e(U.rho, e_int, Xi));
    }

    double get_sound_speed(const FluidVector &U, double p, const double *Xi) const
    {
        if (U.rho < 1e-12)
            return 0.0;
        return std::sqrt(get_gamma(Xi) * p / U.rho);
    }

    double get_total_energy_primitive(double rho, double u, double v, double w, double p, const double *Xi) const
    {
        double gamma_mix = get_gamma(Xi);
        double e_internal_vol = p / (gamma_mix - 1.0);
        return e_internal_vol + eos_utils::calc_kinetic_energy(rho, u, v, w);
    }

    // ========================================================
    // 3. 偏导数 (Implicit Jacobian)
    // ========================================================

    double get_dp_drho_e(double rho, double e, const double *Xi) const
    {
        return (get_gamma(Xi) - 1.0) * e;
    }

    double get_dp_de_rho(double rho, double e, const double *Xi) const
    {
        return (get_gamma(Xi) - 1.0) * rho;
    }
};