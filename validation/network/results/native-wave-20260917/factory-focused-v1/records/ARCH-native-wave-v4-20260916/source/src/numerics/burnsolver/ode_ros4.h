/**
 * @file ode_ros4.h
 * @brief Shared four-stage ROS4 continuation and synchronous linear executor.
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <iostream>

#include "OdeContinuation.h"
#include "odeFunction.h"

template <typename NetType, typename MatrixType, typename LinearSolver>
struct Solver_ROS4
{
    static constexpr int NEQ = NetType::ODE_NEQ;
    static constexpr int NUM_SPEC = NetType::NUM_SPECIES;
    static constexpr int MAX_N = NEQ;
    static constexpr bool USES_JACOBIAN_WORKSPACE = true;

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

    enum class Phase : unsigned char
    {
        BeginSubstep, AwaitFactor, FactorReady, AwaitSolve, SolveReady, Complete
    };
    struct Continuation
    {
        [[no_unique_address]] NetType network{};
        OdeMath::AcceptedState<NEQ> accepted;
        arch::math::CompensatedSum accepted_energy;
        double X_old[MAX_N], X_k[MAX_N], X_trial[MAX_N];
        double RHS[MAX_N], b[MAX_N], W[MAX_N], X_err[MAX_N];
        double u1[MAX_N], u2[MAX_N], u3[MAX_N], u4[MAX_N];
        BurnOdeReport report{};
        double rho = 0.0, dt_target = 0.0, dt_recommended = 0.0;
        double t_current = 0.0, dt = 0.0, err_prev = 1.0;
        int stage = 0;
        bool nse_attempted = false, linear_success = false, solve_failed = false;
        Phase phase = Phase::Complete;
    };

    ARCH_HOST_DEVICE static void begin(
        Continuation& c, const double* X_ODE, double rho, double dt_target,
        const BurnConfigView& cfg, double dt_rec, const NetType& network = {})
    {
        c.network = network;
        c.accepted.initialize(X_ODE);
        c.accepted_energy = {};
        c.report = {};
        c.report.dt_recommended = dt_rec;
        c.rho = rho; c.dt_target = dt_target; c.dt_recommended = dt_rec;
        c.t_current = 0.0;
        c.dt = std::min(dt_target, dt_target * cfg.odeconfig.initial_dt_frac);
        c.err_prev = 1.0;
        c.stage = 0;
        c.nse_attempted = false; c.linear_success = false; c.solve_failed = false;
        c.phase = X_ODE[NUM_SPEC] < cfg.nuclearTempMin || rho < cfg.nuclearDensMin
            ? Phase::Complete : Phase::BeginSubstep;
    }

    ARCH_HOST_DEVICE static bool complete_linear_solve(Continuation& c, bool success)
    {
        if (c.phase == Phase::AwaitFactor) c.phase = Phase::FactorReady;
        else if (c.phase == Phase::AwaitSolve) c.phase = Phase::SolveReady;
        else return false;
        c.linear_success = success;
        return true;
    }

    template <typename EOSType>
    ARCH_HOST_DEVICE static OdeLinearRequest advance(
        Continuation& c, MatrixType& J_mat, MatrixType& A, double* X_ODE,
        const EOSType& eos, const BurnConfigView& burn_cfg)
    {
        // Aliases refer to continuation-owned values that remain valid across
        // a suspended linear solve; they do not introduce separate stage state.
        auto& report = c.report;
        auto& dt = c.dt;
        auto& dt_rec = c.dt_recommended;
        auto& t_current = c.t_current;
        auto& nse_attempted = c.nse_attempted;
        auto& X_old = c.X_old;
        const double rho = c.rho, dt_target = c.dt_target;
        const int max_substeps = burn_cfg.odeconfig.max_substeps;
        for (;;) {
            switch (c.phase) {
            case Phase::Complete: return OdeLinearRequest::Complete;
            case Phase::AwaitFactor: return OdeLinearRequest::Factorize;
            case Phase::AwaitSolve: return OdeLinearRequest::SolveWithFactors;
            case Phase::BeginSubstep: {
                if (!(t_current < dt_target)) {
                    dt_rec = dt;
                    report.status = BurnOdeStatus::OdeSuccess;
                    report.dt_recommended = dt_rec;
                    c.phase = Phase::Complete;
                    continue;
                }
                if constexpr (NetType::SUPPORTS_NSE) {
                    if (!nse_attempted && burn_cfg.use_nse
                        && X_ODE[NUM_SPEC] > burn_cfg.nseTempThreshold
                        && rho > burn_cfg.nseDensThreshold)
                    {
                        nse_attempted = true;
                        ++report.nse_attempts;
                        double nse_energy = 0.0;
                        if (OdeMath::integrate_nse_state<NetType, EOSType>(X_ODE, rho, dt_target, eos,
                                                                        burn_cfg, dt_rec, &nse_energy)) {
                            OdeMath::record_accepted_energy(c.accepted_energy, report, nse_energy);
                            report.status = BurnOdeStatus::NseSuccess;
                            report.dt_recommended = dt_rec;
                            c.phase = Phase::Complete;
                            return OdeLinearRequest::Complete;
                        }
                        ++report.nse_failures;
                    }
                }

                ++report.attempted_substeps;
                const int substep_count = report.attempted_substeps;
                if (substep_count > max_substeps)
                {
#if !defined(__CUDA_ARCH__)
                    std::cerr << "[ROS4] Fatal Error: Exceeded max substeps (" << max_substeps << ")" << std::endl;
#endif
                    report.status = BurnOdeStatus::MaxSubsteps;
                    c.phase = Phase::Complete;
                    return OdeLinearRequest::Complete;
                }

                if (t_current + dt > dt_target) dt = dt_target - t_current;

#pragma omp simd
                for (int i = 0; i < NEQ; ++i) X_old[i] = X_ODE[i];

                assemble(c, J_mat, A, eos);
                c.solve_failed = false;
                c.phase = Phase::AwaitFactor;
                return OdeLinearRequest::Factorize;
            }
            case Phase::FactorReady:
                if (!c.linear_success) {
                    // A failed factorization rejects the same trial as a
                    // failed stage solve: share its rollback, stall check and
                    // NSE retry reset. No uncomputed stage may be consumed.
                    c.solve_failed = true;
                    finish_trial(c, X_ODE, eos, burn_cfg);
                    continue;
                }
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) c.b[i] = gamma * c.dt * c.RHS[i];
                c.stage = 0;
                c.phase = Phase::AwaitSolve;
                return OdeLinearRequest::SolveWithFactors;
            case Phase::SolveReady: {
                double* stage_values = c.stage == 0 ? c.u1 : c.stage == 1 ? c.u2
                    : c.stage == 2 ? c.u3 : c.u4;
                c.solve_failed = !c.linear_success;
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) {
                    stage_values[i] = c.b[i];
                    if (!std::isfinite(stage_values[i])) c.solve_failed = true;
                }
                if (c.solve_failed || c.stage == 3) {
                    finish_trial(c, X_ODE, eos, burn_cfg);
                    continue;
                }
                // Construct the next stage state from the matched ROS4 tableau.
                if (c.stage == 0) {
#pragma omp simd
                    for (int i = 0; i < NEQ; ++i) c.X_k[i] = stage_state_sum(c, i, a21).value();
                } else if (c.stage == 1) {
#pragma omp simd
                    for (int i = 0; i < NEQ; ++i) c.X_k[i] = stage_state_sum(c, i, a31, a32).value();
                } else {
#pragma omp simd
                    for (int i = 0; i < NEQ; ++i) c.X_k[i] = stage_state_sum(c, i, a41, a42, a43).value();
                }
                sanitize_state(c.X_k, burn_cfg);
                OdeMath::eval_burn_rhs<NetType>(c.X_k, c.rho, eos, c.RHS, c.network);
                if (c.stage == 0) {
#pragma omp simd
                    for (int i = 0; i < NEQ; ++i) c.b[i] = gamma * (c.dt * c.RHS[i] + c21 * c.u1[i]);
                } else if (c.stage == 1) {
#pragma omp simd
                    for (int i = 0; i < NEQ; ++i) c.b[i] = gamma * (c.dt * c.RHS[i] + c31 * c.u1[i] + c32 * c.u2[i]);
                } else {
#pragma omp simd
                    for (int i = 0; i < NEQ; ++i) c.b[i] = gamma * (c.dt * c.RHS[i] + c41 * c.u1[i] + c42 * c.u2[i] + c43 * c.u3[i]);
                }
                ++c.stage;
                c.phase = Phase::AwaitSolve;
                return OdeLinearRequest::SolveWithFactors;
            }
            }
        }
    }

    template <typename EOSType>
    static bool integrate(double* X_ODE, double rho, double dt_target, const EOSType& eos,
                          const BurnConfig& cfg, double& dt_rec, double* energy_change = nullptr)
    {
        const auto report = integrate_report(X_ODE, rho, dt_target, eos, make_burn_config_view(cfg),
                                             dt_rec, make_host_burn_network<NetType>());
        if (report.success() && energy_change) *energy_change = report.energy_change;
        return report.success();
    }
    template <typename EOSType>
    ARCH_HOST_DEVICE static BurnOdeReport integrate_report(
        double* X_ODE, double rho, double dt_target, const EOSType& eos,
        const BurnConfigView& cfg, double& dt_rec, const NetType& network = {})
    {
        MatrixType jacobian, system;
        return integrate_report_with_matrices(X_ODE, rho, dt_target, eos, cfg, jacobian, system, dt_rec, network);
    }
    template <typename EOSType>
    ARCH_HOST_DEVICE static BurnOdeReport integrate_report(
        double* X_ODE, double rho, double dt_target, const EOSType& eos,
        const BurnConfigView& cfg, OdeMatrixWorkspace<MatrixType>& workspace, double& dt_rec,
        const NetType& network = {})
    {
        return integrate_report_with_matrices(X_ODE, rho, dt_target, eos, cfg,
                                              workspace.jacobian, workspace.system, dt_rec, network);
    }

private:
    // One affine stage-state authority. Preserve small thermal/source changes
    // on a large background using the shared compensated arithmetic. The
    // caller supplies weights from the ROS4 tableau. Zero coefficients must
    // not read stages that have not yet been computed.
    ARCH_HOST_DEVICE static void add_weighted_stages(
        arch::math::CompensatedSum& sum, const Continuation& c, int i,
        double w1, double w2, double w3, double w4)
    {
        sum.add_product(w1, c.u1[i]);
        if (w2 != 0.0) sum.add_product(w2, c.u2[i]);
        if (w3 != 0.0) sum.add_product(w3, c.u3[i]);
        if (w4 != 0.0) sum.add_product(w4, c.u4[i]);
    }

    ARCH_HOST_DEVICE static arch::math::CompensatedSum stage_state_sum(
        const Continuation& c, int i, double w1, double w2 = 0.0,
        double w3 = 0.0, double w4 = 0.0)
    {
        auto sum = c.accepted.sum(i);
        add_weighted_stages(sum, c, i, w1, w2, w3, w4);
        return sum;
    }

    template <typename EOSType>
    ARCH_HOST_DEVICE static BurnOdeReport integrate_report_with_matrices(
        double* X_ODE, double rho, double dt_target, const EOSType& eos,
        const BurnConfigView& cfg, MatrixType& jacobian, MatrixType& system, double& dt_rec,
        const NetType& network)
    {
        Continuation context;
        int pivots[MAX_N];
        begin(context, X_ODE, rho, dt_target, cfg, dt_rec, network);
        for (;;) {
            const auto request = advance(context, jacobian, system, X_ODE, eos, cfg);
            if (request == OdeLinearRequest::Complete) break;
            bool success = true;
            if (request == OdeLinearRequest::Factorize)
                success = LinearSolver::template factorize<NEQ, MAX_N>(system, pivots);
            else
                LinearSolver::template solve_with_factors<NEQ, MAX_N>(system, pivots, context.b);
            complete_linear_solve(context, success);
        }
        dt_rec = context.dt_recommended;
        return context.report;
    }

    ARCH_HOST_DEVICE static void sanitize_state(double* Y_state, const BurnConfigView& burn_cfg)
    {
#pragma omp simd
        for (int i = 0; i < NUM_SPEC; ++i) {
            if (Y_state[i] < burn_cfg.smallx) Y_state[i] = burn_cfg.smallx;
            else if (Y_state[i] > 1.0) Y_state[i] = 1.0;
        }
        if (Y_state[NUM_SPEC] < burn_cfg.nuclearTempMin) Y_state[NUM_SPEC] = burn_cfg.nuclearTempMin;
    }

    template <typename EOSType>
    ARCH_HOST_DEVICE static void assemble(
        Continuation& c, MatrixType& J_mat, MatrixType& A, const EOSType& eos)
    {
        OdeMath::assemble_burn_jacobian<NetType>(c.X_old, c.rho, eos, J_mat, c.RHS, c.network);
        // The normalized form of (I / (gamma*dt) - J) uses
        // A = I - gamma*dt*J and scales every stage right-hand side by gamma.
        A.set_shifted_identity_from(J_mat, -gamma * c.dt);
    }

    template <typename EOSType>
    ARCH_HOST_DEVICE static void finish_trial(
        Continuation& c, double* X_ODE, const EOSType& eos, const BurnConfigView& burn_cfg)
    {
        auto& X_old = c.X_old; auto& X_trial = c.X_trial;
        auto& X_err = c.X_err; auto& W = c.W;
        auto& u1 = c.u1; auto& u2 = c.u2; auto& u3 = c.u3; auto& u4 = c.u4;
        auto& dt = c.dt; auto& t_current = c.t_current; auto& err_prev = c.err_prev;
        auto& nse_attempted = c.nse_attempted; auto& report = c.report;
        const bool solve_failed = c.solve_failed;
        const double rho = c.rho, rtol = burn_cfg.odeconfig.rtol, atol = burn_cfg.odeconfig.atol;
        bool step_converged = false;
        double current_err = 0.0;
        double trial_energy = 0.0;
            // Assemble the trial state and enforce physical admissibility.
            if (!solve_failed)
            {
                bool admissible = true;
                double mass_sum = 0.0;

#pragma omp simd
                for (int i = 0; i < NUM_SPEC; ++i) {
                    X_trial[i] = stage_state_sum(c, i, m1, m2, m3, m4).value();
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

                // Apply the same stages and error weights to the passive
                // integral as to temperature; never clamp it as an abundance.
                for (int i = NUM_SPEC; i < NEQ; ++i) {
                    X_trial[i] = stage_state_sum(c, i, m1, m2, m3, m4).value();
                    X_err[i] = e1 * u1[i] + e2 * u2[i] + e3 * u3[i] + e4 * u4[i];
                    if (!std::isfinite(X_trial[i])) admissible = false;
                }

                // The 1e11 K upper guard bounds the Timmes EOS/network domain
                // used by the burn solvers; smallt supplies the lower guard.
                if (!std::isfinite(X_trial[NUM_SPEC]) || X_trial[NUM_SPEC] < burn_cfg.smallt || X_trial[NUM_SPEC] > 1.0e11
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

                        // Error scratch is no longer live after its norm. Use
                        // the actual stage increment for closure and handoff;
                        // endpoint subtraction can erase a sub-ULP transfer.
                        for (int i = 0; i < NEQ; ++i) {
                            arch::math::CompensatedSum increment;
                            add_weighted_stages(increment, c, i, m1, m2, m3, m4);
                            X_err[i] = increment.value();
                        }
                        trial_energy = OdeMath::integrated_burn_increment_energy<NetType>(X_err);
                        const double integrated_enuc = trial_energy;
                        const double old_eint = eos.get_eint_from_T(rho, X_old[NUM_SPEC], X_old);
                        const double new_eint = eos.get_eint_from_T(rho, X_trial[NUM_SPEC], X_trial);
                        const double thermal_delta = new_eint - old_eint;

                        // Resolve changes below the internal-energy comparison scale.
                        const double epsilon_eint = std::max(1.0e-12 * std::abs(old_eint), 1.0e-12);

                        if (std::abs(thermal_delta) < epsilon_eint && std::abs(integrated_enuc) < epsilon_eint) {
                            step_converged = true;
                        }
                        else {
                            const double closure_scale = OdeMath::max4(
                                std::abs(integrated_enuc), std::abs(thermal_delta),
                                rtol * std::abs(old_eint), 1.0);
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
                OdeMath::record_accepted_energy(c.accepted_energy, report, trial_energy);
                t_current += dt;
#pragma omp simd
                for (int i = 0; i < NEQ; ++i)
                    X_ODE[i] = c.accepted.commit(i, stage_state_sum(c, i, m1, m2, m3, m4), X_trial[i]);

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
                report.rejected_substeps += 1;
                // Below 1e-22 s, double-precision time accumulation no longer
                // provides useful progress for the supported burn cases.
                if (dt < 1e-22)
                {
#if !defined(__CUDA_ARCH__)
                    std::cerr << "[ROS4] Fatal Error: Stiff ODE stalled. dt < 1e-22" << std::endl;
#endif
                    report.status = BurnOdeStatus::Stalled;
                    c.phase = Phase::Complete;
                    return;
                }
            }
        c.phase = Phase::BeginSubstep;
    }
};
