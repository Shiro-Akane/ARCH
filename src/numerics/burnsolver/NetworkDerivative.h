/** Temperature differences of a complete network RHS, including screening and
 * nonconservative energy terms. One policy is compiled by Host and CUDA.
 * This is a numerical fallback for generated networks, not a replacement for
 * the built-in networks' analytic/AD temperature derivatives.
 */
#pragma once
#include "core/ArchPortability.h"
#include "core/CompensatedSum.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace arch::burnmath {

// A fourth-order stencil balances O(h^4) truncation and O(epsilon/h)
// subtraction error at a relative step proportional to epsilon^(1/5).
ARCH_INLINE double temperature_difference_step(double temperature)
{
    return std::max(std::abs(temperature)
        * std::pow(std::numeric_limits<double>::epsilon(), 1.0 / 5.0), 1.0);
}

template<int Equations, class CompleteRhs>
ARCH_HEAVY_INLINE void temperature_derivative(
    const double* state, double rho, double* derivative, double& energy_derivative,
    const CompleteRhs& evaluate, double step = 0.0)
{
    constexpr int species = Equations - 1;
    constexpr double minimum_temperature = 1.0; // inherited generated Kelvin domain
    const double temperature = state[species];
    // A zero step selects the precision-based default. Explicit positive steps
    // support independent refinement tests; negative/nonfinite steps are errors.
    const double h = step == 0.0 ? temperature_difference_step(temperature) : step;
    if (!std::isfinite(temperature) || temperature < minimum_temperature
        || !std::isfinite(h) || h <= 0.0
        || !std::isfinite(temperature + 4.0 * h)
        || !(temperature + h > temperature)) {
        for (int i = 0; i < species; ++i)
            derivative[i] = std::numeric_limits<double>::quiet_NaN();
        energy_derivative = std::numeric_limits<double>::quiet_NaN();
        return;
    }
    double upper[Equations], lower[Equations];
    double fr[species], fl[species];
    for (int i = 0; i < Equations; ++i) upper[i] = lower[i] = state[i];
    if (temperature - 2.0 * h >= minimum_temperature) {
        for (int radius = 1; radius <= 2; ++radius) {
            upper[species] = temperature + radius * h;
            lower[species] = temperature - radius * h;
            double er = 0.0, el = 0.0;
            evaluate(upper, rho, fr, er);
            evaluate(lower, rho, fl, el);
            const double inverse = 1.0 / (upper[species] - lower[species]);
            for (int i = 0; i < species; ++i) {
                const double estimate = (fr[i] - fl[i]) * inverse;
                derivative[i] = radius == 1 ? estimate
                    : (4.0 * derivative[i] - estimate) / 3.0;
            }
            const double estimate = (er - el) * inverse;
            energy_derivative = radius == 1 ? estimate
                : (4.0 * energy_derivative - estimate) / 3.0;
        }
    } else {
        // Fourth-order forward derivative at the lower domain boundary. Never
        // clamp a central sample and then apply symmetric-stencil weights.
        constexpr double weights[5]{-25.0, 48.0, -36.0, 16.0, -3.0};
        arch::math::CompensatedSum sums[species], energy;
        for (int point = 0; point < 5; ++point) {
            upper[species] = temperature + point * h;
            double enuc = 0.0;
            evaluate(upper, rho, fr, enuc);
            for (int i = 0; i < species; ++i) sums[i].add(weights[point] * fr[i]);
            energy.add(weights[point] * enuc);
        }
        for (int i = 0; i < species; ++i) derivative[i] = sums[i].value() / (12.0 * h);
        energy_derivative = energy.value() / (12.0 * h);
    }
}
} // namespace arch::burnmath
