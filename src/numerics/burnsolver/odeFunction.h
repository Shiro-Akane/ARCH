/**
 * @file odeFunction.h
 * @brief Common mathematical and physical toolkit for all ODE solvers.
 */
#pragma once

#include <cmath>
#include <algorithm>

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
    void enforce_mass_conservation(double *Y, double small_x)
    {
        double sum_X = 0.0;
        // 1. 修正极小负值（由于数值截断误差产生）
        for (int i = 0; i < NUM_SPECIES; ++i)
        {
            if (Y[i] < small_x)
                Y[i] = small_x;
            sum_X += Y[i];
        }

        // 2. 归一化
        double inv_sum = 1.0 / sum_X;
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

} // namespace OdeMath