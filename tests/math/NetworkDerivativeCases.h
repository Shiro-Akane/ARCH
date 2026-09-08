#pragma once
#include "numerics/burnsolver/NetworkDerivative.h"

namespace arch::test {
struct DerivativeRhs {
    double scale;
    bool oscillatory;
    ARCH_HOST_DEVICE void operator()(const double* state, double,
                                    double* rhs, double& energy) const {
        const double x = state[2] / scale;
        rhs[0] = oscillatory ? std::sin(x) : x * x;
        rhs[1] = x * x * x - 2.0 * x;
        // An independent energy term tests the complete callback, not an
        // assumed reconstruction from abundance derivatives.
        energy = x*x*x*x + x*x + (oscillatory ? std::sin(x) : 0.0);
    }
};

ARCH_INLINE bool network_derivative_contract()
{
    for (int sample = 0; sample < 4; ++sample) {
        const double scale = sample < 2 ? 1.0 : (sample == 2 ? 1.e5 : 3.e9);
        const double temperature = sample == 1 ? 1.25 : scale;
        const bool oscillatory = sample >= 2;
        const double x = temperature / scale;
        double state[3]{0.25, 0.75, temperature}, derivative[2], energy = 0.0;
        burnmath::temperature_derivative<3>(state, 1.0, derivative, energy,
                                            DerivativeRhs{scale, oscillatory});
        const double expected[]{(oscillatory ? std::cos(x) : 2.0*x) / scale,
                                (3.0*x*x - 2.0) / scale,
                                (4.0*x*x*x + 2.0*x + (oscillatory ? std::cos(x) : 0.0)) / scale};
        const double actual[]{derivative[0], derivative[1], energy};
        for (int i = 0; i < 3; ++i)
            if (!std::isfinite(actual[i]) || std::abs(actual[i] - expected[i])
                > 2.e-11 * std::max(1.0 / scale, std::abs(expected[i]))) return false;
        if (state[0] != .25 || state[1] != .75 || state[2] != temperature) return false;
    }
    // Independent analytic convergence oracle. Doubling resolution must
    // reduce the centered sine-derivative truncation error by about 2^4.
    double state[3]{.25, .75, 1.e5}, derivative[2], energy = 0.0;
    double errors[2];
    for (int level = 0; level < 2; ++level) {
        burnmath::temperature_derivative<3>(state, 1.0, derivative, energy,
            DerivativeRhs{1.e5, true}, level == 0 ? 5000.0 : 2500.0);
        errors[level] = std::abs(derivative[0] - std::cos(1.0) / 1.e5);
    }
    if (!(errors[0] > 14.0 * errors[1] && errors[0] < 18.0 * errors[1])) return false;
    const double invalid_temperatures[]{0.5, std::numeric_limits<double>::infinity(),
                                        std::numeric_limits<double>::quiet_NaN()};
    for (double invalid : invalid_temperatures) {
        state[2] = invalid;
        burnmath::temperature_derivative<3>(state, 1.0, derivative, energy,
                                            DerivativeRhs{1.0, false});
        if (!std::isnan(derivative[0]) || !std::isnan(derivative[1]) || !std::isnan(energy)) return false;
    }
    state[2] = 1.e5;
    const double invalid_steps[]{-1.0, 1.e-300, std::numeric_limits<double>::infinity()};
    for (double invalid : invalid_steps) {
        burnmath::temperature_derivative<3>(state, 1.0, derivative, energy,
                                            DerivativeRhs{1.e5, false}, invalid);
        if (!std::isnan(derivative[0]) || !std::isnan(energy)) return false;
    }
    return true;
}
}
