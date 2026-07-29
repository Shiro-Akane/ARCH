/**
 * @file ode_ros4.h
 * @brief Unified concept for ODE Integrators using 4-stage Rosenbrock W-method (ROS4).
 *        Updated with intermediate state sanitization and floating-point cancellation safeguards.
 */
#pragma once
#include <cmath>
#include <algorithm>
#include <iostream>

#include "Networks.h"
#include "odeFunction.h"

template <typename NetType, typename MatrixType, typename LinearSolver>
struct Solver_ROS4
{
    static constexpr int NEQ = NetType::ODE_NEQ;
    static constexpr int NUM_SPEC = NetType::NUM_SPECIES;
    static constexpr int MAX_N = BurnLimits::MAX_ODE_NEQ;

    // ROS4 (W-method) Butcher Tableau (GRK4T / L-stable params)
    static constexpr double gamma = 0.25; 
    static constexpr double a21 = 0.5;
    static constexpr double a31 = 0.5,   a32 = 0.5;
    static constexpr double a41 = 0.25,  a42 = 0.25,  a43 = 0.5;
    static constexpr double c21 = -1.0;
    static constexpr double c31 = -1.5,  c32 = -1.0;
    static constexpr double c41 = -2.0,  c42 = -1.5,  c43 = -1.0;
    static constexpr double m1 = 0.166666667, m2 = 0.333333333, m3 = 0.333333333, m4 = 0.166666667;
    static constexpr double e1 = 0.05,        e2 = -0.15,       e3 = 0.15,        e4 = -0.05;

    template <typename EOSType>
    static bool integrate(double *Y_ODE, double rho, double dt_target, const EOSType &eos,
                          const BurnConfig &burn_cfg, double &dt_rec)
    {
        if (Y_ODE[NEQ - 1] < burn_cfg.nuclearTempMin || rho < burn_cfg.nuclearDensMin)
        {
            return true;
        }

        double Y_old[MAX_N], Y_k[MAX_N], Y_trial[MAX_N];
        double RHS[MAX_N], b[MAX_N], W[MAX_N], Y_err[MAX_N];
        double u1[MAX_N], u2[MAX_N], u3[MAX_N], u4[MAX_N];
        MatrixType J_mat, A;

        const double rtol = burn_cfg.odeconfig.rtol;
        const double atol = burn_cfg.odeconfig.atol;
        const int max_substeps = burn_cfg.odeconfig.max_substeps;

        double t_current = 0.0;
        double dt = std::min(dt_target, dt_target * burn_cfg.odeconfig.initial_dt_frac);
        double err_prev = 1.0;
        int substep_count = 0;

        bool nse_attempted = false;

        // 统一的 RHS 评估 Lambda
        auto eval_full_rhs = [&](const double* Y, double* out_RHS) {
            double enuc = 0.0;
            double eta = eos.get_eta(rho, Y[NEQ - 1], Y);
            NetType::eval_rhs(Y, rho, eta, out_RHS, enuc);
            double cv = std::max(eos.get_cv(rho, Y[NEQ - 1], Y), 1e-10);
            out_RHS[NEQ - 1] = enuc / cv;
        };

        // [FIX 1]: 中间态清洗器 - 防止数学构造阶段产生极端的非物理状态导致 RHS 崩溃
        // 仅仅用于保护中间级的导数评估，最终步骤的严格性不受此影响。
        auto sanitize_state = [&](double* Y_state) {
#pragma omp simd
            for (int i = 0; i < NUM_SPEC; ++i) {
                if (Y_state[i] < burn_cfg.smallx) Y_state[i] = burn_cfg.smallx;
                else if (Y_state[i] > 1.0) Y_state[i] = 1.0;
            }
            if (Y_state[NEQ - 1] < burn_cfg.nuclearTempMin) {
                Y_state[NEQ - 1] = burn_cfg.nuclearTempMin;
            }
        };

        // ================= 外层循环：时间子步进推进 =================
        while (t_current < dt_target)
        {
            if (!nse_attempted && burn_cfg.use_nse
                && Y_ODE[NEQ - 1] > burn_cfg.nseTempThreshold
                && rho > burn_cfg.nseDensThreshold)
            {
                nse_attempted = true;
                if (OdeMath::integrate_nse_state<NetType, EOSType>(Y_ODE, rho, dt_target, eos,
                                                        burn_cfg, dt_rec)) {    
                    return true;
                }
            }

            substep_count++;
            if (substep_count > max_substeps)
            {
                std::cerr << "[ROS4] Fatal Error: Exceeded max substeps (" << max_substeps << ")" << std::endl;
                return false;
            }

            if (t_current + dt > dt_target) dt = dt_target - t_current;

#pragma omp simd
            for (int i = 0; i < NEQ; ++i) Y_old[i] = Y_ODE[i];

            // 1. 获取完整的右端项 f(y_0) 
            eval_full_rhs(Y_old, RHS);

            // 2. 调用全解析 Jacobian 接口
            J_mat.zero();
            double T_current = Y_old[NEQ - 1];
            double denuc_dX[MAX_N]{};
            double dRHS_dT[MAX_N]{};
            double denuc_dT = 0.0;
            double eta_jac = eos.get_eta(rho, T_current, Y_old);
            NetType::eval_jacobian(Y_old, rho, eta_jac, J_mat, denuc_dX);
            NetType::eval_temperature_derivative(Y_old, rho, eta_jac, dRHS_dT, denuc_dT);

            const double cv = std::max(eos.get_cv(rho, T_current, Y_old), 1.0e-10);
            const double inv_cv = 1.0 / cv;

#pragma omp simd
            for (int i = 0; i < NUM_SPEC; ++i) J_mat.set(i + 1, NEQ, dRHS_dT[i]);
#pragma omp simd
            for (int j = 0; j < NUM_SPEC; ++j) J_mat.set(NEQ, j + 1, denuc_dX[j] * inv_cv);
            J_mat.set(NEQ, NEQ, denuc_dT * inv_cv);

            // 3. 构造系统矩阵 A = I - gamma * dt * J
            for (int i = 0; i < NEQ; ++i) {
#pragma omp simd
                for (int j = 0; j < NEQ; ++j) {
                    A.set(i + 1, j + 1, -gamma * dt * J_mat(i + 1, j + 1));
                }
                A.set(i + 1, i + 1, A(i + 1, i + 1) + 1.0);
            }

            // 4. LU 分解
            int p[MAX_N];
            bool step_converged = false;
            double current_err = 0.0;
            
            if (!LinearSolver::template factorize<NEQ, MAX_N>(A, p))
            {
                dt *= 0.25;
                continue; 
            }

            bool solve_failed = false;

            // ================= STAGE 1 =================
#pragma omp simd
            for (int i = 0; i < NEQ; ++i) b[i] = dt * RHS[i];
            LinearSolver::template solve_with_factors<NEQ, MAX_N>(A, p, b);
#pragma omp simd
            for (int i = 0; i < NEQ; ++i) { u1[i] = b[i]; if (!std::isfinite(u1[i])) solve_failed = true; }

            // ================= STAGE 2 =================
            if (!solve_failed) {
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) Y_k[i] = Y_old[i] + a21 * u1[i];
                sanitize_state(Y_k); // [FIX] 防止中间状态越界
                eval_full_rhs(Y_k, RHS);
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) b[i] = dt * RHS[i] + c21 * u1[i];
                LinearSolver::template solve_with_factors<NEQ, MAX_N>(A, p, b);
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) { u2[i] = b[i]; if (!std::isfinite(u2[i])) solve_failed = true; }
            }

            // ================= STAGE 3 =================
            if (!solve_failed) {
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) Y_k[i] = Y_old[i] + a31 * u1[i] + a32 * u2[i];
                sanitize_state(Y_k); // [FIX] 防止中间状态越界
                eval_full_rhs(Y_k, RHS);
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) b[i] = dt * RHS[i] + c31 * u1[i] + c32 * u2[i];
                LinearSolver::template solve_with_factors<NEQ, MAX_N>(A, p, b);
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) { u3[i] = b[i]; if (!std::isfinite(u3[i])) solve_failed = true; }
            }

            // ================= STAGE 4 =================
            if (!solve_failed) {
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) Y_k[i] = Y_old[i] + a41 * u1[i] + a42 * u2[i] + a43 * u3[i];
                sanitize_state(Y_k); // [FIX] 防止中间状态越界
                eval_full_rhs(Y_k, RHS);
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) b[i] = dt * RHS[i] + c41 * u1[i] + c42 * u2[i] + c43 * u3[i];
                LinearSolver::template solve_with_factors<NEQ, MAX_N>(A, p, b);
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) { u4[i] = b[i]; if (!std::isfinite(u4[i])) solve_failed = true; }
            }

           // ================= 状态拼装与物理校验 =================
            if (!solve_failed)
            {
                bool admissible = true;
                double mass_sum = 0.0;

#pragma omp simd
                for (int i = 0; i < NUM_SPEC; ++i) {
                    Y_trial[i] = Y_old[i] + m1 * u1[i] + m2 * u2[i] + m3 * u3[i] + m4 * u4[i];
                    Y_err[i]   = e1 * u1[i] + e2 * u2[i] + e3 * u3[i] + e4 * u4[i];
                    
                    if (!std::isfinite(Y_trial[i])) {
                        admissible = false;
                    } 
                    else {
                        if (Y_trial[i] < burn_cfg.smallx) {
                            Y_trial[i] = burn_cfg.smallx; 
                        } else if (Y_trial[i] > 1.0) {
                            Y_trial[i] = 1.0; 
                        }
                    }
                    mass_sum += Y_trial[i];
                }

                Y_trial[NEQ - 1] = Y_old[NEQ - 1] + m1 * u1[NEQ - 1] + m2 * u2[NEQ - 1] + m3 * u3[NEQ - 1] + m4 * u4[NEQ - 1];
                Y_err[NEQ - 1]   = e1 * u1[NEQ - 1] + e2 * u2[NEQ - 1] + e3 * u3[NEQ - 1] + e4 * u4[NEQ - 1];

                if (!std::isfinite(Y_trial[NEQ - 1]) || Y_trial[NEQ - 1] < burn_cfg.smallt || Y_trial[NEQ - 1] > 1.0e11
                    || !std::isfinite(mass_sum) || mass_sum <= 0.0) {
                    admissible = false;
                }

               if (admissible)
                {
                    // 1. 【严苛数学】: 包含所有微量元素的全局截断误差评估
                    OdeMath::calc_weights<NEQ>(Y_trial, rtol, atol, W);
                    current_err = OdeMath::wrms_norm<NEQ>(Y_err, W);

                    if (current_err < 1.0)
                    {
                        // 2. 【严苛物理】: 质量守恒绝对归一化投影
                        double projected_sum = 0.0;
#pragma omp simd
                        for (int i = 0; i < NUM_SPEC; ++i) {
                            Y_trial[i] = std::max(Y_trial[i], burn_cfg.smallx);
                            projected_sum += Y_trial[i];
                        }
                        const double inv_projected_sum = 1.0 / projected_sum;
#pragma omp simd
                        for (int i = 0; i < NUM_SPEC; ++i) Y_trial[i] *= inv_projected_sum;

                        // 3. 【严苛物理】: 能量严格闭包计算
                        long double nuclear_mass_delta = 0.0L;
                        for (int i = 0; i < NUM_SPEC; ++i) {
                            nuclear_mass_delta += static_cast<long double>(Y_trial[i] - Y_old[i]) / NetType::AION[i] * NetType::ENERGY_WEIGHTS[i];
                        }
                        const double integrated_enuc = NetType::ENERGY_CONVERSION * static_cast<double>(nuclear_mass_delta);
                        const double old_eint = eos.get_eint_from_T(rho, Y_old[NEQ - 1], Y_old);
                        const double new_eint = eos.get_eint_from_T(rho, Y_trial[NEQ - 1], Y_trial);
                        const double thermal_delta = new_eint - old_eint;
                        
                        // [FIX 2]: 引入机器精度的底层豁免，彻底解决灾难性相消 (Catastrophic Cancellation)
                        // 当 dt 极小导致能量变化小于浮点截断误差时，不再盲目相除。
                        const double epsilon_eint = std::max(1.0e-12 * std::abs(old_eint), 1.0e-12);

                        if (std::abs(thermal_delta) < epsilon_eint && std::abs(integrated_enuc) < epsilon_eint) {
                            // 变化量淹没在机器精度中，安全放行
                            step_converged = true;
                        } 
                        else {
                            const double closure_scale = std::max({std::abs(integrated_enuc), std::abs(thermal_delta), rtol * std::abs(old_eint), 1.0});
                            const double closure_error = std::abs(thermal_delta - integrated_enuc) / closure_scale;

                            // 恢复 5% 的严苛物理标准
                            if (std::isfinite(closure_error) && closure_error <= 5.0e-2) {
                                step_converged = true; 
                            }
                        }
                    }
                }
            }

            // ================= 步长控制与状态更新 =================
            if (step_converged)
            {
                t_current += dt;
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) Y_ODE[i] = Y_trial[i];

                double dt_new = OdeMath::pi_controller(current_err, err_prev, dt, 4,
                                                       burn_cfg.odeconfig.dt_safe_factor,
                                                       burn_cfg.odeconfig.dt_fac_min,
                                                       burn_cfg.odeconfig.dt_fac_max);
                dt_new = std::max(dt * burn_cfg.odeconfig.dt_fac_min, std::min(dt * burn_cfg.odeconfig.dt_fac_max, dt_new));
                dt = dt_new * burn_cfg.odeconfig.dt_safe_factor;
                err_prev = std::max(current_err, 1e-4); 
            }
            else
            {
                dt *= 0.25;
                nse_attempted = false;
                if (dt < 1e-22)
                {
                    std::cerr << "[ROS4] Fatal Error: Stiff ODE stalled. dt < 1e-22" << std::endl;
                    return false;
                }
            }
        }

        dt_rec = dt; 
        return true; 
    }
};