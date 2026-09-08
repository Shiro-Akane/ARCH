/**
 * @file ode_be-nr.h
 * @brief Shared backward-Euler/Newton continuation and synchronous executor.
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <iostream>

#include "OdeContinuation.h"
#include "odeFunction.h"

template <typename NetType, typename MatrixType, typename LinearSolver>
struct Solver_BE_NR
{
    static constexpr int NEQ = NetType::ODE_NEQ;
    static constexpr int NUM_SPEC = NetType::NUM_SPECIES;
    static constexpr int MAX_N = NEQ;
    static constexpr bool USES_JACOBIAN_WORKSPACE = false;

    enum class Phase : unsigned char
    {
        BeginSubstep, Assemble, AwaitLinear, ApplyCorrection, FinishSubstep, Complete
    };

    /** All state crossing a linear solve is explicit, relocatable device data.
     * Matrix storage is separate: compact CPU execution uses its ordinary local
     * matrix; a sparse executor binds a CSR values view in a bounded batch pool.
     * X_ODE remains the accepted ODE state, not an uncommitted Newton trial.
     */
    struct Continuation
    {
        [[no_unique_address]] NetType network{};
        OdeMath::AcceptedState<NEQ> accepted;
        arch::math::CompensatedSum accepted_energy;
        double X_old[MAX_N], X_k[MAX_N], X_trial[MAX_N];
        double increment[MAX_N]; // Newton unknown relative to the accepted state.
        double RHS[MAX_N], RHS_old[MAX_N], b[MAX_N], W[MAX_N];
        BurnOdeReport report{};
        double rho = 0.0, dt_target = 0.0, dt_recommended = 0.0;
        double t_current = 0.0, dt = 0.0, err_prev = 1.0, current_err = 0.0;
        int newton_iter = 0;
        bool nse_attempted = false, step_converged = false, linear_success = false;
        Phase phase = Phase::Complete;
    };

    ARCH_HOST_DEVICE static void begin(
        Continuation& c, const double* X_ODE, double rho, double dt_target,
        const BurnConfigView& cfg, double dt_rec, const NetType& network = {})
    {
        // Scratch vectors are overwritten before every read; no batch-wide clear.
        c.network = network;
        c.accepted.initialize(X_ODE);
        c.accepted_energy = {};
        c.report = {};
        c.report.dt_recommended = dt_rec;
        c.rho = rho;
        c.dt_target = dt_target;
        c.dt_recommended = dt_rec;
        c.t_current = 0.0;
        c.dt = std::min(dt_target, dt_target * cfg.odeconfig.initial_dt_frac);
        c.err_prev = 1.0;
        c.current_err = 0.0;
        c.newton_iter = 0;
        c.nse_attempted = false;
        c.step_converged = false;
        c.linear_success = false;
        c.phase = X_ODE[NUM_SPEC] < cfg.nuclearTempMin || rho < cfg.nuclearDensMin
            ? Phase::Complete : Phase::BeginSubstep;
    }

    /** Consume exactly one response for an outstanding linear request. */
    ARCH_HOST_DEVICE static bool complete_linear_solve(Continuation& c, bool success)
    {
        if (c.phase != Phase::AwaitLinear) return false;
        c.linear_success = success;
        c.phase = Phase::ApplyCorrection;
        return true;
    }

    template <typename EOSType>
    ARCH_HOST_DEVICE static OdeLinearRequest advance(
        Continuation& c, MatrixType& A, double* X_ODE,
        const EOSType& eos, const BurnConfigView& cfg)
    {
        // This is the only BE-NR algorithm. Backend decisions end at AwaitLinear;
        // adaptive steps, NSE, Newton admissibility, and closure remain shared.
        for (;;) {
            switch (c.phase) {
            case Phase::Complete:
                return OdeLinearRequest::Complete;
            case Phase::AwaitLinear:
                return OdeLinearRequest::FactorizeAndSolve;
            case Phase::BeginSubstep: {
                if (!(c.t_current < c.dt_target)) {
                    c.dt_recommended = c.dt;
                    c.report.status = BurnOdeStatus::OdeSuccess;
                    c.report.dt_recommended = c.dt_recommended;
                    c.phase = Phase::Complete;
                    continue;
                }
                // Failed NSE projection preserves the state and selects the ODE.
                if constexpr (NetType::SUPPORTS_NSE) {
                    if (!c.nse_attempted && cfg.use_nse
                        && X_ODE[NUM_SPEC] > cfg.nseTempThreshold
                        && c.rho > cfg.nseDensThreshold) {
                        c.nse_attempted = true;
                        ++c.report.nse_attempts;
                        double nse_energy = 0.0;
                        if (OdeMath::integrate_nse_state<NetType, EOSType>(
                                X_ODE, c.rho, c.dt_target, eos, cfg, c.dt_recommended, &nse_energy)) {
                            OdeMath::record_accepted_energy(c.accepted_energy, c.report, nse_energy);
                            c.report.status = BurnOdeStatus::NseSuccess;
                            c.report.dt_recommended = c.dt_recommended;
                            c.phase = Phase::Complete;
                            continue;
                        }
                        ++c.report.nse_failures;
                    }
                }
                ++c.report.attempted_substeps;
                if (c.report.attempted_substeps > cfg.odeconfig.max_substeps) {
#if !defined(__CUDA_ARCH__)
                    std::cerr << "[BE-NR] Fatal Error: Exceeded max substeps ("
                              << cfg.odeconfig.max_substeps << ")" << std::endl;
#endif
                    c.report.status = BurnOdeStatus::MaxSubsteps;
                    c.phase = Phase::Complete;
                    continue;
                }
                if (c.t_current + c.dt > c.dt_target) c.dt = c.dt_target - c.t_current;
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) {
                    c.X_old[i] = X_ODE[i];
                    c.X_k[i] = X_ODE[i];
                    c.increment[i] = 0.0;
                }
                c.step_converged = false;
                c.current_err = 0.0;
                c.newton_iter = 0;
                c.phase = Phase::Assemble;
                continue;
            }
            case Phase::Assemble:
                if (c.newton_iter >= cfg.odeconfig.max_newton_iter) {
                    c.phase = Phase::FinishSubstep;
                    continue;
                }
                assemble(c, A, eos);
                c.phase = Phase::AwaitLinear;
                return OdeLinearRequest::FactorizeAndSolve;
            case Phase::ApplyCorrection:
                if (!c.linear_success || !apply_correction(c, eos, cfg) || c.step_converged) {
                    c.phase = Phase::FinishSubstep;
                } else {
                    ++c.newton_iter;
                    c.phase = Phase::Assemble;
                }
                continue;
            case Phase::FinishSubstep:
                finish_substep(c, X_ODE, cfg);
                continue;
            }
        }
    }

    template <typename EOSType>
    ARCH_HOST_DEVICE static OdeLinearRequest advance(
        Continuation& context, MatrixType&, MatrixType& system, double* state,
        const EOSType& eos, const BurnConfigView& config)
    {
        return advance(context, system, state, eos, config);
    }

    template <typename EOSType>
    static bool integrate(double* X_ODE, double rho, double dt_target, const EOSType& eos,
                          const BurnConfig& cfg, double& dt_rec, double* energy_change = nullptr)
    {
        const auto report = integrate_report(X_ODE, rho, dt_target, eos,
                                             make_burn_config_view(cfg), dt_rec,
                                             make_host_burn_network<NetType>());
        if (report.success() && energy_change) *energy_change = report.energy_change;
        return report.success();
    }

    template <typename EOSType>
    ARCH_HOST_DEVICE static BurnOdeReport integrate_report(
        double* X_ODE, double rho, double dt_target, const EOSType& eos,
        const BurnConfigView& cfg, double& dt_rec, const NetType& network = {})
    {
        Continuation context;
        MatrixType A;
        begin(context, X_ODE, rho, dt_target, cfg, dt_rec, network);
        while (advance(context, A, X_ODE, eos, cfg) != OdeLinearRequest::Complete) {
            const bool success = LinearSolver::template solve<NEQ, MAX_N>(A, context.b);
            complete_linear_solve(context, success);
        }
        dt_rec = context.dt_recommended;
        return context.report;
    }

    template <typename EOSType>
    ARCH_HOST_DEVICE static BurnOdeReport integrate_report(
        double* X_ODE, double rho, double dt_target, const EOSType& eos,
        const BurnConfigView& cfg, OdeMatrixWorkspace<MatrixType>&, double& dt_rec,
        const NetType& network = {})
    {
        // Preserve compact execution: no access to the caller's shared workspace
        // slot. Sparse execution binds its matrix view directly to advance().
        return integrate_report(X_ODE, rho, dt_target, eos, cfg, dt_rec, network);
    }

private:
    template <typename EOSType>
    ARCH_HOST_DEVICE static void assemble(
        Continuation& c, MatrixType& A, const EOSType& eos)
    {
        OdeMath::assemble_burn_jacobian<NetType>(c.X_k, c.rho, eos, A, c.RHS, c.network);
        if (c.newton_iter == 0)
            for (int i = 0; i < NEQ; ++i) c.RHS_old[i] = c.RHS[i];
        for (int i = 0; i < NEQ; ++i)
            c.b[i] = c.dt * c.RHS[i] - c.increment[i];
        A.form_shifted_identity(-c.dt);
    }

    template <typename EOSType>
    ARCH_HOST_DEVICE static bool apply_correction(
        Continuation& c, const EOSType& eos, const BurnConfigView& cfg)
    {
        const double rtol = cfg.odeconfig.rtol, atol = cfg.odeconfig.atol;
        for (int i = 0; i < NEQ; ++i)
            if (!std::isfinite(c.b[i])) return false;
        OdeMath::calc_weights<NEQ>(c.X_k, rtol, atol, c.W);
        // Solve the same backward-Euler equation for the increment. Forming
        // X_old-X_k would erase a sub-ULP thermal update before Newton sees it.
        for (int i = 0; i < NEQ; ++i) {
            c.increment[i] += c.b[i];
            c.X_trial[i] = c.accepted.incremented(i, c.increment[i]).value();
        }
        bool admissible = true;
        double mass_sum = 0.0;
        const double negative_tolerance = 10.0 * atol;
        for (int i = 0; i < NUM_SPEC; ++i) {
            if (!std::isfinite(c.X_trial[i]) || c.X_trial[i] < -negative_tolerance
                || c.X_trial[i] > 1.0 + negative_tolerance) admissible = false;
            mass_sum += c.X_trial[i];
        }
        // Temperature and any passive energy integral are evolved unknowns,
        // but only composition is projected into the unit simplex.
        for (int i = NUM_SPEC; i < NEQ; ++i) {
            if (!std::isfinite(c.X_trial[i])) admissible = false;
        }
        // 100*rtol allows roundoff-scale Newton iterates before normalization.
        if (!std::isfinite(c.X_trial[NUM_SPEC]) || c.X_trial[NUM_SPEC] < cfg.smallt
            || c.X_trial[NUM_SPEC] > 1.0e11 || !std::isfinite(mass_sum)
            || mass_sum <= 0.0 || std::abs(mass_sum - 1.0) > 100.0 * rtol)
            admissible = false;
        if (!admissible) return false;
        // Nonlinear convergence and time-discretization accuracy are separate.
        // Reserve one tenth of the ODE error scale for the Newton correction.
        // In particular, an exact linear solve is not an exact time integration.
        constexpr double newton_error_fraction = 0.1;
        const double newton_error = OdeMath::wrms_norm<NEQ>(c.b, c.W);
        if (newton_error < newton_error_fraction) {
            double projected_sum = 0.0;
            for (int i = 0; i < NUM_SPEC; ++i) {
                c.X_trial[i] = std::max(c.X_trial[i], cfg.smallx);
                projected_sum += c.X_trial[i];
            }
            const double inv_projected_sum = 1.0 / projected_sum;
            for (int i = 0; i < NUM_SPEC; ++i) c.X_trial[i] *= inv_projected_sum;
            OdeMath::eval_burn_rhs<NetType>(c.X_trial, c.rho, eos, c.RHS, c.network);
            // Backward Euler minus the trapezoidal update, evaluated with the
            // same endpoint states, estimates the leading O(dt^2) local error.
            // No second solve or copied reaction/EOS implementation is needed.
            for (int i = 0; i < NEQ; ++i)
                c.b[i] = 0.5 * c.dt * (c.RHS[i] - c.RHS_old[i]);
            c.current_err = OdeMath::wrms_norm<NEQ>(c.b, c.W);
            if (!std::isfinite(c.current_err) || c.current_err >= 1.0) return false;
            const double integrated_enuc = OdeMath::integrated_burn_increment_energy<NetType>(c.increment);
            const double old_eint = eos.get_eint_from_T(c.rho, c.X_old[NUM_SPEC], c.X_old);
            const double new_eint = eos.get_eint_from_T(c.rho, c.X_trial[NUM_SPEC], c.X_trial);
            const double thermal_delta = new_eint - old_eint;
            const double closure_scale = OdeMath::max4(
                std::abs(integrated_enuc), std::abs(thermal_delta), rtol * std::abs(old_eint), 1.0);
            const double closure_error = std::abs(thermal_delta - integrated_enuc) / closure_scale;
            if (!std::isfinite(closure_error) || closure_error > 5.0e-2) return false;
            c.step_converged = true;
        }
        for (int i = 0; i < NEQ; ++i) c.X_k[i] = c.X_trial[i];
        return true;
    }

    ARCH_HOST_DEVICE static void finish_substep(
        Continuation& c, double* X_ODE, const BurnConfigView& cfg)
    {
        if (c.step_converged) {
            OdeMath::record_accepted_energy(c.accepted_energy, c.report,
                OdeMath::integrated_burn_increment_energy<NetType>(c.increment));
            c.t_current += c.dt;
#pragma omp simd
            for (int i = 0; i < NEQ; ++i)
                X_ODE[i] = c.accepted.commit(i,
                    c.accepted.incremented(i, c.increment[i]), c.X_k[i]);
            double dt_new = OdeMath::pi_controller(
                c.current_err, c.err_prev, c.dt, 2, cfg.odeconfig.dt_safe_factor,
                cfg.odeconfig.dt_fac_min, cfg.odeconfig.dt_fac_max);
            dt_new = std::max(c.dt * cfg.odeconfig.dt_fac_min,
                             std::min(c.dt * cfg.odeconfig.dt_fac_max, dt_new));
            c.dt = dt_new * cfg.odeconfig.dt_safe_factor;
            c.err_prev = c.current_err;
        } else {
            c.dt *= 0.25;
            ++c.report.rejected_substeps;
            if (c.dt < 1e-22) {
#if !defined(__CUDA_ARCH__)
                std::cerr << "[BE-NR] Fatal Error: Stiff ODE stalled. dt < 1e-22" << std::endl;
#endif
                c.report.status = BurnOdeStatus::Stalled;
                c.phase = Phase::Complete;
                return;
            }
        }
        c.phase = Phase::BeginSubstep;
    }
};
