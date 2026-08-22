/**
 * @file ode_be-nr.h
 * @brief Unified concept for ODE Integrators using Backward Euler + Newton-Raphson with Adaptive Sub-stepping.
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <iostream>

#include "Networks.h"
#include "odeFunction.h"


template <typename NetType, typename MatrixType, typename LinearSolver>
struct Solver_BE_NR
{
    static constexpr int NEQ = NetType::ODE_NEQ;
    static constexpr int NUM_SPEC = NetType::NUM_SPECIES; // Excludes the final temperature equation.
    static constexpr int MAX_N = NEQ;

    template <typename EOSType>
    static bool integrate(double *X_ODE, double rho, double dt_target, const EOSType &eos,
                          const BurnConfig &burn_cfg, double &dt_rec)
    {
        return integrate_report(X_ODE, rho, dt_target, eos,
                                make_burn_config_view(burn_cfg), dt_rec).success();
    }

    template <typename EOSType>
    ARCH_HOST_DEVICE static BurnOdeReport integrate_report(
        double *X_ODE, double rho, double dt_target, const EOSType &eos,
        const BurnConfigView &burn_cfg, double &dt_rec)
    {
        BurnOdeReport report{};
        report.dt_recommended = dt_rec;
        if (X_ODE[NEQ - 1] < burn_cfg.nuclearTempMin || rho < burn_cfg.nuclearDensMin)
        {
            return report;
        }

        double X_old[MAX_N], X_k[MAX_N], X_trial[MAX_N];
        double RHS[MAX_N], b[MAX_N], W[MAX_N];
        MatrixType A;

        // Runtime tolerances and iteration limits.
        const double rtol = burn_cfg.odeconfig.rtol;
        const double atol = burn_cfg.odeconfig.atol;
        const int max_newton_iter = burn_cfg.odeconfig.max_newton_iter;
        const int max_substeps = burn_cfg.odeconfig.max_substeps;

        double t_current = 0.0;
        double dt = std::min(dt_target, dt_target * burn_cfg.odeconfig.initial_dt_frac); // Conservative initial trial substep.
        double err_prev = 1.0;
        int substep_count = 0; // Counts internal attempts against max_substeps.
        bool nse_attempted = false;

        // Cover dt_target with adaptive implicit-Euler substeps.
        while (t_current < dt_target)
        {
            // Network-constrained Timmes NSE projection coupled to the EOS.
            // Binding-energy changes update temperature conservatively. A
            // failed projection preserves X_ODE and selects the stiff ODE path.
            if constexpr (NetType::SUPPORTS_NSE) {
            if (!nse_attempted && burn_cfg.use_nse
                && X_ODE[NEQ - 1] > burn_cfg.nseTempThreshold
                && rho > burn_cfg.nseDensThreshold)
            {
                nse_attempted = true;
                ++report.nse_attempts;
                if (OdeMath::integrate_nse_state<NetType, EOSType>(X_ODE, rho, dt_target, eos,
                                        burn_cfg, dt_rec)) {
                    report.status = BurnOdeStatus::NseSuccess;
                    report.dt_recommended = dt_rec;
                    return report;
                }
                ++report.nse_failures;
            }
            }

            substep_count++;
            report.attempted_substeps = substep_count;
            if (substep_count > max_substeps)
            {
#if !defined(__CUDA_ARCH__)
                std::cerr << "[BE-NR] Fatal Error: Exceeded max substeps (" << max_substeps << ")" << std::endl;
#endif
                report.status = BurnOdeStatus::MaxSubsteps;
                return report;
            }

            // Shorten the final substep to land exactly on dt_target.
            if (t_current + dt > dt_target)
            {
                dt = dt_target - t_current;
            }

            // Preserve the accepted state at the start of this trial substep.
#pragma omp simd
            for (int i = 0; i < NEQ; ++i)
            {
                X_old[i] = X_ODE[i];
                X_k[i] = X_ODE[i];
            }

            bool step_converged = false;
            double current_err = 0.0;

            // Newton iteration for the backward-Euler residual.
            for (int iter = 0; iter < max_newton_iter; ++iter)
            {
                double enuc = 0.0;
                A.zero();

                double T_current = X_k[NEQ - 1];
                double eta = eos.get_eta(rho, T_current, X_k);

                // Evaluate the network RHS and its analytic Jacobian data.
                NetType::eval_rhs(X_k, rho, eta, RHS, enuc);

                // Timmes network derivatives: composition block, nuclear-energy
                // derivatives, and the full analytic temperature column.
                double denuc_dX[MAX_N]{};
                double dRHS_dT[MAX_N]{};
                double denuc_dT = 0.0;
                NetType::eval_jacobian(X_k, rho, eta, A, denuc_dX);
                NetType::eval_temperature_derivative(X_k, rho, eta, dRHS_dT, denuc_dT);

                // Match the original Timmes self-heating Jacobian exactly:
                // dT/dt = enuc/cv and J_T,* = J_enuc,*/cv.  Timmes obtains cv
                // analytically from Helmholtz but does not differentiate cv in
                // the ODE Jacobian. Temperature is not perturbed by this column.
                const double cv = OdeMath::burn_cv_floor(
                    eos.get_cv(rho, T_current, X_k));
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

                // Assemble A=I-dt*J and b=X_old-X_k+dt*RHS(X_k).
                for (int i = 0; i < NEQ; ++i) {
                    b[i] = X_old[i] - X_k[i] + dt * RHS[i];
                }
                A.form_shifted_identity(-dt);

                // Solve the Newton correction A*dX=b.
                bool success = LinearSolver::template solve<NEQ, MAX_N>(A, b);
                if (!success)
                {
                    break; // A singular matrix rejects the substep; the outer loop reduces dt.
                }

                // Reject non-finite Newton updates and retry with a smaller step.
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

                // Weight the Newton correction b=dX at the current iterate.
                OdeMath::calc_weights<NEQ>(X_k, rtol, atol, W);

                // Apply Y_{k+1}=X_k+b and check physical admissibility.
                bool admissible = true;
                double mass_sum = 0.0;
                const double negative_tolerance = 10.0 * atol;
                for (int i = 0; i < NUM_SPEC; ++i) {
                    X_trial[i] = X_k[i] + b[i];
                    if (!std::isfinite(X_trial[i])
                        || X_trial[i] < -negative_tolerance
                        || X_trial[i] > 1.0 + negative_tolerance) {
                        admissible = false;
                    }
                    mass_sum += X_trial[i];
                }
                X_trial[NEQ - 1] = X_k[NEQ - 1] + b[NEQ - 1];
                // Bound temperature to the supported burn/EOS range. The
                // 100*rtol composition allowance avoids rejecting roundoff-
                // scale Newton iterates before final normalization.
                if (!std::isfinite(X_trial[NEQ - 1])
                    || X_trial[NEQ - 1] < burn_cfg.smallt
                    || X_trial[NEQ - 1] > 1.0e11
                    || !std::isfinite(mass_sum) || mass_sum <= 0.0
                    || std::abs(mass_sum - 1.0) > 100.0 * rtol) {
                    admissible = false;
                }
                if (!admissible) break;

                // Evaluate the weighted Newton update over all ODE components.
                current_err = OdeMath::wrms_norm<NEQ>(b, W);

                // WRMS values below one satisfy the configured error scale.
                if (current_err < 1.0)
                {
                    double projected_sum = 0.0;
                    for (int i = 0; i < NUM_SPEC; ++i) {
                        X_trial[i] = std::max(X_trial[i], burn_cfg.smallx);
                        projected_sum += X_trial[i];
                    }
                    const double inv_projected_sum = 1.0 / projected_sum;
                    for (int i = 0; i < NUM_SPEC; ++i) {
                        X_trial[i] *= inv_projected_sum;
                    }

                    long double nuclear_mass_delta = 0.0L;
                    for (int i = 0; i < NUM_SPEC; ++i) {
                        nuclear_mass_delta +=
                            static_cast<long double>(X_trial[i] - X_old[i])
                            / NetType::aion(i) * NetType::energy_weight(i);
                    }
                    const double integrated_enuc = NetType::ENERGY_CONVERSION
                        * static_cast<double>(nuclear_mass_delta);
                    const double old_eint = eos.get_eint_from_T(
                        rho, X_old[NEQ - 1], X_old);
                    const double new_eint = eos.get_eint_from_T(
                        rho, X_trial[NEQ - 1], X_trial);
                    const double thermal_delta = new_eint - old_eint;
                    const double closure_scale = OdeMath::max4(
                        std::abs(integrated_enuc), std::abs(thermal_delta),
                        rtol * std::abs(old_eint), 1.0);
                    const double closure_error =
                        std::abs(thermal_delta - integrated_enuc) / closure_scale;
                    // Reject thermal/nuclear energy disagreement above five percent.
                    if (!std::isfinite(closure_error) || closure_error > 5.0e-2) {
                        break;
                    }

                    for (int i = 0; i < NEQ; ++i) X_k[i] = X_trial[i];
                    step_converged = true;
                    break;
                }

                for (int i = 0; i < NEQ; ++i) X_k[i] = X_trial[i];
            }

            // Accept the converged state or reject and reduce the substep.
            if (step_converged)
            {
                // Commit a converged state and advance internal time.
                t_current += dt;
#pragma omp simd
                for (int i = 0; i < NEQ; ++i)
                {
                    X_ODE[i] = X_k[i];
                }

                // Obtain the unconstrained next substep from the PI controller.
                double dt_new = OdeMath::pi_controller(current_err, err_prev, dt,
                                                       1,
                                                       burn_cfg.odeconfig.dt_safe_factor,
                                                       burn_cfg.odeconfig.dt_fac_min,
                                                       burn_cfg.odeconfig.dt_fac_max);

                // Limit growth and shrinkage to avoid large step-size oscillations.
                dt_new = std::max(dt * burn_cfg.odeconfig.dt_fac_min,
                                  std::min(dt * burn_cfg.odeconfig.dt_fac_max, dt_new));

                // Apply the configured safety factor after the growth bounds.
                dt = dt_new * burn_cfg.odeconfig.dt_safe_factor;
                err_prev = current_err;
            }
            else
            {
                // A divergent or singular Newton solve rejects the state and
                // quarters dt before retrying the same internal interval.
                dt *= 0.25;

                // 1e-22 s is the hard stall guard; smaller substeps would not
                // make meaningful time progress in double precision.
                if (dt < 1e-22)
                {
#if !defined(__CUDA_ARCH__)
                    std::cerr << "[BE-NR] Fatal Error: Stiff ODE stalled. dt < 1e-22" << std::endl;
#endif
                    report.status = BurnOdeStatus::Stalled;
                    report.rejected_substeps += 1;
                    return report;
                }
                report.rejected_substeps += 1;
            }
        }

        dt_rec = dt; // Return the PI controller's final stable substep recommendation.
        report.status = BurnOdeStatus::OdeSuccess;
        report.dt_recommended = dt_rec;
        return report; // The solver covered the complete requested interval.
    }

    template <typename EOSType>
    ARCH_HOST_DEVICE static BurnOdeReport integrate_report(
        double *X_ODE, double rho, double dt_target, const EOSType &eos,
        const BurnConfigView &burn_cfg,
        OdeMatrixWorkspace<MatrixType>&, double &dt_rec)
    {
        return integrate_report(
            X_ODE, rho, dt_target, eos, burn_cfg, dt_rec);
    }
};
