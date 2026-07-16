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


template <typename NetType, typename MatrixType, typename LinearSolver>
struct Solver_BE_NR
{
    static constexpr int NEQ = NetType::ODE_NEQ;
    static constexpr int NUM_SPEC = NetType::NUM_SPECIES; // 需要知道多少个是组分，用于质量守恒
    static constexpr int MAX_N = BurnLimits::MAX_ODE_NEQ;

    template <typename EOSType>
    static bool integrate(double *Y_ODE, double rho, double dt_target, const EOSType &eos,
                          const BurnConfig &burn_cfg, double &dt_rec)
    {
        if (Y_ODE[NEQ - 1] < burn_cfg.nuclearTempMin || rho < burn_cfg.nuclearDensMin)
        {
            return true;
        }

        double Y_old[MAX_N], Y_k[MAX_N], Y_trial[MAX_N];
        double RHS[MAX_N], b[MAX_N], W[MAX_N];
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
        bool nse_attempted = false;

        // ================= 外层循环：时间子步进推进 =================
        while (t_current < dt_target)
        {
            // Replace the old composition-freeze bypass with an actual
            // network-constrained Timmes NSE projection.  The projection is
            // coupled to the EOS so binding-energy release/absorption changes
            // temperature conservatively.  A failed projection leaves Y_ODE
            // untouched and falls back to the ordinary stiff ODE path.
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
                std::cerr << "[BE-NR] Fatal Error: Exceeded max substeps (" << max_substeps << ")" << std::endl;
                return false;
            }

            // 确保最后一步正好到达目标时间
            if (t_current + dt > dt_target)
            {
                dt = dt_target - t_current;
            }

            // 保存这一小步的起点状态
#pragma omp simd
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

                // Timmes network derivatives: composition block, nuclear-energy
                // derivatives, and the full analytic temperature column.
                double T_current = Y_k[NEQ - 1];
                double denuc_dX[MAX_N]{};
                double dRHS_dT[MAX_N]{};
                double denuc_dT = 0.0;
                NetType::eval_jacobian(Y_k, rho, A, denuc_dX);
                NetType::eval_temperature_derivative(Y_k, rho, dRHS_dT, denuc_dT);

                // Match the original Timmes self-heating Jacobian exactly:
                // dT/dt = enuc/cv and J_T,* = J_enuc,*/cv.  Timmes obtains cv
                // analytically from Helmholtz but does not differentiate cv in
                // the ODE Jacobian.  Temperature itself is never perturbed here.
                const double cv = std::max(eos.get_cv(rho, T_current, Y_k),
                                           1.0e-10);
                const double inv_cv = 1.0 / cv;
                RHS[NEQ - 1] = enuc * inv_cv;

#pragma omp simd
                for (int i = 0; i < NUM_SPEC; ++i) {
                    A.set(i + 1, NEQ, dRHS_dT[i]);
                }
                for (int j = 0; j < NUM_SPEC; ++j) {
                    A.set(NEQ, j + 1, denuc_dX[j] * inv_cv);
                }
                A.set(NEQ, NEQ, denuc_dT * inv_cv);

                // 4. 数学拼装：A = I - dt*J, b = Y_old - Y_k + dt*RHS
                for (int i = 0; i < NEQ; ++i)
                {
                    b[i] = Y_old[i] - Y_k[i] + dt * RHS[i];
#pragma omp simd
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
                bool admissible = true;
                double mass_sum = 0.0;
                const double negative_tolerance = 10.0 * atol;
                for (int i = 0; i < NUM_SPEC; ++i) {
                    Y_trial[i] = Y_k[i] + b[i];
                    if (!std::isfinite(Y_trial[i])
                        || Y_trial[i] < -negative_tolerance
                        || Y_trial[i] > 1.0 + negative_tolerance) {
                        admissible = false;
                    }
                    mass_sum += Y_trial[i];
                }
                Y_trial[NEQ - 1] = Y_k[NEQ - 1] + b[NEQ - 1];
                if (!std::isfinite(Y_trial[NEQ - 1])
                    || Y_trial[NEQ - 1] < burn_cfg.smallt
                    || Y_trial[NEQ - 1] > 1.0e11
                    || !std::isfinite(mass_sum) || mass_sum <= 0.0
                    || std::abs(mass_sum - 1.0) > 100.0 * rtol) {
                    admissible = false;
                }
                if (!admissible) break;

                // 6. 物理边界截断器兜底！(防止迭代中途出现负质量或绝对零度)
                // 7. 使用 WRMS 范数计算更新量 dY 的加权误差
                current_err = OdeMath::wrms_norm<NEQ>(b, W);

                // 根据 WRMS 规范，误差 < 1.0 即可认为收敛（有时用更严的 0.1）
                if (current_err < 1.0)
                {
                    double projected_sum = 0.0;
                    for (int i = 0; i < NUM_SPEC; ++i) {
                        Y_trial[i] = std::max(Y_trial[i], burn_cfg.smallx);
                        projected_sum += Y_trial[i];
                    }
                    const double inv_projected_sum = 1.0 / projected_sum;
                    for (int i = 0; i < NUM_SPEC; ++i) {
                        Y_trial[i] *= inv_projected_sum;
                    }

                    long double nuclear_mass_delta = 0.0L;
                    for (int i = 0; i < NUM_SPEC; ++i) {
                        nuclear_mass_delta +=
                            static_cast<long double>(Y_trial[i] - Y_old[i])
                            / NetType::AION[i] * NetType::ENERGY_WEIGHTS[i];
                    }
                    const double integrated_enuc = NetType::ENERGY_CONVERSION
                        * static_cast<double>(nuclear_mass_delta);
                    const double old_eint = eos.get_eint_from_T(
                        rho, Y_old[NEQ - 1], Y_old);
                    const double new_eint = eos.get_eint_from_T(
                        rho, Y_trial[NEQ - 1], Y_trial);
                    const double thermal_delta = new_eint - old_eint;
                    const double closure_scale = std::max(
                        {std::abs(integrated_enuc), std::abs(thermal_delta),
                         rtol * std::abs(old_eint), 1.0});
                    const double closure_error =
                        std::abs(thermal_delta - integrated_enuc) / closure_scale;
                    if (!std::isfinite(closure_error) || closure_error > 5.0e-2) {
                        break;
                    }

                    for (int i = 0; i < NEQ; ++i) Y_k[i] = Y_trial[i];
                    step_converged = true;
                    break;
                }

                for (int i = 0; i < NEQ; ++i) Y_k[i] = Y_trial[i];
            }
            // ==================================================

            // ================= 步长控制与状态更新 =================
            if (step_converged)
            {
                // 迭代成功：接受更新，时间向前推进
                t_current += dt;
#pragma omp simd
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

        dt_rec = dt; // 将最后一次成功并且被 PI 控制器计算出的稳定步长返回
        return true; // 成功跨越了宏观的 dt_target
    }
};