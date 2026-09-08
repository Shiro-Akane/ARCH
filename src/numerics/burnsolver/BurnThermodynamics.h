/** Shared fixed-density thermodynamic closure for temperature-based burning.
 * EOS policies own energy/cv and optional analytic derivatives. This adapter
 * contracts them with reaction rates; CPU and CUDA call the same mathematics.
 */
#pragma once

#include "core/ArchPortability.h"
#include "core/CompensatedSum.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace OdeMath {

ARCH_INLINE double burn_cv_floor(double cv)
{
    return std::max(cv, 1.0e-10);
}

// Derivative at the origin of the quadratic through actual floating-point
// nodes. Keep independent composition coordinates (no renormalization).
template <int Equations, int Columns = Equations, class Evaluate>
ARCH_HEAVY_INLINE void thermodynamic_gradient(
    const double* state, double value, Evaluate evaluate, double* gradient,
    double relative_step = std::cbrt(std::numeric_limits<double>::epsilon()))
{
    double sample[Equations];
    for (int i = 0; i < Equations; ++i) sample[i] = state[i];
    for (int column = 0; column < Columns; ++column) {
        const double coordinate = state[column];
        const double step = relative_step * std::max(std::abs(coordinate), 1.0);
        double first = -step, second = step;
        if (coordinate - step <= 0.0) { first = step; second = 2.0 * step; }
        else if (column < Equations - 1 && coordinate + step > 1.0) {
            first = -step; second = -2.0 * step;
        }
        sample[column] = coordinate + first;
        first = sample[column] - coordinate;
        const double first_value = evaluate(sample);
        sample[column] = coordinate + second;
        second = sample[column] - coordinate;
        const double second_value = evaluate(sample);
        sample[column] = coordinate;
        gradient[column] = (second * ((first_value - value) / first)
            - first * ((second_value - value) / second)) / (second - first);
    }
}

template <int Equations, class EOS>
ARCH_HEAVY_INLINE void burn_cv_gradient(
    const double* state, double rho, const EOS& eos, double cv, double* gradient)
{
    if (cv <= burn_cv_floor(0.0)) {
        for (int i = 0; i < Equations; ++i) gradient[i] = 0.0;
    } else if constexpr (requires {
        eos.template get_cv_gradient<Equations>(rho, state[Equations - 1], state, gradient);
    }) {
        eos.template get_cv_gradient<Equations>(rho, state[Equations - 1], state, gradient);
    } else {
        thermodynamic_gradient<Equations>(state, cv, [&](const double* sample) {
            return burn_cv_floor(eos.get_cv(rho, sample[Equations - 1], sample));
        }, gradient);
    }
}

template <int Equations, class EOS>
ARCH_HEAVY_INLINE void burn_energy_composition_gradient(
    const double* state, double rho, const EOS& eos, double* gradient)
{
    if constexpr (requires {
        eos.template get_energy_composition_gradient<Equations>(rho, state[Equations - 1], state, gradient);
    }) {
        eos.template get_energy_composition_gradient<Equations>(rho, state[Equations - 1], state, gradient);
    } else {
        const auto energy = [&](const double* sample) {
            return eos.get_eint_from_T(rho, sample[Equations - 1], sample);
        };
        thermodynamic_gradient<Equations, Equations - 1>(state, energy(state), energy, gradient);
    }
}

template <int Species>
ARCH_INLINE double composition_energy_rate(const double* gradient, const double* flow)
{
    arch::math::CompensatedSum sum;
    for (int i = 0; i < Species; ++i) sum.add_product(gradient[i], flow[i]);
    return sum.value();
}

template <int Equations, class EOS>
ARCH_HEAVY_INLINE void burn_energy_composition_hessian_action(
    const double* state, double rho, const EOS& eos, const double* flow,
    const double* energy_gradient, double* action)
{
    if constexpr (requires {
        eos.template get_energy_composition_hessian_action<Equations>(
            rho, state[Equations - 1], state, flow, action);
    }) {
        eos.template get_energy_composition_hessian_action<Equations>(
            rho, state[Equations - 1], state, flow, action);
    } else {
        const auto contraction = [&](const double* sample) {
            double gradient[Equations - 1];
            burn_energy_composition_gradient<Equations>(sample, rho, eos, gradient);
            return composition_energy_rate<Equations - 1>(gradient, flow);
        };
        thermodynamic_gradient<Equations>(state,
            composition_energy_rate<Equations - 1>(energy_gradient, flow), contraction, action,
            std::pow(std::numeric_limits<double>::epsilon(), 1.0 / 6.0));
    }
}

} // namespace OdeMath
