/**
 * @file ode_be-nr.h
 * @brief Unified concept for ODE Integrators using Backward Euler + Newton-Raphson with Adaptive Sub-stepping.
 */
#pragma once
#include <cmath>
#include <algorithm>
#include <iostream>

#include "Networks.h"
#include "odeFunction.h"

#include "../../data/GlobalDefs.h"

template <typename NetType, typename MatrixType, typename LinearSolver>
struct Solver_BE_NR
{
    static constexpr int NEQ = NetType::ODE_NEQ;
    static constexpr int NUM_SPEC = NetType::NUM_SPECIES; // 需要知道多少个是组分，用于质量守恒
    static constexpr int MAX_N = BurnLimits::MAX_ODE_NEQ;

    template <typename EOSType>
    static bool integrate(double *Y_ODE, double rho, double dt_target, const EOSType &eos,
                          const BurnConfig &burn_cfg)
    {
        if (Y_ODE[NEQ - 1] < burn_cfg.burn_temp_min || rho < burn_cfg.burn_rho_min)
        {
            return true;
        }

        double Y_old[MAX_N], Y_k[MAX_N], RHS[MAX_N], b[MAX_N], W[MAX_N];
        MatrixType A;

        // 求解器控制参数
        const double rtol = burn_cfg.odeconfig.rtol;
        const double atol = burn_cfg.odeconfig.atol;
        const int max_newton_iter = burn_cfg.odeconfig.max_newton_iter;
        const int max_substeps = burn_cfg.odeconfig.max_substeps;

        double t_current = 0.0;
        double dt = std::min(dt_target, dt_target * burn_cfg.odeconfig.initial_dt_frac); // 初始试探步长，通常设置得很小
        double err_prev = 1.0;
        int substep_count = 0; // 给 PI 控制器记录上一步误差

        // ================= 外层循环：时间子步进推进 =================
        while (t_current < dt_target)
        {
            // 确保最后一步正好到达目标时间
            if (t_current + dt > dt_target)
            {
                dt = dt_target - t_current;
            }

            // 保存这一小步的起点状态
            for (int i = 0; i < NEQ; ++i)
            {
                Y_old[i] = Y_ODE[i];
                Y_k[i] = Y_ODE[i];
            }

            bool step_converged = false;
            double current_err = 0.0;

            // ================= 内层循环：牛顿迭代 =================
            for (int iter = 0; iter < max_newton_iter; ++iter)
            {
                double enuc = 0.0;
                A.zero();

                // 1. 调用物理策略求导
                NetType::eval_rhs(Y_k, rho, RHS, enuc);

                // 计算当前温度下的比热容 C_v 并填充 RHS 的最后一维 (dT/dt)
                double T_current = Y_k[NEQ - 1];
                double cv = eos.get_cv(rho, T_current, Y_k);
                cv = std::max(cv, 1e-10); // 防止除 0
                RHS[NEQ - 1] = enuc / cv;

                // 2. 获取关于组分的 Jacobian (不含温度列)
                NetType::eval_jacobian(Y_k, rho, A);

                // 3. 用有限差分法补充 Jacobian 的最后一列 (关于温度的偏导)
                double dT_fd = std::max(T_current * 1e-3, 1.0);
                Y_k[NEQ - 1] += dT_fd;
                
                double RHS_fd[MAX_N];
                double enuc_fd = 0.0;
                NetType::eval_rhs(Y_k, rho, RHS_fd, enuc_fd);
                
                double cv_fd = eos.get_cv(rho, Y_k[NEQ - 1], Y_k);
                cv_fd = std::max(cv_fd, 1e-10);
                RHS_fd[NEQ - 1] = enuc_fd / cv_fd;
                
                Y_k[NEQ - 1] = T_current; // 恢复温度
                
                double inv_dT = 1.0 / dT_fd;
                for (int i = 0; i < NEQ; ++i)
                {
                    double dRHS_dT = (RHS_fd[i] - RHS[i]) * inv_dT;
                    A.set(i + 1, NEQ, dRHS_dT);
                }

                // 计算最后一行 (关于各个组分 Y_j 的偏导)
                for (int j = 0; j < NUM_SPEC; ++j)
                {
                    double Y_old_val = Y_k[j];
                    double dY_fd = std::max(Y_old_val * 1e-6, 1e-8);
                    Y_k[j] += dY_fd;
                    
                    double RHS_fd_Y[MAX_N];
                    double enuc_fd_Y = 0.0;
                    NetType::eval_rhs(Y_k, rho, RHS_fd_Y, enuc_fd_Y);
                    
                    double cv_fd_Y = eos.get_cv(rho, T_current, Y_k);
                    cv_fd_Y = std::max(cv_fd_Y, 1e-10);
                    RHS_fd_Y[NEQ - 1] = enuc_fd_Y / cv_fd_Y;
                    
                    Y_k[j] = Y_old_val; // 恢复
                    
                    double inv_dY = 1.0 / dY_fd;
                    A.set(NEQ, j + 1, (RHS_fd_Y[NEQ - 1] - RHS[NEQ - 1]) * inv_dY);
                }

                // 4. 数学拼装：A = I - dt*J, b = Y_old - Y_k + dt*RHS
                for (int i = 0; i < NEQ; ++i)
                {
                    b[i] = Y_old[i] - Y_k[i] + dt * RHS[i];
                    for (int j = 0; j < NEQ; ++j)
                    {
                        double jac_val = A(i + 1, j + 1);
                        A.set(i + 1, j + 1, -dt * jac_val);
                    }
                    A.set(i + 1, i + 1, A(i + 1, i + 1) + 1.0);
                }

                // 3. 求解线性方程组
                bool success = LinearSolver::template solve<NEQ, MAX_N>(A, b);
                if (!success)
                {
                    break; // 矩阵奇异，直接跳出内循环，要求外循环缩小 dt
                }

                // [核心修复]：检查解出的更新量 b 是否包含 NaN！
                // 如果包含 NaN 或 Inf，说明虽然矩阵分解成功了，但数值已经爆炸，必须立刻中断并缩小步长
                bool has_nan = false;
                for (int i = 0; i < NEQ; ++i)
                {
                    if (!std::isfinite(b[i]))
                    {
                        has_nan = true;
                        break;
                    }
                }
                if (has_nan)
                {
                    break;
                }

                // 4. 计算当前状态的权重 (用于评估 b 也就是 dY 的误差)
                OdeMath::calc_weights<NEQ>(Y_k, rtol, atol, W);

                // 5. 向量更新：Y_{k+1} = Y_k + b
                OdeMath::vec_axpy<NEQ>(Y_k, 1.0, b, Y_k);

                // 6. 物理边界截断器兜底！(防止迭代中途出现负质量或绝对零度)
                OdeMath::enforce_mass_conservation<NUM_SPEC>(Y_k, 1e-20);
                OdeMath::enforce_temperature_bounds<NEQ>(Y_k, 1e6, 1e10);

                // 7. 使用 WRMS 范数计算更新量 dY 的加权误差
                current_err = OdeMath::wrms_norm<NEQ>(b, W);

                // 根据 WRMS 规范，误差 < 1.0 即可认为收敛（有时用更严的 0.1）
                if (current_err < 1.0)
                {
                    step_converged = true;
                    break;
                }
            }
            // ==================================================

            // ================= 步长控制与状态更新 =================
            if (step_converged)
            {
                // 迭代成功：接受更新，时间向前推进
                t_current += dt;
                for (int i = 0; i < NEQ; ++i)
                {
                    Y_ODE[i] = Y_k[i];
                }

                // 1. 使用 PI 控制器计算初步的理想步长
                double dt_new = OdeMath::pi_controller(current_err, err_prev, dt,
                                                       1,
                                                       burn_cfg.odeconfig.dt_safe_factor,
                                                       burn_cfg.odeconfig.dt_fac_min,
                                                       burn_cfg.odeconfig.dt_fac_max);

                // 2. 强行截断放缩比例，防止步长剧烈震荡
                dt_new = std::max(dt * burn_cfg.odeconfig.dt_fac_min,
                                  std::min(dt * burn_cfg.odeconfig.dt_fac_max, dt_new));

                // 3. 乘以安全系数，保守推进
                dt = dt_new * burn_cfg.odeconfig.dt_safe_factor;
                err_prev = current_err;
            }
            else
            {
                // 迭代失败 (发散或奇异)：拒绝更新，砍掉步长重新算这一步
                dt *= 0.25;

                // 如果 dt 已经小到物理极限依然发散，则报错退出防止死循环
                if (dt < 1e-22)
                {
                    std::cerr << "[BE-NR] Fatal Error: Stiff ODE stalled. dt < 1e-22" << std::endl;
                    return false;
                }
            }
        }
        // ==================================================

        return true; // 成功跨越了宏观的 dt_target
    }
};