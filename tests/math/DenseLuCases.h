// Host/device witnesses for the shared compact linear provider.
#pragma once
#include "numerics/linalg/DenseWrap.h"
#include <cmath>
#include <limits>

namespace DenseLuCases {

ARCH_INLINE bool mixed_units()
{
    // Two-variable principal subsystem captured from the actual aprox13/Helm
    // BD first-divergence matrix (2026-09-06, macro 6, first burn half).
    // The independent oracle is the analytic 2x2 determinant formula evaluated
    // with 80 decimal digits on the exact binary64 inputs, then rounded once.
    constexpr double original[2][2]{
        {1.000000000244876, -6.401316084438572e-20},
        {-1.4914256518698759, 0.9999999996227632}};
    constexpr double rhs[2]{1.1397961314252351e-11, 0.06696341376067727};
    constexpr double expected[2]{0x1.9107b56b51081p-37, 0x1.12483a84fe7dep-4};
    constexpr double budget = 16.0 * std::numeric_limits<double>::epsilon();
    // Equivalent row-unit changes and opposite RHS signs do not change the
    // equation's conditioning in physical units. Exercise factor reuse too.
    for (int exponent = -20; exponent <= 20; exponent += 20) {
        DenseMatrixData<2> matrix;
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j)
                matrix.data[i][j] = std::ldexp(original[i][j], i == 0 ? exponent : -exponent);
        int pivots[2];
        if (!DenseLUSolver::factorize<2, 2>(matrix, pivots)) return false;
        for (int sign = -1; sign <= 1; sign += 2) {
            double b[2]{sign * std::ldexp(rhs[0], exponent),
                        sign * std::ldexp(rhs[1], -exponent)};
            DenseLUSolver::solve_with_factors<2, 2>(matrix, pivots, b);
            for (int i = 0; i < 2; ++i) {
                if (!std::isfinite(b[i]) || std::abs(b[i] - sign * expected[i])
                    > budget * std::abs(expected[i])) return false;
                double product = 0.0, scale = std::abs(rhs[i]);
                for (int j = 0; j < 2; ++j) {
                    product += original[i][j] * b[j];
                    scale += std::abs(original[i][j] * b[j]);
                }
                if (std::abs(product - sign * rhs[i]) > budget * scale) return false;
            }
        }
    }
    return true;
}

ARCH_INLINE bool failure_controls()
{
    DenseMatrixData<2> zero{};
    int pivots[2];
    if (DenseLUSolver::factorize<2, 2>(zero, pivots)) return false;
    DenseMatrixData<2> singular{{{1.0, 2.0}, {2.0, 4.0}}};
    if (DenseLUSolver::factorize<2, 2>(singular, pivots)) return false;
    DenseMatrixData<2> nonfinite{{{1.0, 0.0}, {0.0, 1.0}}};
    nonfinite.data[0][1] = std::numeric_limits<double>::quiet_NaN();
    if (DenseLUSolver::factorize<2, 2>(nonfinite, pivots)) return false;
    nonfinite.data[0][1] = std::numeric_limits<double>::infinity();
    return !DenseLUSolver::factorize<2, 2>(nonfinite, pivots);
}
}
