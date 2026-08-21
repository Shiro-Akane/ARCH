/**
 * @file EOS_Utils.h
 * @brief Common thermodynamic and kinematic utilities for EOS solvers.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "eos.h" // Provides FluidVector through the EOS policy surface.

#ifndef EOS_INLINE
#define EOS_INLINE inline
#endif

namespace eos_utils
{
    /** Thermodynamic point reached along a fixed-composition isentrope. */
    struct IsentropicState
    {
        double rho = 0.0;
        double temperature = 0.0;
        double pressure = 0.0;
        double sound_speed = 0.0;
    };

    // Shared kinetic/internal-energy conversions.
    EOS_INLINE double calc_kinetic_energy(double rho, double u, double v, double w)
    {
        return 0.5 * rho * (u * u + v * v + w * w);
    }

    // Extract specific internal energy from a conservative state.
    EOS_INLINE double extract_specific_internal_energy(const FluidVector &U)
    {
        if (U.rho < 1e-12)
            return 0.0;
        double kinetic_density = 0.5 * (U.mom_u * U.mom_u + U.mom_v * U.mom_v + U.mom_w * U.mom_w) / U.rho;
        return (U.eng - kinetic_density) / U.rho;
    }

    // Generic Newton pressure inversion for three- and four-dimensional tables.
    // TEOSView must provide get_pressure_from_rho_e and get_dp_de_rho.
    template <typename TEOSView>
    EOS_INLINE double solve_total_energy(const TEOSView &eos_view,
                                         double rho, double u, double v, double w,
                                         double target_p, const double *Xi)
    {
        // Initialize with the gamma=1.4 ideal-gas estimate. This is a numerical
        // seed only; all accepted iterates use the selected tabular EOS.
        double e_guess = target_p / ((1.4 - 1.0) * rho);

        for (int iter = 0; iter < 20; ++iter)
        {
            double p_guess = eos_view.get_pressure_from_rho_e(rho, e_guess, Xi);
            double dp_de = eos_view.get_dp_de_rho(rho, e_guess, Xi);

            if (std::abs(dp_de) < 1e-12)
                break;

            double delta_e = (target_p - p_guess) / dp_de;

            // Backtrack until the trial specific internal energy remains above
            // the 1e-12 positivity floor.
            while (e_guess + delta_e <= 1e-12)
            {
                delta_e *= 0.5;
            }
            e_guess += delta_e;

            if (std::abs(delta_e) < 1e-6 * e_guess)
                break;
        }

        return rho * e_guess + calc_kinetic_energy(rho, u, v, w);
    }

    /**
     * Move a (rho,T,X) reference state to a requested pressure factor while
     * holding composition and specific entropy fixed.
     *
     * Every EOS policy obtains the same implementation through its required
     * evaluate_state(eos_state_t&) surface.  The thermodynamic path obeys
     *
     *   d ln(T) / d ln(rho) |_s,X = (dP/dT)_rho,X / (rho c_v)
     *
     * and a Newton solve in ln(rho), using Gamma1=rho*c_s^2/P, matches the
     * target pressure.  This is a local-state construction: the accepted
     * solution may not move farther than 0.25 in ln(rho) from the reference.
     */
    template <typename TEOSPolicy>
    IsentropicState get_isentropic_state_at_pressure_factor(
        const TEOSPolicy &eos,
        double reference_rho,
        double reference_temperature,
        const double *mass_fractions,
        double pressure_factor)
    {
        if (!std::isfinite(reference_rho) || reference_rho <= 0.0 ||
            !std::isfinite(reference_temperature) || reference_temperature <= 0.0 ||
            !std::isfinite(pressure_factor) || pressure_factor <= 0.0) {
            throw std::invalid_argument(
                "Invalid reference state or pressure factor for isentropic initialization.");
        }

        const auto evaluate = [&](double rho, double temperature) {
            eos_state_t state{};
            state.rho = rho;
            state.T = temperature;
            state.Xi = mass_fractions;
            eos.evaluate_state(state);
            if (!std::isfinite(state.P) || state.P <= 0.0 ||
                !std::isfinite(state.cv) || state.cv <= 0.0 ||
                !std::isfinite(state.dp_dT) ||
                !std::isfinite(state.sound_speed) || state.sound_speed <= 0.0) {
                throw std::runtime_error(
                    "EOS returned an invalid state while integrating an isentrope.");
            }
            return state;
        };

        const eos_state_t reference = evaluate(reference_rho, reference_temperature);
        const double target_pressure = pressure_factor * reference.P;
        if (!std::isfinite(target_pressure) || target_pressure <= 0.0) {
            throw std::invalid_argument("Isentropic target pressure is not finite and positive.");
        }

        const double log_rho_reference = std::log(reference_rho);
        const double log_temperature_reference = std::log(reference_temperature);
        constexpr double max_log_density_distance = 0.25;

        const auto log_temperature_on_isentrope = [&](double log_rho_target) {
            const double interval = log_rho_target - log_rho_reference;
            if (std::abs(interval) > max_log_density_distance) {
                throw std::runtime_error(
                    "Isentropic pressure solve left its supported local neighborhood.");
            }
            const int steps = std::max(
                8, static_cast<int>(std::ceil(std::abs(interval) / 1.0e-4)));
            const double step = interval / static_cast<double>(steps);
            double log_rho = log_rho_reference;
            double log_temperature = log_temperature_reference;

            const auto derivative = [&](double x, double y) {
                const eos_state_t state = evaluate(std::exp(x), std::exp(y));
                return state.dp_dT / (state.rho * state.cv);
            };
            for (int index = 0; index < steps; ++index) {
                const double k1 = derivative(log_rho, log_temperature);
                const double k2 = derivative(
                    log_rho + 0.5 * step, log_temperature + 0.5 * step * k1);
                const double k3 = derivative(
                    log_rho + 0.5 * step, log_temperature + 0.5 * step * k2);
                const double k4 = derivative(
                    log_rho + step, log_temperature + step * k3);
                log_temperature += step * (k1 + 2.0 * k2 + 2.0 * k3 + k4) / 6.0;
                log_rho += step;
            }
            return log_temperature;
        };

        const double gamma1_reference = reference_rho * reference.sound_speed *
                                        reference.sound_speed / reference.P;
        if (!std::isfinite(gamma1_reference) || gamma1_reference <= 0.0) {
            throw std::runtime_error("EOS returned an invalid reference adiabatic exponent.");
        }

        double log_rho = log_rho_reference + std::log(pressure_factor) / gamma1_reference;
        eos_state_t state{};
        for (int iteration = 0; iteration < 12; ++iteration) {
            const double temperature = std::exp(log_temperature_on_isentrope(log_rho));
            state = evaluate(std::exp(log_rho), temperature);
            const double residual = std::log(state.P / target_pressure);
            if (std::abs(residual) <= 2.0e-13) {
                return {
                    state.rho,
                    state.T,
                    state.P,
                    state.sound_speed,
                };
            }
            const double gamma1 = state.rho * state.sound_speed *
                                  state.sound_speed / state.P;
            if (!std::isfinite(gamma1) || gamma1 <= 0.0) {
                throw std::runtime_error("EOS returned an invalid adiabatic exponent.");
            }
            log_rho -= residual / gamma1;
        }

        throw std::runtime_error("Isentropic pressure initialization did not converge.");
    }
}
