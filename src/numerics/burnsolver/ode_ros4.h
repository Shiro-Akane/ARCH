/**
 * @file ode_ros4.h
 * @brief Four-stage L-stable ROS4 integrator with a shared diagonal matrix.
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <iostream>

#include "Networks.h"
#include "odeFunction.h"

template <typename NetType, typename MatrixType, typename LinearSolver>
struct Solver_ROS4
{
    static constexpr int NEQ = NetType::ODE_NEQ;
    static constexpr int NUM_SPEC = NetType::NUM_SPECIES;
    static constexpr int MAX_N = NEQ;

    // Four-stage, fourth-order, L-stable ROS4 tableau.  The coefficients are
    // a matched set; changing gamma independently violates the order conditions.
    static constexpr double gamma = 0.57282;
    static constexpr double a21 = 2.0;
    static constexpr double a31 = 1.867943637803922;
    static constexpr double a32 = 0.2344449711399156;
    static constexpr double a41 = a31;
    static constexpr double a42 = a32;
    static constexpr double a43 = 0.0;
    static constexpr double c21 = -7.137615036412310;
    static constexpr double c31 = 2.580708087951457;
    static constexpr double c32 = 0.6515950076447975;
    static constexpr double c41 = -2.137148994382534;
    static constexpr double c42 = -0.3214669691237626;
    static constexpr double c43 = -0.6949742501781779;
    static constexpr double m1 = 2.255570073418735;
    static constexpr double m2 = 0.2870493262186792;
    static constexpr double m3 = 0.4353179431840180;
    static constexpr double m4 = 1.093502252409163;
    static constexpr double e1 = -0.2815431932141155;
    static constexpr double e2 = -0.0727619912493892;
    static constexpr double e3 = -0.1082196201495311;
    static constexpr double e4 = -1.093502252409163;

    template <typename EOSType>
    static bool integrate(double *X_ODE, double rho, double dt_target, const EOSType &eos,
                          const BurnConfig &burn_cfg, double &dt_rec)
    {
        if (X_ODE[NEQ - 1] < burn_cfg.nuclearTempMin || rho < burn_cfg.nuclearDensMin)
        {
            return true;
        }

        double X_old[MAX_N], X_k[MAX_N], X_trial[MAX_N];
        double RHS[MAX_N], b[MAX_N], W[MAX_N], X_err[MAX_N];
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

        // Full RHS evaluation for each Rosenbrock stage.
        auto eval_full_rhs = [&](const double* Y, double* out_RHS) {
            double enuc = 0.0;
            double eta = eos.get_eta(rho, Y[NEQ - 1], Y);
            NetType::eval_rhs(Y, rho, eta, out_RHS, enuc);
            double cv = std::max(eos.get_cv(rho, Y[NEQ - 1], Y), 1e-10);
            out_RHS[NEQ - 1] = enuc / cv;
        };

        // Bound intermediate stage states before RHS evaluation.
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

        // Advance with adaptive internal substeps.
        while (t_current < dt_target)
        {
            if constexpr (NetType::SUPPORTS_NSE) {
            if (!nse_attempted && burn_cfg.use_nse
                && X_ODE[NEQ - 1] > burn_cfg.nseTempThreshold
                && rho > burn_cfg.nseDensThreshold)
            {
                nse_attempted = true;
                if (OdeMath::integrate_nse_state<NetType, EOSType>(X_ODE, rho, dt_target, eos,
                                                        burn_cfg, dt_rec)) {
                    return true;
                }
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
            for (int i = 0; i < NEQ; ++i) X_old[i] = X_ODE[i];

            // Evaluate f(y_0).
            eval_full_rhs(X_old, RHS);

            // Assemble the analytic Jacobian, including temperature coupling.
            J_mat.zero();
            double T_current = X_old[NEQ - 1];
            double denuc_dX[MAX_N]{};
            double dRHS_dT[MAX_N]{};
            double denuc_dT = 0.0;
            double eta_jac = eos.get_eta(rho, T_current, X_old);
            NetType::eval_jacobian(X_old, rho, eta_jac, J_mat, denuc_dX);
            NetType::eval_temperature_derivative(X_old, rho, eta_jac, dRHS_dT, denuc_dT);

            const double cv = std::max(eos.get_cv(rho, T_current, X_old), 1.0e-10);
            const double inv_cv = 1.0 / cv;

#pragma omp simd
            for (int i = 0; i < NUM_SPEC; ++i) J_mat.set(i + 1, NEQ, dRHS_dT[i]);
#pragma omp simd
            for (int j = 0; j < NUM_SPEC; ++j) J_mat.set(NEQ, j + 1, denuc_dX[j] * inv_cv);
            J_mat.set(NEQ, NEQ, denuc_dT * inv_cv);

            // The normalized form of (I / (gamma*dt) - J) uses
            // A = I - gamma*dt*J and scales every stage right-hand side by gamma.
            A.set_shifted_identity_from(J_mat, -gamma * dt);

            // Factor the shared stage matrix once for this substep.
            int p[MAX_N];
            bool step_converged = false;
            double current_err = 0.0;

            if (!LinearSolver::template factorize<NEQ, MAX_N>(A, p))
            {
                // A singular shared matrix rejects the trial stiffness scale;
                // a factor-of-four reduction moves the retry well below it.
                dt *= 0.25;
                continue;
            }

            bool solve_failed = false;

            // Stage 1.
#pragma omp simd
            for (int i = 0; i < NEQ; ++i) b[i] = gamma * dt * RHS[i];
            LinearSolver::template solve_with_factors<NEQ, MAX_N>(A, p, b);
#pragma omp simd
            for (int i = 0; i < NEQ; ++i) { u1[i] = b[i]; if (!std::isfinite(u1[i])) solve_failed = true; }

            // Stage 2.
            if (!solve_failed) {
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) X_k[i] = X_old[i] + a21 * u1[i];
                sanitize_state(X_k);
                eval_full_rhs(X_k, RHS);
#pragma omp simd
                for (int i = 0; i < NEQ; ++i)
                    b[i] = gamma * (dt * RHS[i] + c21 * u1[i]);
                LinearSolver::template solve_with_factors<NEQ, MAX_N>(A, p, b);
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) { u2[i] = b[i]; if (!std::isfinite(u2[i])) solve_failed = true; }
            }

            // Stage 3.
            if (!solve_failed) {
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) X_k[i] = X_old[i] + a31 * u1[i] + a32 * u2[i];
                sanitize_state(X_k);
                eval_full_rhs(X_k, RHS);
#pragma omp simd
                for (int i = 0; i < NEQ; ++i)
                    b[i] = gamma * (dt * RHS[i] + c31 * u1[i] + c32 * u2[i]);
                LinearSolver::template solve_with_factors<NEQ, MAX_N>(A, p, b);
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) { u3[i] = b[i]; if (!std::isfinite(u3[i])) solve_failed = true; }
            }

            // Stage 4.
            if (!solve_failed) {
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) X_k[i] = X_old[i] + a41 * u1[i] + a42 * u2[i] + a43 * u3[i];
                sanitize_state(X_k);
                eval_full_rhs(X_k, RHS);
#pragma omp simd
                for (int i = 0; i < NEQ; ++i)
                    b[i] = gamma * (dt * RHS[i] + c41 * u1[i] + c42 * u2[i]
                                               + c43 * u3[i]);
                LinearSolver::template solve_with_factors<NEQ, MAX_N>(A, p, b);
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) { u4[i] = b[i]; if (!std::isfinite(u4[i])) solve_failed = true; }
            }

            // Assemble the trial state and enforce physical admissibility.
            if (!solve_failed)
            {
                bool admissible = true;
                double mass_sum = 0.0;

#pragma omp simd
                for (int i = 0; i < NUM_SPEC; ++i) {
                    X_trial[i] = X_old[i] + m1 * u1[i] + m2 * u2[i] + m3 * u3[i] + m4 * u4[i];
                    X_err[i]   = e1 * u1[i] + e2 * u2[i] + e3 * u3[i] + e4 * u4[i];

                    if (!std::isfinite(X_trial[i])) {
                        admissible = false;
                    }
                    else {
                        if (X_trial[i] < burn_cfg.smallx) {
                            X_trial[i] = burn_cfg.smallx;
                        } else if (X_trial[i] > 1.0) {
                            X_trial[i] = 1.0;
                        }
                    }
                    mass_sum += X_trial[i];
                }

                X_trial[NEQ - 1] = X_old[NEQ - 1] + m1 * u1[NEQ - 1] + m2 * u2[NEQ - 1] + m3 * u3[NEQ - 1] + m4 * u4[NEQ - 1];
                X_err[NEQ - 1]   = e1 * u1[NEQ - 1] + e2 * u2[NEQ - 1] + e3 * u3[NEQ - 1] + e4 * u4[NEQ - 1];

                // The 1e11 K upper guard bounds the Timmes EOS/network domain
                // used by the burn solvers; smallt supplies the lower guard.
                if (!std::isfinite(X_trial[NEQ - 1]) || X_trial[NEQ - 1] < burn_cfg.smallt || X_trial[NEQ - 1] > 1.0e11
                    || !std::isfinite(mass_sum) || mass_sum <= 0.0) {
                    admissible = false;
                }

               if (admissible)
                {
                    // Evaluate the weighted truncation error over all components.
                    OdeMath::calc_weights<NEQ>(X_trial, rtol, atol, W);
                    current_err = OdeMath::wrms_norm<NEQ>(X_err, W);

                    if (current_err < 1.0)
                    {
                        // Project species onto unit total mass fraction.
                        double projected_sum = 0.0;
#pragma omp simd
                        for (int i = 0; i < NUM_SPEC; ++i) {
                            X_trial[i] = std::max(X_trial[i], burn_cfg.smallx);
                            projected_sum += X_trial[i];
                        }
                        const double inv_projected_sum = 1.0 / projected_sum;
#pragma omp simd
                        for (int i = 0; i < NUM_SPEC; ++i) X_trial[i] *= inv_projected_sum;

                        // Evaluate the nuclear/thermal energy closure.
                        long double nuclear_mass_delta = 0.0L;
                        for (int i = 0; i < NUM_SPEC; ++i) {
                            nuclear_mass_delta += static_cast<long double>(X_trial[i] - X_old[i]) / NetType::AION[i] * NetType::ENERGY_WEIGHTS[i];
                        }
                        const double integrated_enuc = NetType::ENERGY_CONVERSION * static_cast<double>(nuclear_mass_delta);
                        const double old_eint = eos.get_eint_from_T(rho, X_old[NEQ - 1], X_old);
                        const double new_eint = eos.get_eint_from_T(rho, X_trial[NEQ - 1], X_trial);
                        const double thermal_delta = new_eint - old_eint;

                        // Resolve changes below the internal-energy comparison scale.
                        const double epsilon_eint = std::max(1.0e-12 * std::abs(old_eint), 1.0e-12);

                        if (std::abs(thermal_delta) < epsilon_eint && std::abs(integrated_enuc) < epsilon_eint) {
                            step_converged = true;
                        }
                        else {
                            const double closure_scale = std::max({std::abs(integrated_enuc), std::abs(thermal_delta), rtol * std::abs(old_eint), 1.0});
                            const double closure_error = std::abs(thermal_delta - integrated_enuc) / closure_scale;

                            // Five percent is the engineering energy-closure
                            // tolerance shared by the production burn solvers.
                            if (std::isfinite(closure_error) && closure_error <= 5.0e-2) {
                                step_converged = true;
                            }
                        }
                    }
                }
            }

            // Accept the state or reduce the internal step.
            if (step_converged)
            {
                t_current += dt;
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) X_ODE[i] = X_trial[i];

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
                // Quarter the rejected step before retrying the same interval.
                dt *= 0.25;
                nse_attempted = false;
                // Below 1e-22 s, double-precision time accumulation no longer
                // provides useful progress for the supported burn cases.
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
