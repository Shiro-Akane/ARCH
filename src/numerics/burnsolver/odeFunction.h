/**
 * @file odeFunction.h
 * @brief Common mathematical and physical toolkit for all ODE solvers.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include "../../core/ArchPortability.h"
#include "../../core/CompensatedSum.h"
#include "../../data/GlobalDefs.h"
#include "../../physics/nse/nse_solver.h"
#include "OdeContinuation.h"
#include "BurnThermodynamics.h"

namespace OdeMath
{
    // Accepted-step arithmetic, not another ODE algorithm. Keep sub-ULP
    // increments across accepted substeps; rejected trials never change this
    // state. A physical projection starts a new sum at the projected value.
    // Both backends store exactly this bounded O(NEQ) continuation payload.
    template <int Equations>
    class AcceptedState
    {
    public:
        ARCH_HOST_DEVICE void initialize(const double* values)
        {
            for (int i = 0; i < Equations; ++i) {
                sums_[i] = {};
                sums_[i].add(values[i]);
            }
        }
        ARCH_HOST_DEVICE arch::math::CompensatedSum sum(int i) const { return sums_[i]; }
        ARCH_HOST_DEVICE arch::math::CompensatedSum incremented(int i, double increment) const
        {
            auto result = sums_[i];
            result.add(increment);
            return result;
        }
        ARCH_HOST_DEVICE double commit(int i, arch::math::CompensatedSum trial, double projected)
        {
            if (trial.value() != projected) {
                trial = {};
                trial.add(projected);
            }
            sums_[i] = trial;
            return projected;
        }
    private:
        arch::math::CompensatedSum sums_[Equations];
    };

    // An optional passive, signed energy quadrature follows composition and
    // temperature. Its rate cannot depend on its accumulated value. Keeping it
    // in the existing ODE state supplies each method's own stage weights,
    // extrapolation, error control and rejected-step rollback automatically.
    template <class Network>
    inline constexpr bool has_nonconservative_energy = requires {
        Network::NONCONSERVATIVE_ENERGY_INDEX;
    };

    template <class Network>
    ARCH_HOST_DEVICE constexpr void check_burn_state_layout()
    {
        if constexpr (has_nonconservative_energy<Network>) {
            static_assert(Network::NONCONSERVATIVE_ENERGY_INDEX == Network::NUM_SPECIES + 1);
            static_assert(Network::ODE_NEQ == Network::NUM_SPECIES + 2);
            static_assert(!Network::SUPPORTS_NSE,
                "NSE with nonconservative energy needs a separately defined loss path");
        } else {
            static_assert(Network::ODE_NEQ == Network::NUM_SPECIES + 1);
        }
    }

    // Structural thermal/source rows accompany the network's species pattern.
    // This uses the same physical indices as assemble_burn_jacobian below;
    // the signed integral never feeds back into a physical RHS column.
    template <class Network, class Pattern>
    void include_burn_coupling(Pattern& pattern)
    {
        check_burn_state_layout<Network>();
        constexpr int temperature = Network::NUM_SPECIES + 1; // one-based
        for (int index = 1; index <= temperature; ++index) {
            pattern.set(index, temperature, 0.0);
            pattern.set(temperature, index, 0.0);
            if constexpr (has_nonconservative_energy<Network>)
                pattern.set(Network::NONCONSERVATIVE_ENERGY_INDEX + 1, index, 0.0);
        }
    }

    // Composition-derived nuclear energy only. Nonconservative sources such
    // as escaping-neutrino losses require their own time-integrated term;
    // they cannot be inferred from a change of isotope masses.
    template <class Network, class Increment>
    ARCH_INLINE double composition_increment_energy(const Increment& increment)
    {
        arch::math::CompensatedSum nuclear_mass_delta;
        for (int i = 0; i < Network::NUM_SPECIES; ++i) {
            const double molar_delta = increment[i] / Network::aion(i);
            nuclear_mass_delta.add_product(molar_delta, Network::energy_weight(i));
        }
        return Network::ENERGY_CONVERSION * nuclear_mass_delta.value();
    }

    struct StateDifference
    {
        const double* after;
        const double* before;
        ARCH_INLINE double operator[](int i) const { return after[i] - before[i]; }
    };

    template <class Network>
    ARCH_INLINE double integrated_composition_energy(const double* after, const double* before)
    {
        return composition_increment_energy<Network>(StateDifference{after, before});
    }

    template <class Network, class Increment>
    ARCH_INLINE double integrated_burn_increment_energy(const Increment& increment)
    {
        const double composition = composition_increment_energy<Network>(increment);
        if constexpr (has_nonconservative_energy<Network>) {
            constexpr int source = Network::NONCONSERVATIVE_ENERGY_INDEX;
            return composition + increment[source];
        }
        return composition;
    }

    template <class Network>
    ARCH_INLINE double integrated_burn_energy(const double* after, const double* before)
    {
        return integrated_burn_increment_energy<Network>(StateDifference{after, before});
    }

    ARCH_INLINE void record_accepted_energy(arch::math::CompensatedSum& integral,
                                           BurnOdeReport& report, double increment)
    {
        integral.add(increment);
        report.energy_change = integral.value();
    }

    /** BD work-based macro-step selection over an explicit order-cost table.
     * Only err_fac[1..accepted_order] has been evaluated. A higher candidate
     * uses its ratio estimate, never the unevaluated err_fac[accepted_order+1].
     */
    template <int Levels>
    ARCH_INLINE double bd_recommend_macro_step(
        double H, const double* err_fac, int accepted_order,
        const int* sequence, const double* work_cost,
        double min_factor, double max_factor)
    {
        double work_min = 1.0e20;
        double selected_factor = err_fac[accepted_order];
        for (int order = 1; order <= accepted_order; ++order) {
            const double step = H * err_fac[order] * 0.9;
            const double work = work_cost[order] / step;
            if (work < work_min) {
                work_min = work;
                selected_factor = err_fac[order];
            }
        }
        if (accepted_order < Levels - 1) {
            const double estimate = err_fac[accepted_order]
                * (static_cast<double>(sequence[accepted_order + 1]) / sequence[accepted_order]);
            const double step = H * estimate * 0.9;
            if (work_cost[accepted_order + 1] / step < work_min)
                selected_factor = estimate;
        }
        const double next = H * selected_factor * 0.9;
        return std::max(H * min_factor, std::min(H * max_factor, next));
    }

    // Basic vector operations.

    /**
     * @brief Apply the generic vector update X_new = X_old + alpha*dX.
     */
    template <int ODE_NEQ>
    ARCH_INLINE void vec_axpy(const double *X_old, double alpha, const double *dX, double *X_new)
    {
        // ODE_NEQ is a compile-time constant, allowing vectorization and full
        // unrolling for the compact supported network sizes.
#pragma omp simd
        for (int i = 0; i < ODE_NEQ; ++i)
        {
            X_new[i] = X_old[i] + alpha * dX[i];
        }
    }

    // Error weights and norms.

    /**
     * @brief Compute component weights W_i = RTOL*|Y_i| + ATOL.
     */
    template <int ODE_NEQ>
    ARCH_INLINE void calc_weights(const double *Y, double rtol, double atol, double *W)
    {
#pragma omp simd
        for (int i = 0; i < ODE_NEQ; ++i)
        {
            W[i] = rtol * std::abs(Y[i]) + atol;
        }
    }

    /**
     * @brief Compute the weighted root-mean-square error norm.
     */
    template <int ODE_NEQ>
    ARCH_INLINE double wrms_norm(const double *err_vec, const double *weight_vec)
    {
        double sum = 0.0;
        for (int i = 0; i < ODE_NEQ; ++i)
        {
            double val = err_vec[i] / weight_vec[i];
            sum += val * val;
        }
        return std::sqrt(sum / ODE_NEQ);
    }

    // Physical admissibility helpers.

    /**
     * @brief Clip and normalize the species mass fractions.
     * Only the first NUM_SPECIES entries are composition; the final temperature
     * component is deliberately excluded.
     */
    template <int NUM_SPECIES>
    ARCH_INLINE void enforce_mass_conservation(double *Y, double smallx)
    {
        double sum_X = 0.0;
        // Remove small negative values introduced by truncation error.
        for (int i = 0; i < NUM_SPECIES; ++i)
        {
            if (Y[i] < smallx)
                Y[i] = smallx;
            sum_X += Y[i];
        }

        // Normalize the surviving mass fractions to unit sum.
        double inv_sum = 1.0 / sum_X;
#pragma omp simd
        for (int i = 0; i < NUM_SPECIES; ++i)
        {
            Y[i] *= inv_sum;
        }
    }

    /**
     * @brief Clamp the explicitly indexed temperature, never an auxiliary state.
     */
    template <int T_INDEX>
    ARCH_INLINE void enforce_temperature_bounds(double *Y, double T_min, double T_max)
    {
        if (Y[T_INDEX] < T_min)
            Y[T_INDEX] = T_min;
        if (Y[T_INDEX] > T_max)
            Y[T_INDEX] = T_max;
    }

    // Adaptive PI step-size controller.

    ARCH_INLINE bool pi_uses_small_error_branch(double err_n)
    {
        return err_n < 1.0e-10;
    }

    /**
     * @brief Compute the next step from current and previous WRMS errors.
     * @param err_n Current-step WRMS error.
     * @param err_n_1 Previous-step WRMS error.
     * @param dt_n Current step size.
     * @return Proposed step size dt_next.
     */
    ARCH_INLINE double pi_controller(double err_n, double err_n_1, double dt_n,
                         int order_q, double safe, double min_fac, double max_fac)
    {
        // Exponents use the power of dt in the supplied local error estimate,
        // not the nonlinear iteration count or necessarily the solution order.
        const double k1 = 0.7 / order_q;
        const double k2 = 0.2 / order_q;

        if (pi_uses_small_error_branch(err_n))
            return dt_n * max_fac;

        double fac = safe * std::pow(err_n, -k1) * std::pow(err_n_1, k2);
        fac = std::max(min_fac, std::min(max_fac, fac));
        return dt_n * fac;
    }

    template <int Species>
    struct BurnRhsState {
        double energy = 0.0, cv = 0.0, eta = 0.0;
        double energy_composition_gradient[Species]{};
    };

    template <class Network, class EOS>
    ARCH_HEAVY_INLINE BurnRhsState<Network::NUM_SPECIES> eval_burn_rhs(
        const double* state, double rho, const EOS& eos, double* rhs,
        const Network& network = {})
    {
        check_burn_state_layout<Network>();
        constexpr int temperature = Network::NUM_SPECIES;
        BurnRhsState<Network::NUM_SPECIES> result{};
        result.eta = eos.get_eta(rho, state[temperature], state);
        if constexpr (has_nonconservative_energy<Network> && requires {
            network.eval_rhs(state, rho, result.eta, rhs, result.energy, rhs);
        }) {
            // Generated weak rates already produce total and signed source
            // energy together; do not evaluate the same tables a second time.
            network.eval_rhs(state, rho, result.eta, rhs, result.energy,
                             rhs + Network::NONCONSERVATIVE_ENERGY_INDEX);
        } else {
            network.eval_rhs(state, rho, result.eta, rhs, result.energy);
            if constexpr (has_nonconservative_energy<Network>)
                rhs[Network::NONCONSERVATIVE_ENERGY_INDEX] =
                    network.eval_nonconservative_energy(state, rho, result.eta);
        }
        result.cv = burn_cv_floor(eos.get_cv(rho, state[temperature], state));
        burn_energy_composition_gradient<Network::NUM_SPECIES + 1>(
            state, rho, eos, result.energy_composition_gradient);
        // At fixed rho: de/dt = cv*T' + sum_i e_Xi*X_i'. Nuclear/source
        // energy is unchanged; the changing mixture also changes EOS energy.
        rhs[temperature] = (result.energy - composition_energy_rate<Network::NUM_SPECIES>(
            result.energy_composition_gradient, rhs)) / result.cv;
        return result;
    }

    // Differentiate the complete first-law thermal RHS, including its EOS
    // composition Hessian contraction and cv denominator. Species/network
    // derivatives stay owned by the network and sparse pattern is unchanged.
    template <class Network, class Matrix, class EOS>
    ARCH_HEAVY_INLINE void assemble_burn_jacobian(
        const double* state, double rho, const EOS& eos, Matrix& matrix, double* rhs,
        const Network& network = {})
    {
        constexpr int species = Network::NUM_SPECIES;
        constexpr int physical_equations = species + 1;
        double energy_x[species]{}, rhs_t[species]{}, energy_t = 0.0;
        matrix.zero();
        const auto evaluated = eval_burn_rhs<Network>(state, rho, eos, rhs, network);
        network.eval_jacobian(state, rho, evaluated.eta, matrix, energy_x);
        network.eval_temperature_derivative(state, rho, evaluated.eta, rhs_t, energy_t);
        const double inverse_cv = 1.0 / evaluated.cv;
        // The auxiliary integral is not a species or a thermodynamic input.
        double cv_gradient[physical_equations]{};
        if (rhs[species] != 0.0)
            burn_cv_gradient<physical_equations>(state, rho, eos, evaluated.cv, cv_gradient);
        double energy_hessian_action[physical_equations]{};
        burn_energy_composition_hessian_action<physical_equations>(state, rho, eos, rhs,
            evaluated.energy_composition_gradient, energy_hessian_action);
        for (int i = 0; i < species; ++i) {
            matrix.set(i + 1, physical_equations, rhs_t[i]);
            arch::math::CompensatedSum composition_jacobian;
            for (int row = 0; row < species; ++row)
                composition_jacobian.add_product(evaluated.energy_composition_gradient[row], matrix(row + 1, i + 1));
            matrix.set(physical_equations, i + 1,
                (energy_x[i] - composition_jacobian.value() - energy_hessian_action[i]
                 - rhs[species] * cv_gradient[i]) * inverse_cv);
        }
        matrix.set(physical_equations, physical_equations,
            (energy_t - composition_energy_rate<species>(evaluated.energy_composition_gradient, rhs_t)
             - energy_hessian_action[species] - rhs[species] * cv_gradient[species]) * inverse_cv);
        if constexpr (has_nonconservative_energy<Network>) {
            double source_gradient[physical_equations]{};
            network.eval_nonconservative_gradient(state, rho, evaluated.eta, source_gradient);
            for (int column = 0; column < physical_equations; ++column)
                matrix.set(Network::NONCONSERVATIVE_ENERGY_INDEX + 1, column + 1,
                           source_gradient[column]);
        }
    }

    ARCH_INLINE double max4(double a, double b, double c, double d)
    {
        return std::max(std::max(a, b), std::max(c, d));
    }

    // Self-consistent NSE projection.
    template <typename NetType, typename EOSType>
    ARCH_HEAVY_INLINE bool integrate_nse_state(double* state, double rho,
                             double dt_target, const EOSType& eos,
                             const BurnConfigView& burn_cfg,
                             double& dt_rec, double* accepted_energy = nullptr)
    {
        // Network dimensions are compile-time properties of NetType.
        constexpr int NEQ = NetType::ODE_NEQ;
        constexpr int NUM_SPEC = NetType::NUM_SPECIES;
        constexpr int MAX_N = NEQ;

        struct Candidate {
            double temperature = 0.0;
            double x[MAX_N]{};
            double enuc = 0.0;
            double residual = 0.0;
            double scale = 1.0;
        };

        double old_x[MAX_N]{};
        arch::math::CompensatedSum ye_sum;
        for (int i = 0; i < NUM_SPEC; ++i) {
            old_x[i] = state[i];
            ye_sum.add(state[i] * (NetType::zion(i) / NetType::aion(i)));
        }
        const double ye = ye_sum.value();
        const double old_temperature = state[NEQ - 1];
        const double old_eint = eos.get_eint_from_T(rho, old_temperature, old_x);
        if (!std::isfinite(ye) || !std::isfinite(old_eint)) return false;

        auto evaluate = [&](double temperature, Candidate& candidate) {
            candidate.temperature = temperature;
            if (!NSESolver<NetType>::solve(temperature, rho, ye, old_x,
                                           candidate.x, candidate.enuc)) {
                return false;
            }
            const double new_eint = eos.get_eint_from_T(rho, temperature, candidate.x);
            candidate.residual = new_eint - old_eint - candidate.enuc;
            candidate.scale = max4(std::abs(new_eint), std::abs(old_eint),
                                   std::abs(candidate.enuc), 1.0);
            return std::isfinite(new_eint) && std::isfinite(candidate.residual) && std::isfinite(candidate.scale);
        };

        auto closed = [](const Candidate& candidate) {
            // A 1e-12 relative residual targets near-double-precision closure
            // without requiring bitwise cancellation of EOS energies.
            constexpr double closure_rtol = 1.0e-12;
            return std::abs(candidate.residual) <= closure_rtol * candidate.scale;
        };

        auto accept = [&](const Candidate& candidate) {
#pragma omp simd
            for (int i = 0; i < NUM_SPEC; ++i) state[i] = candidate.x[i];
            state[NEQ - 1] = candidate.temperature;
            dt_rec = dt_target;
            if (accepted_energy) *accepted_energy = candidate.enuc;
            return true;
        };

        const double minimum_temperature = std::max(burn_cfg.nseTempThreshold, burn_cfg.smallt);
        // Timmes burn/NSE states above 1e11 K are outside the maintained range.
        constexpr double maximum_temperature = 1.0e11;
        if (old_temperature < minimum_temperature || old_temperature > maximum_temperature) {
            return false;
        }

        Candidate current;
        if (!evaluate(old_temperature, current)) return false;
        if (closed(current)) return accept(current);

        Candidate previous;
        bool have_previous = false;
        // Twenty safeguarded Newton attempts bound work before bracketing fallback.
        for (int iter = 0; iter < 20; ++iter) {
            double derivative = eos.get_cv(rho, current.temperature, current.x);
            if (have_previous && current.temperature != previous.temperature) {
                const double secant = (current.residual - previous.residual) / (current.temperature - previous.temperature);
                if (std::isfinite(secant) && secant > 0.0) derivative = secant;
            }
            if (!std::isfinite(derivative) || derivative <= 0.0) break;

            double delta_temperature = -current.residual / derivative;
            // Limit one Newton correction to half the current temperature so
            // the undamped proposal cannot cross zero.
            const double step_limit = 0.5 * current.temperature;
            delta_temperature = std::clamp(delta_temperature, -step_limit, step_limit);

            bool improved = false;
            double alpha = 1.0;
            Candidate trial;
            // Sixteen trial levels reach a minimum damping of 2^-15 before fallback.
            for (int line_search = 0; line_search < 16; ++line_search) {
                const double trial_temperature = std::clamp(current.temperature + alpha * delta_temperature, minimum_temperature, maximum_temperature);
                if (trial_temperature == current.temperature) break;
                if (evaluate(trial_temperature, trial) && std::abs(trial.residual) < std::abs(current.residual)) {
                    improved = true;
                    break;
                }
                alpha *= 0.5;
            }
            if (!improved) break;
            previous = current;
            have_previous = true;
            current = trial;
            if (closed(current)) return accept(current);
        }

        Candidate lower;
        Candidate upper;
        if (!evaluate(minimum_temperature, lower) || !evaluate(maximum_temperature, upper) || std::signbit(lower.residual) == std::signbit(upper.residual)) {
            return false;
        }
        if (lower.residual > 0.0) std::swap(lower, upper);

        for (int iter = 0; iter < 64; ++iter) {
            Candidate midpoint;
            const double midpoint_temperature = 0.5 * (lower.temperature + upper.temperature);
            if (!evaluate(midpoint_temperature, midpoint)) return false;
            if (closed(midpoint)) return accept(midpoint);
            if (midpoint.residual < 0.0) lower = midpoint;
            else upper = midpoint;
        }
        return false;
    }

    // Bader-Deuflhard polynomial extrapolation.

    /**
     * @brief Raise the semi-implicit midpoint solution through polynomial extrapolation.
     * @param k Current extrapolation level in [0, MAX_K-1].
     * @param n_seq Bader-Deuflhard substep sequence, such as 2, 6, 10, 14.
     * @param T Extrapolation tableau T[k][j][NEQ]; BD stores increments from
     *          the macro-step state to avoid cancelling a large common offset.
     * @param y_err Output truncation-error estimate.
     */
    template <int ODE_NEQ, int MAX_K>
    ARCH_INLINE void bd_extrapolate(int k, const int* n_seq,
                        double T[MAX_K][MAX_K][ODE_NEQ],
                        double* y_err)
    {
        // Starting at j=1, combine lower-order entries to obtain the next order.
        for (int j = 1; j <= k; ++j)
        {
            double fac = static_cast<double>(n_seq[k] * n_seq[k]) /
                         static_cast<double>(n_seq[k - j] * n_seq[k - j]) - 1.0;
            const double inv_fac = 1.0 / fac;

#pragma omp simd
            for (int i = 0; i < ODE_NEQ; ++i)
            {
                T[k][j][i] = T[k][j - 1][i] + (T[k][j - 1][i] - T[k - 1][j - 1][i]) * inv_fac;
            }
        }

        // Deuflhard's truncation estimate is the difference between the two
        // highest available extrapolation orders.
        if (k > 0) {
#pragma omp simd
            for (int i = 0; i < ODE_NEQ; ++i) {
                y_err[i] = T[k][k][i] - T[k][k - 1][i];
            }
        }
    }

} // namespace OdeMath
