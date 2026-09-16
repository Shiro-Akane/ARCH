#pragma once

#include "core/CompensatedSum.h"
#include <cmath>
#include <limits>

namespace arch::linalg {

enum class SparseResidualState : int { Accurate = 0, Correctable = 1, Unrefinable = 2 };

struct SparseResidualRow {
    double correction_rhs = 0.0;
    SparseResidualState state = SparseResidualState::Unrefinable;
};

// Original-system iterative-refinement RHS, shared scalar Host/Device math.
// The acceptance expression deliberately matches CsrMatrixView's existing
// componentwise gate; compensated products improve only the correction RHS.
// This does not relax the gate or replace the caller's final residual check.
ARCH_HOST_DEVICE inline SparseResidualRow sparse_residual_row(
    int extent, int begin, int end, const int* columns, const double* values,
    const double* solution, double rhs)
{
    SparseResidualRow result;
    if (extent <= 0 || begin < 0 || end < begin || !columns || !values || !solution
        || !std::isfinite(rhs)) return result;
    double product = 0.0, scale = std::abs(rhs);
    math::CompensatedSum residual;
    residual.add(rhs);
    for (int slot = begin; slot < end; ++slot) {
        if (columns[slot] < 0 || columns[slot] >= extent) return result;
        const double a = values[slot], x = solution[columns[slot]];
        if (!std::isfinite(a) || !std::isfinite(x)) return result;
        const double term = a * x;
        if (!std::isfinite(term)) return result;
        product += term;
        scale += std::abs(term);
        residual.add_product(-a, x);
    }
    result.correction_rhs = residual.value();
    if (!std::isfinite(product) || !std::isfinite(scale)
        || !std::isfinite(result.correction_rhs)) return result;
    const double allowance = 64.0 * (extent + 1.0) * std::numeric_limits<double>::epsilon();
    result.state = std::abs(product - rhs) > allowance * scale
        ? SparseResidualState::Correctable : SparseResidualState::Accurate;
    return result;
}

} // namespace arch::linalg
