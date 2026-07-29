/**
 * @file ode_bd.h
 * @brief Unified concept for ODE Integrators using Bader-Deuflhard Semi-Implicit Extrapolation.
 */
#pragma once
#include <cmath>
#include <algorithm>
#include <iostream>

#include "Networks.h"
#include "odeFunction.h"

template <typename NetType, typename MatrixType, typename LinearSolver>
struct Solver_BD
{
    static constexpr int NEQ = NetType::ODE_NEQ;
    static constexpr int NUM_SPEC = NetType::NUM_SPECIES;
    static constexpr int MAX_N = BurnLimits::MAX_ODE_NEQ;
    
    // BD 序列最多推 7 阶（对应 8 个节点），避免龙格现象
    static constexpr int MAX_K = 7; 
    // 标准 Deuflhard 调和序列 (Roman's Sequence 变体)
    static constexpr int n_seq[MAX_K] = {2, 6, 10, 14, 22, 34, 50}; 
    // 工作量估算：由于一次外推中 Jacobian 只算一次，计算量主要在解线性方程和算 RHS
    static constexpr double work_cost[MAX_K] = {2.0, 8.0, 18.0, 32.0, 54.0, 88.0, 138.0};

    template <typename EOSType>
    static bool integrate(double *Y_ODE, double rho, double dt_target, const EOSType &eos,
                          const BurnConfig &burn_cfg, double &dt_rec)
    {
        if (Y_ODE[NEQ - 1] < burn_cfg.nuclearTempMin || rho < burn_cfg.nuclearDensMin) {
            return true;
        }

        const double rtol = burn_cfg.odeconfig.rtol;
        const double atol = burn_cfg.odeconfig.atol;
        const int max_substeps = burn_cfg.odeconfig.max_substeps;

        double t_current = 0.0;
        double H = std::min(dt_target, dt_target * burn_cfg.odeconfig.initial_dt_frac);
        int substep_count = 0;
        bool nse_attempted = false;

        // BD 专属变量
        double T_extrap[MAX_K][MAX_K][NEQ];
        double err_fac[MAX_K];
        double W[MAX_N], Y_err[MAX_N], Y_trial[MAX_N];
        double RHS[MAX_N], b[MAX_N], delta[MAX_N], x_j[MAX_N], Y_j[MAX_N];
        MatrixType J_mat, A;
        int p[MAX_N]; // LU 分解主元

        auto sanitize_state = [&](double* Y_state) {
            for (int i = 0; i < NUM_SPEC; ++i) {
                if (Y_state[i] < burn_cfg.smallx) Y_state[i] = burn_cfg.smallx;
                else if (Y_state[i] > 1.0) Y_state[i] = 1.0;
            }
            if (Y_state[NEQ - 1] < burn_cfg.nuclearTempMin) {
                Y_state[NEQ - 1] = burn_cfg.nuclearTempMin;
            }
        };

        // ================= 外层循环：推进宏观时间步 H =================
        while (t_current < dt_target)
        {
            if (!nse_attempted && burn_cfg.use_nse && Y_ODE[NEQ - 1] > burn_cfg.nseTempThreshold && rho > burn_cfg.nseDensThreshold) {
                nse_attempted = true;
                if (OdeMath::integrate_nse_state<NetType, EOSType>(Y_ODE, rho, dt_target, eos, burn_cfg, dt_rec)) return true;
            }

            substep_count++;
            if (substep_count > max_substeps) {
                std::cerr << "[BD] Fatal Error: Exceeded max substeps." << std::endl;
                return false;
            }

            if (t_current + H > dt_target) H = dt_target - t_current;

            // 1. 宏观步长开始，只计算【唯一一次】全局 Jacobian
            double enuc = 0.0;
            double T_current = Y_ODE[NEQ - 1];
            double eta = eos.get_eta(rho, T_current, Y_ODE);
            NetType::eval_rhs(Y_ODE, rho, eta, RHS, enuc);

            J_mat.zero();
            double denuc_dX[MAX_N]{};
            double dRHS_dT[MAX_N]{};
            double denuc_dT = 0.0;
            double eta_jac = eos.get_eta(rho, T_current, Y_ODE);
            NetType::eval_jacobian(Y_ODE, rho, eta_jac, J_mat, denuc_dX);
            NetType::eval_temperature_derivative(Y_ODE, rho, eta_jac, dRHS_dT, denuc_dT);

            const double cv = std::max(eos.get_cv(rho, T_current, Y_ODE), 1.0e-10);
            const double inv_cv = 1.0 / cv;
            RHS[NEQ - 1] = enuc * inv_cv;

#pragma omp simd
            for (int i = 0; i < NUM_SPEC; ++i) J_mat.set(i + 1, NEQ, dRHS_dT[i]);
#pragma omp simd
            for (int j = 0; j < NUM_SPEC; ++j) J_mat.set(NEQ, j + 1, denuc_dX[j] * inv_cv);
            J_mat.set(NEQ, NEQ, denuc_dT * inv_cv);

            OdeMath::calc_weights<NEQ>(Y_ODE, rtol, atol, W);

            bool step_converged = false;
            int optimal_k = 0;
            double current_err = 0.0;

            // ================= 内层循环：建立阶数 k 的外推表 =================
            for (int k = 0; k < MAX_K; ++k)
            {
                int m = n_seq[k];
                double h = H / m;

                // 构造矩阵 A = I - h * J
                for (int i = 0; i < NEQ; ++i) {
#pragma omp simd
                    for (int j = 0; j < NEQ; ++j) {
                        A.set(i + 1, j + 1, -h * J_mat(i + 1, j + 1));
                    }
                    A.set(i + 1, i + 1, A(i + 1, i + 1) + 1.0);
                }

                if (!LinearSolver::template factorize<NEQ, MAX_N>(A, p)) {
                    // 矩阵奇异，当前 H 太大，直接退出内层循环
                    break; 
                }

                // --------- 半隐式中点法则 (Semi-Implicit Midpoint Rule) ---------
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) {
                    b[i] = h * RHS[i];
                    Y_j[i] = Y_ODE[i];
                }
                LinearSolver::template solve_with_factors<NEQ, MAX_N>(A, p, b);
                
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) {
                    delta[i] = b[i];
                    Y_j[i] += delta[i];
                }
                sanitize_state(Y_j); // 保护内部状态不爆炸

                // 中间游走
                bool simpr_failed = false;
                for (int j = 1; j < m; ++j)
                {
                    double stage_enuc = 0.0;
                    double stage_RHS[MAX_N];
                    double eta = eos.get_eta(rho, Y_j[NEQ - 1], Y_j);
                    NetType::eval_rhs(Y_j, rho, eta, stage_RHS, stage_enuc);
                    double stage_cv = std::max(eos.get_cv(rho, Y_j[NEQ - 1], Y_j), 1.0e-10);
                    stage_RHS[NEQ - 1] = stage_enuc / stage_cv;

#pragma omp simd
                    for (int i = 0; i < NEQ; ++i) {
                        b[i] = h * stage_RHS[i] - delta[i];
                    }
                    LinearSolver::template solve_with_factors<NEQ, MAX_N>(A, p, b);
                    
#pragma omp simd
                    for (int i = 0; i < NEQ; ++i) {
                        x_j[i] = b[i];
                        if (!std::isfinite(x_j[i])) simpr_failed = true;
                        Y_j[i] += delta[i] + 2.0 * x_j[i];
                        delta[i] += 2.0 * x_j[i];
                    }
                    if (simpr_failed) break;
                    sanitize_state(Y_j);
                }
                if (simpr_failed) break;

                // 终点平滑
                double end_enuc = 0.0;
                double end_RHS[MAX_N];
                double eta = eos.get_eta(rho, Y_j[NEQ - 1], Y_j);
                NetType::eval_rhs(Y_j, rho, eta, end_RHS, end_enuc);
                double end_cv = std::max(eos.get_cv(rho, Y_j[NEQ - 1], Y_j), 1.0e-10);
                end_RHS[NEQ - 1] = end_enuc / end_cv;

#pragma omp simd
                for (int i = 0; i < NEQ; ++i) {
                    b[i] = h * end_RHS[i] - delta[i];
                }
                LinearSolver::template solve_with_factors<NEQ, MAX_N>(A, p, b);
                
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) {
                    T_extrap[k][0][i] = Y_j[i] + b[i]; // 写入外推表起点
                }
                // -----------------------------------------------------------------

                // 执行多项式外推，并获取误差
                OdeMath::bd_extrapolate<NEQ, MAX_K>(k, n_seq, T_extrap, Y_err);

                // 只在 k > 0 时才评估误差
                if (k > 0)
                {
                    current_err = OdeMath::wrms_norm<NEQ>(Y_err, W);
                    
                    // 【严苛物理】将数学试探解装载入 Y_trial 准备物理验收
#pragma omp simd
                    for(int i=0; i<NEQ; ++i) Y_trial[i] = T_extrap[k][k][i];
                    
                    bool physically_sound = true;
                    double mass_sum = 0.0;
                    for (int i = 0; i < NUM_SPEC; ++i) {
                        if(Y_trial[i] < -10.0*atol || !std::isfinite(Y_trial[i])) physically_sound = false;
                        mass_sum += std::max(Y_trial[i], burn_cfg.smallx);
                    }
                    if(!std::isfinite(Y_trial[NEQ - 1]) || Y_trial[NEQ - 1] < burn_cfg.smallt) physically_sound = false;

                    // 只有数学误差合格，且不产生无穷大，才进入昂贵的 EOS 物理闭包检查
                    if (physically_sound && current_err < 1.0)
                    {
                        const double inv_sum = 1.0 / mass_sum;
#pragma omp simd
                        for (int i = 0; i < NUM_SPEC; ++i) Y_trial[i] = std::max(Y_trial[i], burn_cfg.smallx) * inv_sum;

                        long double nuclear_mass_delta = 0.0L;
                        for (int i = 0; i < NUM_SPEC; ++i) {
                            nuclear_mass_delta += static_cast<long double>(Y_trial[i] - Y_ODE[i]) / NetType::AION[i] * NetType::ENERGY_WEIGHTS[i];
                        }
                        const double integrated_enuc = NetType::ENERGY_CONVERSION * static_cast<double>(nuclear_mass_delta);
                        const double old_eint = eos.get_eint_from_T(rho, Y_ODE[NEQ - 1], Y_ODE);
                        const double new_eint = eos.get_eint_from_T(rho, Y_trial[NEQ - 1], Y_trial);
                        const double thermal_delta = new_eint - old_eint;
                        
                        const double epsilon_eint = std::max(1.0e-12 * std::abs(old_eint), 1.0e-12);
                        if (std::abs(thermal_delta) < epsilon_eint && std::abs(integrated_enuc) < epsilon_eint) {
                            step_converged = true;
                        } 
                        else {
                            const double closure_scale = std::max({std::abs(integrated_enuc), std::abs(thermal_delta), rtol * std::abs(old_eint), 1.0});
                            const double closure_error = std::abs(thermal_delta - integrated_enuc) / closure_scale;

                            // 【物理验收大门】：误差必须 < 5%
                            if (std::isfinite(closure_error) && closure_error <= 5.0e-2) {
                                step_converged = true;
                            }
                        }
                    }

                    // 记录此阶的最佳缩小/放大系数 (Hairer Wanner Formula)
                    err_fac[k] = std::pow(current_err, 1.0 / (2 * k + 1));
                    err_fac[k] = std::max(burn_cfg.odeconfig.dt_fac_min, std::min(burn_cfg.odeconfig.dt_fac_max, 1.0 / err_fac[k]));

                    if (step_converged) {
                        optimal_k = k;
                        break; // 只要收敛了，立刻退出内层循环
                    }
                    else if (k > 1 && k + 1 < MAX_K
                             && current_err > std::pow(
                                    static_cast<double>(n_seq[k + 1]) / n_seq[0], 2)) {
                        // 启示式规则：如果误差大得离谱，继续提高阶数也无救，尽早退出并砍步长
                        break;
                    }
                }
            }
            // =========================================================

            // ================= 步长控制与状态更新 =================
            if (step_converged)
            {
                t_current += H;
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) Y_ODE[i] = Y_trial[i];

                // Deuflhard 动态阶数策略：计算使用哪一阶能让未来的效率 (步长/工作量) 最高
                double work_min = 1.0e20;
                int k_next = optimal_k;
                
                // 向下寻找可能的更高效率阶数
                for (int k = 1; k <= optimal_k; ++k) {
                    double step_for_k = H * err_fac[k] * 0.9; // 0.9 为安全因子
                    double work_k = work_cost[k] / step_for_k;
                    if (work_k < work_min) {
                        work_min = work_k;
                        k_next = k;
                    }
                }
                
                // 推测尝试更高一阶是否有益
                if (optimal_k < MAX_K - 1) {
                    double err_est = err_fac[optimal_k] * (static_cast<double>(n_seq[optimal_k + 1]) / n_seq[optimal_k]);
                    double step_higher = H * err_est * 0.9;
                    double work_higher = work_cost[optimal_k + 1] / step_higher;
                    if (work_higher < work_min) {
                        k_next = optimal_k + 1;
                    }
                }

                // 选定新步长并限制爆炸
                double H_new = H * err_fac[k_next] * 0.9;
                H_new = std::max(H * burn_cfg.odeconfig.dt_fac_min, std::min(H * burn_cfg.odeconfig.dt_fac_max, H_new));
                H = H_new;
            }
            else
            {
                // 如果推进到最高阶依然数学/物理验收失败，则断崖式降低宏观步长
                H *= 0.25;
                nse_attempted = false;
                if (H < 1e-22)
                {
                    std::cerr << "[BD] Fatal Error: Stiff ODE stalled. H < 1e-22" << std::endl;
                    return false;
                }
            }
        }

        dt_rec = H; 
        return true; 
    }
};
