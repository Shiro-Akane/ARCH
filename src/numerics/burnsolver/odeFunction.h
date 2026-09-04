/**
 * @file odeFunction.h
 * @brief Common mathematical and physical toolkit for all ODE solvers.
 */
#pragma once

#include <algorithm>
#include <cmath>

#include "../../core/ArchPortability.h"
#include "../../core/CompensatedSum.h"
#include "../../data/GlobalDefs.h"
#include "../../physics/nse/nse_solver.h"

template <typename MatrixType>
struct OdeMatrixWorkspace
{
    MatrixType jacobian;
    MatrixType system;
};

namespace OdeMath
{

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
     * @brief Clamp the temperature stored in the final ODE component.
     */
    template <int ODE_NEQ>
    ARCH_INLINE void enforce_temperature_bounds(double *Y, double T_min, double T_max)
    {
        const int T_INDEX = ODE_NEQ - 1;
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
        // Hairer-Wanner PI exponents scale with the formal method order.
        const double k1 = 0.7 / order_q;
        const double k2 = 0.2 / order_q;

        if (pi_uses_small_error_branch(err_n))
            return dt_n * max_fac;

        double fac = safe * std::pow(err_n, -k1) * std::pow(err_n_1, k2);
        fac = std::max(min_fac, std::min(max_fac, fac));
        return dt_n * fac;
    }

    ARCH_INLINE double burn_cv_floor(double cv)
    {
        return std::max(cv, 1.0e-10);
    }

    ARCH_INLINE double max4(double a, double b, double c, double d)
    {
        return std::max(std::max(a, b), std::max(c, d));
    }

    // Self-consistent NSE projection.
    template <typename NetType, typename EOSType>
    ARCH_INLINE bool integrate_nse_state(double* state, double rho,
                             double dt_target, const EOSType& eos,
                             const BurnConfigView& burn_cfg,
                             double& dt_rec)
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
     * @param T Extrapolation tableau T[k][j][NEQ].
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
