/**
 * @file ode_bd.h
 * @brief Shared Bader-Deuflhard continuation and synchronous linear executor.
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <iostream>

#include "OdeContinuation.h"
#include "odeFunction.h"

template <typename NetType, typename MatrixType, typename LinearSolver>
struct Solver_BD
{
    static constexpr int NEQ = NetType::ODE_NEQ;
    static constexpr int NUM_SPEC = NetType::NUM_SPECIES;
    static constexpr int MAX_N = NEQ;
    static constexpr bool USES_JACOBIAN_WORKSPACE = true;

    // Seven extrapolation levels limit high-order polynomial oscillation while
    // covering the useful compact-network accuracy range.
    static constexpr int MAX_K = 7;
    // Deuflhard harmonic sequence using the common Roman-sequence variant.
    ARCH_INLINE static constexpr int sequence_value(int level)
    {
        switch (level) {
        case 0: return 2;
        case 1: return 6;
        case 2: return 10;
        case 3: return 14;
        case 4: return 22;
        case 5: return 34;
        default: return 50;
        }
    }
    // Relative work estimates count RHS evaluations and linear solves. One
    // Jacobian is shared by all midpoint substeps at a given extrapolation level.
    ARCH_INLINE static constexpr double work_cost_value(int level)
    {
        switch (level) {
        case 0: return 2.0;
        case 1: return 8.0;
        case 2: return 18.0;
        case 3: return 32.0;
        case 4: return 54.0;
        case 5: return 88.0;
        default: return 138.0;
        }
    }

    enum class Phase : unsigned char
    {
        BeginMacro, BeginLevel, AwaitFactor, FactorReady, AwaitSolve, SolveReady,
        AssessLevel, FinishMacro, Complete
    };
    struct Continuation
    {
        [[no_unique_address]] NetType network{};
        OdeMath::AcceptedState<NEQ> accepted;
        arch::math::CompensatedSum accepted_energy;
        // GPU execution places this O(NEQ) tableau in a bounded global pool,
        // never an unbounded per-cell dense-matrix or device-stack allocation.
        // Extrapolate changes from the macro-step state, not large absolute
        // temperatures/energy integrals whose subtraction erases small heating.
        double T_extrap[MAX_K][MAX_K][NEQ], err_fac[MAX_K];
        double W[MAX_N], X_err[MAX_N], X_trial[MAX_N];
        double RHS[MAX_N], b[MAX_N], delta[MAX_N], increment[MAX_N], X_j[MAX_N];
        int n_seq[MAX_K];
        BurnOdeReport report{};
        double rho = 0.0, dt_target = 0.0, dt_recommended = 0.0;
        double t_current = 0.0, H = 0.0, h = 0.0, current_err = 0.0;
        int k = 0, j = 0, m = 0, stage = 0, optimal_k = 0;
        bool nse_attempted = false, linear_success = false, step_converged = false;
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
        c.H = std::min(dt_target, dt_target * cfg.odeconfig.initial_dt_frac);
        c.h = 0.0; c.current_err = 0.0;
        c.k = c.j = c.m = c.stage = c.optimal_k = 0;
        c.nse_attempted = false; c.linear_success = false; c.step_converged = false;
        for (int level = 0; level < MAX_K; ++level) c.n_seq[level] = sequence_value(level);
        c.phase = X_ODE[NUM_SPEC] < cfg.nuclearTempMin || rho < cfg.nuclearDensMin
            ? Phase::Complete : Phase::BeginMacro;
    }

    ARCH_HOST_DEVICE static bool complete_linear_solve(Continuation& c, bool success)
    {
        if (c.phase == Phase::AwaitFactor) c.phase = Phase::FactorReady;
        else if (c.phase == Phase::AwaitSolve) c.phase = Phase::SolveReady;
        else return false;
        c.linear_success = success;
        return true;
    }

    /** Select a next step using only error factors actually computed this step.
     * The higher-order candidate has an ESTIMATED factor, not err_fac[k+1].
     * The current work table does not select a higher order for positive finite
     * inputs, but this must remain correct if that table is refined in future.
     */
    ARCH_HOST_DEVICE static double recommend_macro_step(
        double H, const double* err_fac, int optimal_k, const BurnConfigView& cfg)
    {
        int sequence[MAX_K];
        double work[MAX_K];
        for (int order = 0; order < MAX_K; ++order) {
            sequence[order] = sequence_value(order);
            work[order] = work_cost_value(order);
        }
        return OdeMath::bd_recommend_macro_step<MAX_K>(H, err_fac, optimal_k,
            sequence, work, cfg.odeconfig.dt_fac_min, cfg.odeconfig.dt_fac_max);
    }

    template <typename EOSType>
    ARCH_HOST_DEVICE static OdeLinearRequest advance(
        Continuation& c, MatrixType& J_mat, MatrixType& A, double* X_ODE,
        const EOSType& eos, const BurnConfigView& burn_cfg)
    {
        auto& report = c.report;
        auto& dt_rec = c.dt_recommended; auto& t_current = c.t_current; auto& H = c.H;
        auto& nse_attempted = c.nse_attempted;
        const double rho = c.rho, dt_target = c.dt_target;
        const int max_substeps = burn_cfg.odeconfig.max_substeps;
        for (;;) {
            switch (c.phase) {
            case Phase::Complete: return OdeLinearRequest::Complete;
            case Phase::AwaitFactor: return OdeLinearRequest::Factorize;
            case Phase::AwaitSolve: return OdeLinearRequest::SolveWithFactors;
            case Phase::BeginMacro: {
                if (!(t_current < dt_target)) {
                    dt_rec = H;
                    report.status = BurnOdeStatus::OdeSuccess;
                    report.dt_recommended = dt_rec;
                    c.phase = Phase::Complete;
                    continue;
                }
                if constexpr (NetType::SUPPORTS_NSE) {
                    if (!nse_attempted && burn_cfg.use_nse && X_ODE[NUM_SPEC] > burn_cfg.nseTempThreshold && rho > burn_cfg.nseDensThreshold) {
                        nse_attempted = true;
                        ++report.nse_attempts;
                        double nse_energy = 0.0;
                        if (OdeMath::integrate_nse_state<NetType, EOSType>(X_ODE, rho, dt_target, eos, burn_cfg, dt_rec, &nse_energy)) {
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
                if (substep_count > max_substeps) {
#if !defined(__CUDA_ARCH__)
                    std::cerr << "[BD] Fatal Error: Exceeded max substeps." << std::endl;
#endif
                    report.status = BurnOdeStatus::MaxSubsteps;
                    c.phase = Phase::Complete;
                    return OdeLinearRequest::Complete;
                }

                if (t_current + H > dt_target) H = dt_target - t_current;

                assemble(c, J_mat, X_ODE, eos, burn_cfg);
                c.step_converged = false;
                c.optimal_k = 0;
                c.current_err = 0.0;
                c.k = 0;
                c.phase = Phase::BeginLevel;
                continue;
            }
            case Phase::BeginLevel:
                if (c.k == MAX_K) {
                    c.phase = Phase::FinishMacro;
                    continue;
                }
                c.m = c.n_seq[c.k];
                c.h = c.H / c.m;
                A.set_shifted_identity_from(J_mat, -c.h);
                c.phase = Phase::AwaitFactor;
                return OdeLinearRequest::Factorize;
            case Phase::FactorReady:
                if (!c.linear_success) {
                    c.phase = Phase::FinishMacro;
                    continue;
                }
#pragma omp simd
                for (int i = 0; i < NEQ; ++i) {
                    c.b[i] = c.h * c.RHS[i];
                    c.X_j[i] = X_ODE[i];
                }
                c.stage = 0;
                c.phase = Phase::AwaitSolve;
                return OdeLinearRequest::SolveWithFactors;
            case Phase::SolveReady: {
                if (!c.linear_success) {
                    c.phase = Phase::FinishMacro;
                    continue;
                }
                if (c.stage == 0) {
#pragma omp simd
                    for (int i = 0; i < NEQ; ++i) {
                        c.delta[i] = c.b[i];
                        c.increment[i] = c.delta[i];
                    }
                    materialize_midpoint_state(c, X_ODE, burn_cfg);
                    c.j = 1;
                } else if (c.stage == 1) {
                    bool simpr_failed = false;
#pragma omp simd
                    for (int i = 0; i < NEQ; ++i) {
                        if (!std::isfinite(c.b[i])) simpr_failed = true;
                        c.increment[i] += c.delta[i] + 2.0 * c.b[i];
                        c.delta[i] += 2.0 * c.b[i];
                    }
                    if (simpr_failed) {
                        c.phase = Phase::FinishMacro;
                        continue;
                    }
                    materialize_midpoint_state(c, X_ODE, burn_cfg);
                    ++c.j;
                } else {
#pragma omp simd
                    for (int i = 0; i < NEQ; ++i)
                        c.T_extrap[c.k][0][i] = c.increment[i] + c.b[i];
                    c.phase = Phase::AssessLevel;
                    continue;
                }
                // Midpoint updates and endpoint smoothing share h*f-delta.
                // Only consumption of the solved correction differs.
                midpoint_rhs(c, eos);
                c.stage = c.j < c.m ? 1 : 2;
                c.phase = Phase::AwaitSolve;
                return OdeLinearRequest::SolveWithFactors;
            }
            case Phase::AssessLevel:
                assess_level(c, X_ODE, eos, burn_cfg);
                continue;
            case Phase::FinishMacro:
                finish_macro(c, X_ODE, burn_cfg);
                continue;
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

    ARCH_HOST_DEVICE static void materialize_midpoint_state(
        Continuation& c, const double* initial, const BurnConfigView& burn_cfg)
    {
        for (int i = 0; i < NEQ; ++i) {
            const double candidate = c.accepted.incremented(i, c.increment[i]).value();
            double bounded = candidate;
            if (i < NUM_SPEC) {
                if (bounded < burn_cfg.smallx) bounded = burn_cfg.smallx;
                else if (bounded > 1.0) bounded = 1.0;
            } else if (i == NUM_SPEC && bounded < burn_cfg.nuclearTempMin) {
                bounded = burn_cfg.nuclearTempMin;
            }
            c.X_j[i] = bounded;
            // Project each midpoint stage into its admissible domain. Only a clamp resets
            // the increment; subtracting the baseline on every stage would
            // lose the small changes retained by this increment representation.
            if (bounded != candidate) c.increment[i] = bounded - initial[i];
        }
    }
    template <typename EOSType>
    ARCH_HOST_DEVICE static void assemble(
        Continuation& c, MatrixType& J_mat, double* X_ODE,
        const EOSType& eos, const BurnConfigView& burn_cfg)
    {
        // One shared Jacobian at the start of this macro step.
        OdeMath::assemble_burn_jacobian<NetType>(X_ODE, c.rho, eos, J_mat, c.RHS, c.network);
        OdeMath::calc_weights<NEQ>(X_ODE, burn_cfg.odeconfig.rtol, burn_cfg.odeconfig.atol, c.W);
    }

    template <typename EOSType>
    ARCH_HOST_DEVICE static void midpoint_rhs(Continuation& c, const EOSType& eos)
    {
        double stage_RHS[MAX_N];
        OdeMath::eval_burn_rhs<NetType>(c.X_j, c.rho, eos, stage_RHS, c.network);
#pragma omp simd
        for (int i = 0; i < NEQ; ++i) c.b[i] = c.h * stage_RHS[i] - c.delta[i];
    }

    template <typename EOSType>
    ARCH_HOST_DEVICE static void assess_level(
        Continuation& c, const double* X_ODE, const EOSType& eos, const BurnConfigView& burn_cfg)
    {
        auto& T_extrap = c.T_extrap; auto& X_err = c.X_err; auto& W = c.W;
        auto& X_trial = c.X_trial; auto& err_fac = c.err_fac;
        auto& current_err = c.current_err; auto& step_converged = c.step_converged;
        auto& optimal_k = c.optimal_k;
        const auto& n_seq = c.n_seq;
        const int k = c.k;
        const double rho = c.rho, rtol = burn_cfg.odeconfig.rtol, atol = burn_cfg.odeconfig.atol;
                // Extrapolate the midpoint result and obtain its truncation estimate.
                OdeMath::bd_extrapolate<NEQ, MAX_K>(k, n_seq, T_extrap, X_err);

                // At least two orders are required for an error estimate.
                if (k > 0)
                {
                    current_err = OdeMath::wrms_norm<NEQ>(X_err, W);

                    // Load the extrapolated candidate for physical checks.
#pragma omp simd
                    for(int i=0; i<NEQ; ++i)
                        X_trial[i] = c.accepted.incremented(i, T_extrap[k][k][i]).value();

                    bool physically_sound = true;
                    double mass_sum = 0.0;
                    // Permit negative extrapolation noise only within ten
                    // absolute-tolerance units before projection.
                    for (int i = 0; i < NUM_SPEC; ++i) {
                        if(X_trial[i] < -10.0*atol || !std::isfinite(X_trial[i])) physically_sound = false;
                        mass_sum += std::max(X_trial[i], burn_cfg.smallx);
                    }
                    // smallt is the configured lower boundary of the EOS burn state.
                    if(!std::isfinite(X_trial[NUM_SPEC]) || X_trial[NUM_SPEC] < burn_cfg.smallt) physically_sound = false;
                    for (int i = NUM_SPEC + 1; i < NEQ; ++i)
                        if (!std::isfinite(X_trial[i])) physically_sound = false;

                    // Run the more expensive EOS energy-closure check only for
                    // finite states whose normalized mathematical error passes.
                    if (physically_sound && current_err < 1.0)
                    {
                        const double inv_sum = 1.0 / mass_sum;
#pragma omp simd
                        for (int i = 0; i < NUM_SPEC; ++i) X_trial[i] = std::max(X_trial[i], burn_cfg.smallx) * inv_sum;

                        const double integrated_enuc = OdeMath::integrated_burn_increment_energy<NetType>(
                            T_extrap[k][k]);
                        const double old_eint = eos.get_eint_from_T(rho, X_ODE[NUM_SPEC], X_ODE);
                        const double new_eint = eos.get_eint_from_T(rho, X_trial[NUM_SPEC], X_trial);
                        const double thermal_delta = new_eint - old_eint;

                        const double epsilon_eint = std::max(1.0e-12 * std::abs(old_eint), 1.0e-12);
                        if (std::abs(thermal_delta) < epsilon_eint && std::abs(integrated_enuc) < epsilon_eint) {
                            step_converged = true;
                        }
                        else {
                            const double closure_scale = OdeMath::max4(
                                std::abs(integrated_enuc), std::abs(thermal_delta),
                                rtol * std::abs(old_eint), 1.0);
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
                        c.phase = Phase::FinishMacro;
                        return; // The first accepted order completes this macro step.
                    }
                    else if (k > 1 && k + 1 < MAX_K
                             && current_err > std::pow(
                                    static_cast<double>(n_seq[k + 1]) / n_seq[0], 2)) {
                        // Deuflhard's early-rejection heuristic stops raising
                        // order when the error already exceeds the improvement
                        // expected from the next substep-count ratio.
                        c.phase = Phase::FinishMacro;
                        return;
                    }
                }
        ++c.k;
        c.phase = Phase::BeginLevel;
    }

    ARCH_HOST_DEVICE static void finish_macro(
        Continuation& c, double* X_ODE, const BurnConfigView& cfg)
    {
        if (c.step_converged) {
            OdeMath::record_accepted_energy(c.accepted_energy, c.report,
                OdeMath::integrated_burn_increment_energy<NetType>(c.T_extrap[c.optimal_k][c.optimal_k]));
            c.t_current += c.H;
#pragma omp simd
            for (int i = 0; i < NEQ; ++i)
                X_ODE[i] = c.accepted.commit(i,
                    c.accepted.incremented(i, c.T_extrap[c.optimal_k][c.optimal_k][i]), c.X_trial[i]);
            c.H = recommend_macro_step(c.H, c.err_fac, c.optimal_k, cfg);
        } else {
            c.H *= 0.25;
            c.nse_attempted = false;
            ++c.report.rejected_substeps;
            if (c.H < 1e-22) {
#if !defined(__CUDA_ARCH__)
                std::cerr << "[BD] Fatal Error: Stiff ODE stalled. H < 1e-22" << std::endl;
#endif
                c.report.status = BurnOdeStatus::Stalled;
                c.phase = Phase::Complete;
                return;
            }
        }
        c.phase = Phase::BeginMacro;
    }
};
