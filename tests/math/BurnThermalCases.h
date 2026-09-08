// Independent analytic self-heating witnesses; shared only by test consumers.
#pragma once
#include "numerics/burnsolver/ode_ros4.h"
#include "numerics/burnsolver/ode_be-nr.h"
#include "numerics/burnsolver/ode_bd.h"
#include "numerics/linalg/DenseWrap.h"
#include "physics/eos/IdealGas.h"
#include <cmath>

namespace arch::test::burn_thermal {
struct Reaction {
    static constexpr int NUM_SPECIES = 2, ODE_NEQ = 3;
    static constexpr bool SUPPORTS_NSE = false;
    static constexpr double ENERGY_CONVERSION = 1.0;
    ARCH_HOST_DEVICE static double aion(int) { return 1.0; }
    ARCH_HOST_DEVICE static double energy_weight(int i) { return i == 0 ? 0.0 : 1.0; }
    ARCH_HOST_DEVICE static void eval_rhs(const double* x, double, double, double* f, double& e) {
        f[0] = -x[0]; f[1] = x[0]; e = x[0];
    }
    template<class Matrix>
    ARCH_HOST_DEVICE static void eval_jacobian(const double*, double, double, Matrix& m, double* e) {
        m.set(1, 1, -1.0); m.set(2, 1, 1.0); e[0] = 1.0; e[1] = 0.0;
    }
    ARCH_HOST_DEVICE static void eval_temperature_derivative(
        const double*, double, double, double* f, double& e) { f[0] = f[1] = e = 0.0; }
};
struct Eos {
    bool composition;
    ARCH_HOST_DEVICE double get_cv(double, double t, const double* x) const {
        return t * (composition ? 1.0 + x[1] : 1.0);
    }
    ARCH_HOST_DEVICE double get_eta(double, double, const double*) const { return 0.0; }
    ARCH_HOST_DEVICE double get_eint_from_T(double rho, double t, const double* x) const {
        return 0.5 * t * get_cv(rho, t, x);
    }
    template <int Equations>
    ARCH_HOST_DEVICE void get_cv_gradient(double, double t, const double* x, double* gradient) const {
        static_assert(Equations == 3);
        gradient[0] = 0.0; gradient[1] = composition ? t : 0.0;
        gradient[2] = composition ? 1.0 + x[1] : 1.0;
    }
    template <int Equations>
    ARCH_HOST_DEVICE void get_energy_composition_gradient(double, double t, const double*, double* gradient) const {
        static_assert(Equations == 3);
        gradient[0] = 0.0; gradient[1] = composition ? 0.5 * t * t : 0.0;
    }
    template <int Equations>
    ARCH_HOST_DEVICE void get_energy_composition_hessian_action(
        double, double t, const double*, const double* flow, double* action) const {
        static_assert(Equations == 3);
        action[0] = action[1] = 0.0; action[2] = composition ? t * flow[1] : 0.0;
    }
};

ARCH_INLINE bool first_law_contract()
{
    const double state[3]{0.5, 0.5, 1.0};
    double rhs[3];
    const auto result = OdeMath::eval_burn_rhs<Reaction>(state, 1.0, Eos{true}, rhs);
    const double energy_rate = 1.5 * rhs[2] + 0.5 * rhs[1];
    return std::abs(energy_rate - 0.5) < 1e-14 && result.energy == 0.5;
}

ARCH_INLINE bool jacobian_contract()
{
    {
        const double coefficients[2]{3.0, 7.0};
        IdealGasView eos;
        eos.species.Cv = coefficients;
        eos.species.count = 2;
        const double state[3]{0.25, 0.75, 2.0};
        double gradient[3];
        OdeMath::burn_cv_gradient<3>(state, 1.0, eos, eos.get_cv(1.0, 2.0, state), gradient);
        if (gradient[0] != 3.0 || gradient[1] != 7.0 || gradient[2] != 0.0) return false;
        // A clamped denominator has zero derivative in the active flat branch.
        OdeMath::burn_cv_gradient<3>(state, 1.0, eos, OdeMath::burn_cv_floor(0.0), gradient);
        if (gradient[0] != 0.0 || gradient[1] != 0.0 || gradient[2] != 0.0) return false;
        eos.species = {};
        OdeMath::burn_cv_gradient<3>(state, 1.0, eos, eos.get_cv(1.0, 2.0, state), gradient);
        if (gradient[0] != 0.0 || gradient[1] != 0.0 || gradient[2] != 0.0) return false;
    }
    for (bool composition : {false, true}) {
        const Eos eos{composition};
        // Both one-sided composition boundaries and a centered interior.
        for (double fraction : {0.0, 0.5, 1.0}) {
            const double state[3]{fraction, 1.0 - fraction, 2.0};
            DenseMatrixData<3> matrix{};
            double rhs[3];
            OdeMath::assemble_burn_jacobian<Reaction>(state, 1.0, eos, matrix, rhs);
            const double factor = composition ? 1.0 + state[1] : 1.0;
            const double heat = composition ? -1.0 : 1.0; // 1 - T^2/2 at T=2
            const double expected[3][3]{{-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0},
                {heat / (2.0 * factor), composition ? -fraction * heat / (2.0 * factor * factor) : 0.0,
                 -fraction * (composition ? 0.75 : 0.25) / factor}};
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    if (!std::isfinite(matrix(i + 1, j + 1))
                        || std::abs(matrix(i + 1, j + 1) - expected[i][j]) > 2.e-10) return false;
            if (state[0] != fraction || state[1] != 1.0 - fraction || state[2] != 2.0) return false;
        }
    }
    return true;
}

struct Convergence {
    double composition_error[3]{}, temperature_error[3]{};
    bool success = true;
};

struct NoHeatingReaction : Reaction {
    static constexpr double ENERGY_CONVERSION = 0.0;
    ARCH_HOST_DEVICE static void eval_rhs(const double* x, double rho, double eta, double* f, double& e) {
        Reaction::eval_rhs(x, rho, eta, f, e); e = 0.0;
    }
    template<class Matrix>
    ARCH_HOST_DEVICE static void eval_jacobian(const double* x, double rho, double eta, Matrix& m, double* e) {
        Reaction::eval_jacobian(x, rho, eta, m, e); e[0] = e[1] = 0.0;
    }
};

struct ToleranceControl { bool success = true; double error[2]{}; int attempts[2]{}; };

// A runtime-bound immutable rate, not a static policy constant. Two views of
// the same network type must coexist without a process-global selected value.
struct BoundRateReaction : NoHeatingReaction {
    const double* rate = nullptr;
    ARCH_HOST_DEVICE double coefficient() const {
        return rate ? *rate : std::numeric_limits<double>::quiet_NaN();
    }
    ARCH_HOST_DEVICE void eval_rhs(const double* x, double, double, double* f, double& e) const {
        f[0] = -coefficient() * x[0]; f[1] = -f[0]; e = 0.0;
    }
    template<class Matrix>
    ARCH_HOST_DEVICE void eval_jacobian(const double*, double, double, Matrix& m, double* e) const {
        m.set(1, 1, -coefficient()); m.set(2, 1, coefficient()); e[0] = e[1] = 0.0;
    }
};
struct BoundNetworkResult { bool success = true; double abundance[3][2]{}; };
template<template<class, class, class> class Solver>
ARCH_INLINE bool bound_network_path(const BurnConfigView& original, double* final)
{
    auto cfg = original;
    cfg.odeconfig.rtol = 1.e-6; cfg.odeconfig.atol = 1.e-10;
    cfg.odeconfig.max_substeps = 10000;
    const double rates[2]{0.5, 2.0};
    double states[2][3]{{0.5, 0.5, 1.0}, {0.5, 0.5, 1.0}};
    using Ode = Solver<BoundRateReaction, DenseMatrixData<3>, DenseLUSolver>;
    for (int step = 0; step < 4; ++step)
        for (int lane = 0; lane < 2; ++lane) {
            double dt = 0.025;
            BoundRateReaction view; view.rate = rates + lane;
            const auto report = Ode::integrate_report(states[lane], 1.0, dt, Eos{false}, cfg, dt, view);
            if (!report.success()) return false;
        }
    for (int lane = 0; lane < 2; ++lane) {
        final[lane] = states[lane][0];
        if (std::abs(final[lane] - 0.5 * std::exp(-rates[lane] * 0.1)) > 1.e-4
            || states[lane][2] != 1.0) return false;
    }
    return true;
}
ARCH_INLINE BoundNetworkResult bound_network_suite(const BurnConfigView& cfg)
{
    BoundNetworkResult result;
    result.success = bound_network_path<Solver_BE_NR>(cfg, result.abundance[0])
        && bound_network_path<Solver_BD>(cfg, result.abundance[1])
        && bound_network_path<Solver_ROS4>(cfg, result.abundance[2]);
    return result;
}

struct Ros4FailureControl {
    bool success = true;
    bool lane_pass[4]{}, nonfinite_matrix[4]{};
    BurnOdeReport reports[4]{};
};

// The same rejected interval must terminate identically whether its provider
// rejects factorization or a stage solve. The final lane uses a genuinely
// unbound rate view and the real DenseLU nonfinite-matrix rejection.
ARCH_INLINE Ros4FailureControl ros4_failure_control(BurnConfigView config)
{
    using Ode = Solver_ROS4<BoundRateReaction, DenseMatrixData<3>, DenseLUSolver>;
    Ros4FailureControl result;
    const double rate = 1.0, interval = 1.e-20;
    for (int lane = 0; lane < 4; ++lane) {
        config.odeconfig.max_substeps = lane == 1 ? 2 : 10;
        config.odeconfig.initial_dt_frac = 1.0;
        double state[3]{0.5, 0.5, 1.0};
        DenseMatrixData<3> jacobian{}, matrix{};
        int pivots[3]{};
        BoundRateReaction view;
        view.rate = lane == 3 ? nullptr : &rate;
        typename Ode::Continuation context;
        Ode::begin(context, state, 1.0, interval, config, interval, view);
        bool completed = false, saw_nonfinite_matrix = false;
        // Bound the test itself if a broken continuation stops making progress.
        for (int request_count = 0; request_count < 64; ++request_count) {
            const auto request = Ode::advance(context, jacobian, matrix, state, Eos{false}, config);
            if (request == OdeLinearRequest::Complete) { completed = true; break; }
            bool success = false;
            if (request == OdeLinearRequest::Factorize) {
                for (int i = 1; i <= 3; ++i)
                    for (int j = 1; j <= 3; ++j)
                        saw_nonfinite_matrix = saw_nonfinite_matrix || !std::isfinite(matrix(i, j));
                if (lane >= 2) success = DenseLUSolver::factorize<3, 3>(matrix, pivots);
            } else if (request == OdeLinearRequest::SolveWithFactors) {
                if (lane != 2) {
                    DenseLUSolver::solve_with_factors<3, 3>(matrix, pivots, context.b);
                    success = true;
                }
            } else result.success = false;
            result.success = Ode::complete_linear_solve(context, success) && result.success;
        }
        const auto& report = context.report;
        // Record logical report fields, not a padding-bearing aggregate copy.
        result.reports[lane] = BurnOdeReport{
            .status = report.status,
            .attempted_substeps = report.attempted_substeps,
            .rejected_substeps = report.rejected_substeps,
            .nse_attempts = report.nse_attempts,
            .nse_failures = report.nse_failures,
            .dt_recommended = report.dt_recommended,
            .energy_change = report.energy_change};
        result.nonfinite_matrix[lane] = saw_nonfinite_matrix;
        result.lane_pass[lane] = completed && !report.success()
            && report.status == (lane == 1 ? BurnOdeStatus::MaxSubsteps : BurnOdeStatus::Stalled)
            && report.attempted_substeps == (lane == 1 ? 3 : 4)
            && report.rejected_substeps == (lane == 1 ? 2 : 4)
            && report.nse_attempts == 0 && report.nse_failures == 0
            && report.dt_recommended == interval && context.dt_recommended == interval
            && report.energy_change == 0.0
            && state[0] == 0.5 && state[1] == 0.5 && state[2] == 1.0
            && saw_nonfinite_matrix == (lane == 3);
        result.success = result.success && result.lane_pass[lane];
    }
    return result;
}

ARCH_INLINE ToleranceControl be_tolerance_control(BurnConfigView config)
{
    ToleranceControl result;
    const double exact = 0.5 * std::exp(-1.0);
    for (int level = 0; level < 2; ++level) {
        config.odeconfig.rtol = level == 0 ? 1.e-2 : 1.e-4;
        config.odeconfig.atol = 1.e-10;
        config.odeconfig.max_substeps = 10000;
        double state[3]{0.5, 0.5, 1.0}, dt = 1.0;
        const auto report = Solver_BE_NR<NoHeatingReaction, DenseMatrixData<3>, DenseLUSolver>::integrate_report(
            state, 1.0, 1.0, Eos{false}, config, dt);
        result.error[level] = std::abs(state[0] - exact);
        result.attempts[level] = report.attempted_substeps;
        result.success = result.success && report.success() && report.rejected_substeps > 0
            && state[2] == 1.0 && std::abs(state[0] + state[1] - 1.0) < 1.e-14;
    }
    // First-order global error with a second-order local error estimator:
    // reducing the local budget by 100 should improve the solution about 10x.
    const double ratio = result.error[0] / result.error[1];
    result.success = result.success && ratio > 7.0 && ratio < 13.0
        && result.attempts[1] > result.attempts[0] && result.error[1] < 3.e-3;
    return result;
}
ARCH_INLINE Convergence ros4_convergence(bool composition, const BurnConfigView& config)
{
    Convergence result;
    const double exact_a = 0.5 * std::exp(-1.0);
    // A'=-A, B'=A, e'=A. For e=T^2(1+B)/2, the FIRST LAW gives
    // e(t)=e(0)+A(0)-A(t); omitting e_B B' violates this identity.
    const double exact_t = composition
        ? std::sqrt((2.5 - 2.0 * exact_a) / (2.0 - exact_a))
        : std::sqrt(2.0 - std::exp(-1.0));
    for (int level = 0; level < 3; ++level) {
        const int steps = 16 << level;
        double state[3]{0.5, 0.5, 1.0};
        for (int step = 0; step < steps; ++step) {
            double dt = 1.0 / steps;
            const auto report = Solver_ROS4<Reaction, DenseMatrixData<3>, DenseLUSolver>::integrate_report(
                state, 1.0, dt, Eos{composition}, config, dt);
            result.success = result.success && report.success()
                && report.attempted_substeps == 1 && report.rejected_substeps == 0;
        }
        result.composition_error[level] = std::abs(state[0] - exact_a);
        result.temperature_error[level] = std::abs(state[2] - exact_t);
        if (level > 0) {
            const double ratio_x = result.composition_error[level - 1] / result.composition_error[level];
            const double ratio_t = result.temperature_error[level - 1] / result.temperature_error[level];
            result.success = result.success && ratio_x > 14.0 && ratio_x < 18.0
                && ratio_t > 14.0 && ratio_t < 18.0;
        }
    }
    return result;
}

inline BurnConfigView convergence_config()
{
    BurnConfig settings;
    settings.smallx = 1.e-30; settings.smallt = 0.0;
    settings.nuclearTempMin = 0.0; settings.nuclearDensMin = 0.0;
    settings.use_nse = false;
    // This test fixes the external timestep and asserts exactly one internal
    // accepted step; it tests order, not production tolerance acceptance.
    settings.odeconfig.rtol = settings.odeconfig.atol = 1.0;
    settings.odeconfig.initial_dt_frac = 1.0;
    return make_burn_config_view(settings);
}

// Passive signed energy quadrature: A'=-A, B'=A, Q_mass=A,
// Q_external=-2(A+T), cv=1, so T'=-A-2T. The exact solution is
// T=(T0+A0)exp(-2t)-A0 exp(-t). A0=0 isolates cooling without any
// composition change; recovering energy only from isotope masses must fail.
struct ExternalEnergyReaction : Reaction {
    static constexpr int ODE_NEQ = 4;
    static constexpr int NONCONSERVATIVE_ENERGY_INDEX = 3;
    ARCH_HOST_DEVICE static void eval_rhs(const double* x, double rho, double eta, double* f, double& e) {
        Reaction::eval_rhs(x, rho, eta, f, e); e = -x[0] - 2.0 * x[2];
    }
    template<class Matrix>
    ARCH_HOST_DEVICE static void eval_jacobian(const double* x, double rho, double eta, Matrix& m, double* e) {
        Reaction::eval_jacobian(x, rho, eta, m, e); e[0] = -1.0;
    }
    ARCH_HOST_DEVICE static void eval_temperature_derivative(
        const double*, double, double, double* f, double& e) { f[0] = f[1] = 0.0; e = -2.0; }
    ARCH_HOST_DEVICE static double eval_nonconservative_energy(const double* x, double, double) {
        return -2.0 * (x[0] + x[2]);
    }
    ARCH_HOST_DEVICE static void eval_nonconservative_gradient(const double*, double, double, double* g) {
        g[0] = -2.0; g[1] = 0.0; g[2] = -2.0;
    }
};
struct UnitCvEos {
    ARCH_HOST_DEVICE double get_cv(double, double, const double*) const { return 1.0; }
    ARCH_HOST_DEVICE double get_eta(double, double, const double*) const { return 0.0; }
    ARCH_HOST_DEVICE double get_eint_from_T(double, double t, const double*) const { return t; }
};

// Exact constant-source witness: X'=0, T'=q, signed energy'=q. Substep
// accumulation/extrapolation must not turn four representable temperature ULPs
// into six merely because a large common background is present.
struct ConstantThermalSource {
    static constexpr int NUM_SPECIES = 1, ODE_NEQ = 3;
    static constexpr int NONCONSERVATIVE_ENERGY_INDEX = 2;
    static constexpr bool SUPPORTS_NSE = false;
    static constexpr double ENERGY_CONVERSION = 1.0;
    double rate = 0.0;
    ARCH_HOST_DEVICE static double aion(int) { return 1.0; }
    ARCH_HOST_DEVICE static double energy_weight(int) { return 0.0; }
    ARCH_HOST_DEVICE void eval_rhs(const double*, double, double, double* f, double& e) const {
        f[0] = 0.0; e = rate;
    }
    template<class Matrix>
    ARCH_HOST_DEVICE static void eval_jacobian(const double*, double, double, Matrix&, double* e) {
        e[0] = 0.0;
    }
    ARCH_HOST_DEVICE static void eval_temperature_derivative(
        const double*, double, double, double* f, double& e) { f[0] = e = 0.0; }
    ARCH_HOST_DEVICE double eval_nonconservative_energy(const double*, double, double) const {
        return rate;
    }
    ARCH_HOST_DEVICE static void eval_nonconservative_gradient(const double*, double, double, double* g) {
        g[0] = g[1] = 0.0;
    }
};
struct ThermalTranslationControl { bool success = true; double temperature_error_ulps[4]{}; };

// A sub-ULP species transfer can release representable heat. The expected
// integral is the dyadic rate times its binding-energy difference, not a
// subtraction of the rounded endpoint abundances or EOS energies.
struct ConstantCompositionSource : Reaction {
    double rate = 0.0;
    ARCH_HOST_DEVICE static double energy_weight(int i) { return i == 0 ? 0.0 : 0x1p60; }
    ARCH_HOST_DEVICE void eval_rhs(const double*, double, double, double* f, double& e) const {
        f[0] = -rate; f[1] = rate; e = rate * 0x1p60;
    }
    template<class Matrix>
    ARCH_HOST_DEVICE void eval_jacobian(const double*, double, double, Matrix&, double* e) const {
        e[0] = e[1] = 0.0;
    }
};

template<template<class, class, class> class Policy>
ARCH_INLINE bool composition_energy_control(BurnConfigView config)
{
    config.odeconfig.initial_dt_frac = 1.0;
    using Solver = Policy<ConstantCompositionSource, DenseMatrixData<3>, DenseLUSolver>;
    for (const double sign : {-1.0, 1.0}) {
        ConstantCompositionSource source;
        source.rate = sign * 0x1p-60;
        double state[3]{0.5, 0.5, 4.0}, dt = 1.0;
        const auto report = Solver::integrate_report(state, 1.0, dt, UnitCvEos{}, config, dt, source);
        if (!report.success() || report.rejected_substeps != 0 || report.attempted_substeps != 1
            || state[0] != 0.5 || state[1] != 0.5
            || std::abs(report.energy_change - sign) > 4.0 * std::numeric_limits<double>::epsilon()
            || std::abs(state[2] - (4.0 + sign)) > 4.0 * std::numeric_limits<double>::epsilon()) return false;
    }
    return true;
}

ARCH_INLINE bool accepted_state_contract()
{
    OdeMath::AcceptedState<1> state;
    const double initial[1]{0x1p30};
    state.initialize(initial);
    // Inspecting/rejecting trial copies must not publish any of their changes.
    for (int i = 0; i < 16; ++i) {
        const auto discarded = state.incremented(0, 1.0);
        if (discarded.value() != initial[0] + 1.0 || state.sum(0).value() != initial[0]) return false;
    }
    for (int i = 0; i < 16; ++i) {
        const auto trial = state.incremented(0, 0x1p-25);
        state.commit(0, trial, trial.value());
    }
    if (state.sum(0).value() != initial[0] + 0x1p-21) return false;
    // A genuine physical projection clears the old compensation as well.
    const auto projected_trial = state.incremented(0, 0x1p-25);
    state.commit(0, projected_trial, 1.0);
    return state.sum(0).value() == 1.0
        && state.incremented(0, 0x1p-52).value() == 1.0 + 0x1p-52;
}

template<template<class, class, class> class Policy>
ARCH_INLINE ThermalTranslationControl translation_control(BurnConfigView config, int substeps = 1)
{
    ThermalTranslationControl result;
    config.odeconfig.rtol = 1.e-7; config.odeconfig.atol = 1.e-14;
    config.odeconfig.initial_dt_frac = 1.0 / substeps;
    // Pin both ends: BD has its own safety factor before the final factor
    // clamp, so setting only the maximum does not produce fixed substeps.
    config.odeconfig.dt_fac_min = config.odeconfig.dt_fac_max = 1.0;
    config.odeconfig.dt_safe_factor = 1.0;
    using Solver = Policy<ConstantThermalSource, DenseMatrixData<3>, DenseLUSolver>;
    for (int offset = 0; offset < 2; ++offset)
        for (int sign = 0; sign < 2; ++sign)
          for (int coefficient = 1; coefficient <= 64; ++coefficient) {
            const double initial = offset == 0 ? 1.0 : 0x1p30;
            // Dyadic rates span half-ULP ties through many representable
            // increments; do not qualify only a favorable single rate.
            const double rate = coefficient * 0x1p-23;
            const ConstantThermalSource source{sign == 0 ? rate : -rate};
            double state[3]{1.0, initial, 0.0}, dt = 1.0;
            const auto report = Solver::integrate_report(state, 1.0, dt, UnitCvEos{}, config, dt, source);
            const double exact = initial + source.rate;
            const double ulp = std::nextafter(exact, std::numeric_limits<double>::infinity()) - exact;
            const double error = std::abs(state[1] - exact) / ulp;
            auto& worst = result.temperature_error_ulps[2 * offset + sign];
            worst = std::max(worst, error);
            result.success = result.success && report.success() && report.attempted_substeps == substeps
                && report.rejected_substeps == 0 && state[0] == 1.0 && error <= 1.0
                && std::abs(state[2] - source.rate)
                    <= 4.0 * std::numeric_limits<double>::epsilon() * std::abs(source.rate)
                && std::abs(report.energy_change - source.rate)
                    <= 4.0 * std::numeric_limits<double>::epsilon() * std::abs(source.rate);
        }
    return result;
}

ARCH_INLINE bool external_energy_jacobian_contract()
{
    const double state[4]{0.125, 0.875, 1.0, -0.25};
    DenseMatrixData<4> matrix;
    double rhs[4]{};
    OdeMath::assemble_burn_jacobian<ExternalEnergyReaction>(state, 1.0, UnitCvEos{}, matrix, rhs);
    constexpr double expected[4][4]{{-1, 0, 0, 0}, {1, 0, 0, 0},
                                    {-1, 0, -2, 0}, {-2, 0, -2, 0}};
    constexpr double expected_rhs[4]{-0.125, 0.125, -2.125, -2.25};
    for (int i = 0; i < 4; ++i) {
        if (rhs[i] != expected_rhs[i]) return false;
        for (int j = 0; j < 4; ++j)
            if (matrix(i + 1, j + 1) != expected[i][j]) return false;
    }
    return true;
}

struct ExternalEnergyPath {
    bool success = true;
    double temperature_error[3]{}, quadrature_error[3]{};
    double accepted_state[3][4]{};
};
template<template<class, class, class> class Policy>
ARCH_INLINE ExternalEnergyPath external_energy_path(bool reacting, const BurnConfigView& config)
{
    ExternalEnergyPath result;
    const double a0 = reacting ? 0.125 : 0.0;
    const double exact_a = a0 * std::exp(-1.0);
    const double exact_t = (1.0 + a0) * std::exp(-2.0) - exact_a;
    const double exact_quadrature = exact_t - 1.0 - (a0 - exact_a);
    for (int level = 0; level < 3; ++level) {
        const int steps = 8 << level;
        double state[4]{a0, 1.0 - a0, 1.0, 0.0};
        arch::math::CompensatedSum accepted_energy;
        for (int step = 0; step < steps; ++step) {
            double dt = 1.0 / steps;
            const auto report = Policy<ExternalEnergyReaction, DenseMatrixData<4>, DenseLUSolver>::integrate_report(
                state, 1.0, dt, UnitCvEos{}, config, dt);
            result.success = result.success && report.success() && report.rejected_substeps == 0;
            accepted_energy.add(report.energy_change);
        }
        result.temperature_error[level] = std::abs(state[2] - exact_t);
        result.quadrature_error[level] = std::abs(state[3] - exact_quadrature);
        for (int i = 0; i < 4; ++i) result.accepted_state[level][i] = state[i];
        result.success = result.success && std::isfinite(state[3]) && state[3] < 0.0
            && std::abs(state[0] + state[1] - 1.0) < 1.e-14
            && std::abs(accepted_energy.value() - (state[2] - 1.0)) < 1.e-13
            && std::abs((state[2] - 1.0) - (a0 - state[0] + state[3])) < 1.e-13;
        if (level > 0) {
            // Each policy must actually converge under time refinement.
            // Do not infer a named high order from this lower-bound check.
            result.success = result.success
                && result.temperature_error[level - 1] > 1.7 * result.temperature_error[level]
                && result.quadrature_error[level - 1] > 1.7 * result.quadrature_error[level];
        }
    }
    return result;
}

struct ExternalEnergySuite {
    bool jacobian;
    bool rollback[3];
    ExternalEnergyPath paths[3][2];
};
template<template<class, class, class> class Policy>
ARCH_INLINE bool external_energy_rollback(BurnConfigView config)
{
    using Solver = Policy<ExternalEnergyReaction, DenseMatrixData<4>, DenseLUSolver>;
    config.odeconfig.max_substeps = 1;
    typename Solver::Continuation context;
    DenseMatrixData<4> jacobian, system;
    const double before[4]{0.125, 0.875, 1.0, -0.25};
    double state[4];
    for (int i = 0; i < 4; ++i) state[i] = before[i];
    Solver::begin(context, state, 1.0, 0.125, config, 0.125);
    const auto request = Solver::advance(context, jacobian, system, state, UnitCvEos{}, config);
    if (request == OdeLinearRequest::Complete
        || Solver::advance(context, jacobian, system, state, UnitCvEos{}, config) != request
        || !Solver::complete_linear_solve(context, false)) return false;
    // Inject provider failure before the first accepted substep. The rejected
    // trial must not publish temperature OR the accumulated signed loss.
    if (Solver::advance(context, jacobian, system, state, UnitCvEos{}, config)
            != OdeLinearRequest::Complete
        || context.report.success() || context.report.rejected_substeps != 1
        || context.report.energy_change != 0.0) return false;
    for (int i = 0; i < 4; ++i)
        if (state[i] != before[i]) return false;
    return true;
}
ARCH_INLINE ExternalEnergySuite external_energy_suite(const BurnConfigView& config)
{
    ExternalEnergySuite suite{};
    suite.jacobian = external_energy_jacobian_contract();
    suite.rollback[0] = external_energy_rollback<Solver_BE_NR>(config);
    suite.rollback[1] = external_energy_rollback<Solver_BD>(config);
    suite.rollback[2] = external_energy_rollback<Solver_ROS4>(config);
    for (int reacting = 0; reacting < 2; ++reacting) {
        suite.paths[0][reacting] = external_energy_path<Solver_BE_NR>(reacting != 0, config);
        suite.paths[1][reacting] = external_energy_path<Solver_BD>(reacting != 0, config);
        suite.paths[2][reacting] = external_energy_path<Solver_ROS4>(reacting != 0, config);
    }
    return suite;
}
}
