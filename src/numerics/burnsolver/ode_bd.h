/**
 * @file ode_bd.h
 * @brief Unified concept for ODE Integrators using Bader-Deuflhard Semi-Implicit Extrapolation.
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <iostream>

#include "Networks.h"
#include "odeFunction.h"

template <typename NetType, typename MatrixType, typename LinearSolver>
struct Solver_BD
{
    static constexpr int NEQ = NetType::ODE_NEQ;
    static constexpr int NUM_SPEC = NetType::NUM_SPECIES;
    static constexpr int MAX_N = NEQ;

    // Seven extrapolation levels limit high-order polynomial oscillation while
    // covering the useful compact-network accuracy range.
    static constexpr int MAX_K = 7;
    // Deuflhard harmonic sequence using the common Roman-sequence variant.
    static constexpr int n_seq[MAX_K] = {2, 6, 10, 14, 22, 34, 50};
    // Relative work estimates count RHS evaluations and linear solves. One
    // Jacobian is shared by all midpoint substeps at a given extrapolation level.
    static constexpr double work_cost[MAX_K] = {2.0, 8.0, 18.0, 32.0, 54.0, 88.0, 138.0};

    template <typename EOSType>
    static bool integrate(double *X_ODE, double rho, double dt_target, const EOSType &eos,
                          const BurnConfig &burn_cfg, double &dt_rec)
    {
        if (X_ODE[NEQ - 1] < burn_cfg.nuclearTempMin || rho < burn_cfg.nuclearDensMin) {
            return true;
        }

        const double rtol = burn_cfg.odeconfig.rtol;
        const double atol = burn_cfg.odeconfig.atol;
        const int max_substeps = burn_cfg.odeconfig.max_substeps;

        double t_current = 0.0;
        double H = std::min(dt_target, dt_target * burn_cfg.odeconfig.initial_dt_frac);
        int substep_count = 0;
        bool nse_attempted = false;

        // Bader-Deuflhard extrapolation tableau and error workspace.
        double T_extrap[MAX_K][MAX_K][NEQ];
        double err_fac[MAX_K];
        double W[MAX_N], X_err[MAX_N], X_trial[MAX_N];
        double RHS[MAX_N], b[MAX_N], delta[MAX_N], x_j[MAX_N], X_j[MAX_N];
        MatrixType J_mat, A;
        int p[MAX_N]; // Row-pivot indices for the LU factorization.

        auto sanitize_state = [&](double* Y_state) {
            for (int i = 0; i < NUM_SPEC; ++i) {
                if (Y_state[i] < burn_cfg.smallx) Y_state[i] = burn_cfg.smallx;
                else if (Y_state[i] > 1.0) Y_state[i] = 1.0;
            }
            if (Y_state[NEQ - 1] < burn_cfg.nuclearTempMin) {
                Y_state[NEQ - 1] = burn_cfg.nuclearTempMin;
            }
        };

        // Advance the requested interval with adaptive macro steps H.
        while (t_current < dt_target)
        {
            if constexpr (NetType::SUPPORTS_NSE) {
            if (!nse_attempted && burn_cfg.use_nse && X_ODE[NEQ - 1] > burn_cfg.nseTempThreshold && rho > burn_cfg.nseDensThreshold) {
                nse_attempted = true;
                if (OdeMath::integrate_nse_state<NetType, EOSType>(X_ODE, rho, dt_target, eos, burn_cfg, dt_rec)) return true;
            }
            }

            substep_count++;
            if (substep_count > max_substeps) {
                std::cerr << "[BD] Fatal Error: Exceeded max substeps." << std::endl;
                return false;
            }

            if (t_current + H > dt_target) H = dt_target - t_current;

            // Evaluate one global Jacobian for this macro step.
            double enuc = 0.0;
            double T_current = X_ODE[NEQ - 1];
            double eta = eos.get_eta(rho, T_current, X_ODE);
            NetType::eval_rhs(X_ODE, rho, eta, RHS, enuc);

            J_mat.zero();
            double denuc_dX[MAX_N]{};
            double dRHS_dT[MAX_N]{};
            double denuc_dT = 0.0;
            double eta_jac = eos.get_eta(rho, T_current, X_ODE);
            NetType::eval_jacobian(X_ODE, rho, eta_jac, J_mat, denuc_dX);
            NetType::eval_temperature_derivative(X_ODE, rho, eta_jac, dRHS_dT, denuc_dT);

            const double cv = std::max(eos.get_cv(rho, T_current, X_ODE), 1.0e-10);
            const double inv_cv = 1.0 / cv;
            RHS[NEQ - 1] = enuc * inv_cv;

#pragma omp simd
            for (int i = 0; i < NUM_SPEC; ++i) J_mat.set(i + 1, NEQ, dRHS_dT[i]);
#pragma omp simd
            for (int j = 0; j < NUM_SPEC; ++j) J_mat.set(NEQ, j + 1, denuc_dX[j] * inv_cv);
            J_mat.set(NEQ, NEQ, denuc_dT * inv_cv);

            OdeMath::calc_weights<NEQ>(X_ODE, rtol, atol, W);

            bool step_converged = false;
            int optimal_k = 0;
            double current_err = 0.0;

            // Build one extrapolation row for each candidate order k.
            for (int k = 0; k < MAX_K; ++k)
            {
                int m = n_seq[k];
                double h = H / m;

                // Semi-implicit midpoint matrix A=I-h*J.
                A.set_shifted_identity_from(J_mat, -h);

                if (!LinearSolver::template factorize<NEQ, MAX_N>(A, p)) {
                    // A singular factorization indicates that the current H is
                    // too large for this level; leave the order loop and reduce H.
                    break;
                }

                // Semi-implicit midpoint recurrence.
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) {
                    b[i] = h * RHS[i];
                    X_j[i] = X_ODE[i];
                }
                LinearSolver::template solve_with_factors<NEQ, MAX_N>(A, p, b);

#pragma omp simd
                for (int i = 0; i < NEQ; ++i) {
                    delta[i] = b[i];
                    X_j[i] += delta[i];
                }
                sanitize_state(X_j); // Bound an internal stage before evaluating its RHS.

                // March through the remaining midpoint substeps.
                bool simpr_failed = false;
                for (int j = 1; j < m; ++j)
                {
                    double stage_enuc = 0.0;
                    double stage_RHS[MAX_N];
                    double eta = eos.get_eta(rho, X_j[NEQ - 1], X_j);
                    NetType::eval_rhs(X_j, rho, eta, stage_RHS, stage_enuc);
                    double stage_cv = std::max(eos.get_cv(rho, X_j[NEQ - 1], X_j), 1.0e-10);
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
                        X_j[i] += delta[i] + 2.0 * x_j[i];
                        delta[i] += 2.0 * x_j[i];
                    }
                    if (simpr_failed) break;
                    sanitize_state(X_j);
                }
                if (simpr_failed) break;

                // Apply the midpoint endpoint smoothing formula.
                double end_enuc = 0.0;
                double end_RHS[MAX_N];
                double eta = eos.get_eta(rho, X_j[NEQ - 1], X_j);
                NetType::eval_rhs(X_j, rho, eta, end_RHS, end_enuc);
                double end_cv = std::max(eos.get_cv(rho, X_j[NEQ - 1], X_j), 1.0e-10);
                end_RHS[NEQ - 1] = end_enuc / end_cv;

#pragma omp simd
                for (int i = 0; i < NEQ; ++i) {
                    b[i] = h * end_RHS[i] - delta[i];
                }
                LinearSolver::template solve_with_factors<NEQ, MAX_N>(A, p, b);

#pragma omp simd
                for (int i = 0; i < NEQ; ++i) {
                    T_extrap[k][0][i] = X_j[i] + b[i]; // Seed this extrapolation row.
                }

                // Extrapolate the midpoint result and obtain its truncation estimate.
                OdeMath::bd_extrapolate<NEQ, MAX_K>(k, n_seq, T_extrap, X_err);

                // At least two orders are required for an error estimate.
                if (k > 0)
                {
                    current_err = OdeMath::wrms_norm<NEQ>(X_err, W);

                    // Load the extrapolated candidate for physical checks.
#pragma omp simd
                    for(int i=0; i<NEQ; ++i) X_trial[i] = T_extrap[k][k][i];

                    bool physically_sound = true;
                    double mass_sum = 0.0;
                    // Permit negative extrapolation noise only within ten
                    // absolute-tolerance units before projection.
                    for (int i = 0; i < NUM_SPEC; ++i) {
                        if(X_trial[i] < -10.0*atol || !std::isfinite(X_trial[i])) physically_sound = false;
                        mass_sum += std::max(X_trial[i], burn_cfg.smallx);
                    }
                    // smallt is the configured lower boundary of the EOS burn state.
                    if(!std::isfinite(X_trial[NEQ - 1]) || X_trial[NEQ - 1] < burn_cfg.smallt) physically_sound = false;

                    // Run the more expensive EOS energy-closure check only for
                    // finite states whose normalized mathematical error passes.
                    if (physically_sound && current_err < 1.0)
                    {
                        const double inv_sum = 1.0 / mass_sum;
#pragma omp simd
                        for (int i = 0; i < NUM_SPEC; ++i) X_trial[i] = std::max(X_trial[i], burn_cfg.smallx) * inv_sum;

                        long double nuclear_mass_delta = 0.0L;
                        for (int i = 0; i < NUM_SPEC; ++i) {
                            nuclear_mass_delta += static_cast<long double>(X_trial[i] - X_ODE[i]) / NetType::AION[i] * NetType::ENERGY_WEIGHTS[i];
                        }
                        const double integrated_enuc = NetType::ENERGY_CONVERSION * static_cast<double>(nuclear_mass_delta);
                        const double old_eint = eos.get_eint_from_T(rho, X_ODE[NEQ - 1], X_ODE);
                        const double new_eint = eos.get_eint_from_T(rho, X_trial[NEQ - 1], X_trial);
                        const double thermal_delta = new_eint - old_eint;

                        const double epsilon_eint = std::max(1.0e-12 * std::abs(old_eint), 1.0e-12);
                        if (std::abs(thermal_delta) < epsilon_eint && std::abs(integrated_enuc) < epsilon_eint) {
                            step_converged = true;
                        }
                        else {
                            const double closure_scale = std::max({std::abs(integrated_enuc), std::abs(thermal_delta), rtol * std::abs(old_eint), 1.0});
                            const double closure_error = std::abs(thermal_delta - integrated_enuc) / closure_scale;

                            // Accept energy-closure errors up to five percent.
                            if (std::isfinite(closure_error) && closure_error <= 5.0e-2) {
                                step_converged = true;
                            }
                        }
                    }

                    // Hairer-Wanner order-dependent error factor for later work estimates.
                    err_fac[k] = std::pow(current_err, 1.0 / (2 * k + 1));
                    err_fac[k] = std::max(burn_cfg.odeconfig.dt_fac_min, std::min(burn_cfg.odeconfig.dt_fac_max, 1.0 / err_fac[k]));

                    if (step_converged) {
                        optimal_k = k;
                        break; // The first accepted order completes this macro step.
                    }
                    else if (k > 1 && k + 1 < MAX_K
                             && current_err > std::pow(
                                    static_cast<double>(n_seq[k + 1]) / n_seq[0], 2)) {
                        // Deuflhard's early-rejection heuristic stops raising
                        // order when the error already exceeds the improvement
                        // expected from the next substep-count ratio.
                        break;
                    }
                }
            }

            // Accept the state and select the next order and macro step.
            if (step_converged)
            {
                t_current += H;
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) X_ODE[i] = X_trial[i];

                // Select the order that minimizes estimated work per unit
                // accepted time, equivalent to maximizing step size per work.
                double work_min = 1.0e20;
                int k_next = optimal_k;

                // Compare all accepted lower orders.
                for (int k = 1; k <= optimal_k; ++k) {
                    double step_for_k = H * err_fac[k] * 0.9; // 0.9 is the extrapolation safety factor.
                    double work_k = work_cost[k] / step_for_k;
                    if (work_k < work_min) {
                        work_min = work_k;
                        k_next = k;
                    }
                }

                // Estimate whether one higher order would reduce future work.
                if (optimal_k < MAX_K - 1) {
                    double err_est = err_fac[optimal_k] * (static_cast<double>(n_seq[optimal_k + 1]) / n_seq[optimal_k]);
                    double step_higher = H * err_est * 0.9;
                    double work_higher = work_cost[optimal_k + 1] / step_higher;
                    if (work_higher < work_min) {
                        k_next = optimal_k + 1;
                    }
                }

                // Form the next macro step and apply the configured growth bounds.
                double H_new = H * err_fac[k_next] * 0.9;
                H_new = std::max(H * burn_cfg.odeconfig.dt_fac_min, std::min(H * burn_cfg.odeconfig.dt_fac_max, H_new));
                H = H_new;
            }
            else
            {
                // If every candidate order fails mathematical or physical
                // acceptance, quarter H to leave the rejected stiffness scale.
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
