/**
 * @file odeFunction.h
 * @brief Common mathematical and physical toolkit for all ODE solvers.
 */
#pragma once

#include <cmath>
#include <algorithm>

#include "../../data/GlobalDefs.h"
#include "../../physics/nse/nse_solver.h"

namespace OdeMath
{

    // =================================================================
    // 1. 基础向量操作 (Vector Math)
    // =================================================================

    /**
     * @brief Y_new = Y_old + alpha * dY (通用向量更新)
     */
    template <int ODE_NEQ>
    void vec_axpy(const double *Y_old, double alpha, const double *dY, double *Y_new)
    {
        // 因为 ODE_NEQ 是模板参数(编译期常量)，编译器会在此处进行激进的循环展开
#pragma omp simd
        for (int i = 0; i < ODE_NEQ; ++i)
        {
            Y_new[i] = Y_old[i] + alpha * dY[i];
        }
    }

    // =================================================================
    // 2. 误差控制与权重计算 (Error Control)
    // =================================================================

    /**
     * @brief 计算每个变量的误差权重 W_i = RTOL * |Y_i| + ATOL
     */
    template <int ODE_NEQ>
    void calc_weights(const double *Y, double rtol, double atol, double *W)
    {
#pragma omp simd
        for (int i = 0; i < ODE_NEQ; ++i)
        {
            W[i] = rtol * std::abs(Y[i]) + atol;
        }
    }

    /**
     * @brief 计算加权均方根误差 (WRMS Norm)
     */
    template <int ODE_NEQ>
    double wrms_norm(const double *err_vec, const double *weight_vec)
    {
        double sum = 0.0;
        for (int i = 0; i < ODE_NEQ; ++i)
        {
            double val = err_vec[i] / weight_vec[i];
            sum += val * val;
        }
        return std::sqrt(sum / ODE_NEQ);
    }

    // =================================================================
    // 3. 物理守恒与安全截断 (Physics Enforcers)
    // =================================================================

    /**
     * @brief 质量分数守恒强加器 (Mass Conservation Enforcer)
     * 只对前 NUM_SPECIES 个元素操作，忽略温度 T
     */
    template <int NUM_SPECIES>
    void enforce_mass_conservation(double *Y, double smallx)
    {
        double sum_X = 0.0;
        // 1. 修正极小负值（由于数值截断误差产生）
        for (int i = 0; i < NUM_SPECIES; ++i)
        {
            if (Y[i] < smallx)
                Y[i] = smallx;
            sum_X += Y[i];
        }

        // 2. 归一化
        double inv_sum = 1.0 / sum_X;
#pragma omp simd
        for (int i = 0; i < NUM_SPECIES; ++i)
        {
            Y[i] *= inv_sum;
        }
    }

    /**
     * @brief 温度安全截断器
     * 假设温度固定在数组的最后一个位置 (ODE_NEQ - 1)
     */
    template <int ODE_NEQ>
    void enforce_temperature_bounds(double *Y, double T_min, double T_max)
    {
        const int T_INDEX = ODE_NEQ - 1;
        if (Y[T_INDEX] < T_min)
            Y[T_INDEX] = T_min;
        if (Y[T_INDEX] > T_max)
            Y[T_INDEX] = T_max;
    }

    // =================================================================
    // 4. 自适应步长 PI 控制器 (PI Step Size Controller)
    // =================================================================

    /**
     * @brief 根据当前误差和上一步误差，计算下一步的 dt 缩放因子
     * @param err_n    当前步的 WRMS 误差
     * @param err_n_1  上一步的 WRMS 误差
     * @param dt_n     当前步长
     * @return         新步长 dt_next
     */
    double pi_controller(double err_n, double err_n_1, double dt_n,
                         int order_q, double safe, double min_fac, double max_fac)
    {
        // 动态计算基于阶数的控制参数 (Hairer & Wanner 标准设定)
        const double k1 = 0.7 / order_q;
        const double k2 = 0.2 / order_q;

        if (err_n < 1e-10)
            return dt_n * max_fac;

        double fac = safe * std::pow(err_n, -k1) * std::pow(err_n_1, k2);
        fac = std::max(min_fac, std::min(max_fac, fac));
        return dt_n * fac;
    }

    // =================================================================
    // 5. NSE 自洽循环 (NSE Self-Consistency Loop)
    // =================================================================
    template <typename NetType, typename EOSType>
    bool integrate_nse_state(double* state, double rho,
                             double dt_target, const EOSType& eos,
                             const BurnConfig& burn_cfg,
                             double& dt_rec)
    {
        // 从 NetType 动态提取维度信息
        constexpr int NEQ = NetType::ODE_NEQ;
        constexpr int NUM_SPEC = NetType::NUM_SPECIES;
        constexpr int MAX_N = BurnLimits::MAX_ODE_NEQ;

        struct Candidate {
            double temperature = 0.0;
            double x[MAX_N]{};
            double enuc = 0.0;
            double residual = 0.0;
            double scale = 1.0;
        };

        double old_x[MAX_N]{};
        long double ye_sum = 0.0L;
#pragma omp simd reduction(+:ye_sum)
        for (int i = 0; i < NUM_SPEC; ++i) {
            old_x[i] = state[i];
            ye_sum += static_cast<long double>(state[i])
                    * static_cast<long double>(NetType::ZION[i] / NetType::AION[i]);
        }
        const double ye = static_cast<double>(ye_sum);
        const double old_temperature = state[NEQ - 1];
        const double old_eint = eos.get_eint_from_T(rho, old_temperature, old_x);
        if (!std::isfinite(ye) || !std::isfinite(old_eint)) return false;

        auto evaluate = [&](double temperature, Candidate& candidate) {
            candidate.temperature = temperature;
            if (!NSESolver<NetType>::solve(temperature, rho, ye, old_x,
                                           candidate.x, candidate.enuc)) {
                return false;
            }
            const double new_eint = eos.get_eint_from_T(rho, temperature, candidate.x);
            candidate.residual = new_eint - old_eint - candidate.enuc;
            candidate.scale = std::max({std::abs(new_eint), std::abs(old_eint), std::abs(candidate.enuc), 1.0});
            return std::isfinite(new_eint) && std::isfinite(candidate.residual) && std::isfinite(candidate.scale);
        };

        auto closed = [](const Candidate& candidate) {
            constexpr double closure_rtol = 1.0e-12;
            return std::abs(candidate.residual) <= closure_rtol * candidate.scale;
        };

        auto accept = [&](const Candidate& candidate) {
#pragma omp simd
            for (int i = 0; i < NUM_SPEC; ++i) state[i] = candidate.x[i];
            state[NEQ - 1] = candidate.temperature;
            dt_rec = dt_target;
            return true;
        };

        const double minimum_temperature = std::max(burn_cfg.nseTempThreshold, burn_cfg.smallt);
        constexpr double maximum_temperature = 1.0e11;
        if (old_temperature < minimum_temperature || old_temperature > maximum_temperature) {
            return false;
        }

        Candidate current;
        if (!evaluate(old_temperature, current)) return false;
        if (closed(current)) return accept(current);

        Candidate previous;
        bool have_previous = false;
        for (int iter = 0; iter < 20; ++iter) {
            double derivative = eos.get_cv(rho, current.temperature, current.x);
            if (have_previous && current.temperature != previous.temperature) {
                const double secant = (current.residual - previous.residual) / (current.temperature - previous.temperature);
                if (std::isfinite(secant) && secant > 0.0) derivative = secant;
            }
            if (!std::isfinite(derivative) || derivative <= 0.0) break;

            double delta_temperature = -current.residual / derivative;
            const double step_limit = 0.5 * current.temperature;
            delta_temperature = std::clamp(delta_temperature, -step_limit, step_limit);

            bool improved = false;
            double alpha = 1.0;
            Candidate trial;
            for (int line_search = 0; line_search < 16; ++line_search) {
                const double trial_temperature = std::clamp(current.temperature + alpha * delta_temperature, minimum_temperature, maximum_temperature);
                if (trial_temperature == current.temperature) break;
                if (evaluate(trial_temperature, trial) && std::abs(trial.residual) < std::abs(current.residual)) {
                    improved = true;
                    break;
                }
                alpha *= 0.5;
            }
            if (!improved) break;
            previous = current;
            have_previous = true;
            current = trial;
            if (closed(current)) return accept(current);
        }

        Candidate lower;
        Candidate upper;
        if (!evaluate(minimum_temperature, lower) || !evaluate(maximum_temperature, upper) || std::signbit(lower.residual) == std::signbit(upper.residual)) {
            return false;
        }
        if (lower.residual > 0.0) std::swap(lower, upper);

        for (int iter = 0; iter < 64; ++iter) {
            Candidate midpoint;
            const double midpoint_temperature = 0.5 * (lower.temperature + upper.temperature);
            if (!evaluate(midpoint_temperature, midpoint)) return false;
            if (closed(midpoint)) return accept(midpoint);
            if (midpoint.residual < 0.0) lower = midpoint;
            else upper = midpoint;
        }
        return false;
    }

    // =================================================================
    // 6. Bader-Deuflhard 多项式外推与控制 (Extrapolation & Control)
    // =================================================================

    /**
     * @brief 执行多项式外推 (Polynomial Extrapolation)
     * @param k 当前的外推层级 (0 to MAX_K-1)
     * @param n_seq BD 序列 (如 2, 6, 10, 14...)
     * @param T 外推表 T[k][j][NEQ]
     * @param y_err 输出的截断误差估计
     */
    template <int ODE_NEQ, int MAX_K>
    void bd_extrapolate(int k, const int* n_seq,
                        double T[MAX_K][MAX_K][ODE_NEQ],
                        double* y_err)
    {
        // 从 j=1 开始，利用低阶的解外推高阶
        for (int j = 1; j <= k; ++j)
        {
            double fac = static_cast<double>(n_seq[k] * n_seq[k]) /
                         static_cast<double>(n_seq[k - j] * n_seq[k - j]) - 1.0;
            const double inv_fac = 1.0 / fac;

#pragma omp simd
            for (int i = 0; i < ODE_NEQ; ++i)
            {
                T[k][j][i] = T[k][j - 1][i] + (T[k][j - 1][i] - T[k - 1][j - 1][i]) * inv_fac;
            }
        }

        // 误差估计：最高阶与其上一阶的差值 (Deuflhard 标准截断误差)
        if (k > 0) {
#pragma omp simd
            for (int i = 0; i < ODE_NEQ; ++i) {
                y_err[i] = T[k][k][i] - T[k][k - 1][i];
            }
        }
    }

} // namespace OdeMath