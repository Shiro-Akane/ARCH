/**
 * @file LinearEquilibration.h
 * @brief Equation/unknown unit normalization, independent of linear provider.
 *
 * D A C y = D b, x = C y is equivalent to A x = b. Retain original buffers for
 * backward-error acceptance; normalization is not regularization or clipping.
 */
#pragma once

#include "../../core/ArchPortability.h"
#include <cmath>
#include <limits>

namespace arch::linalg {

ARCH_HOST_DEVICE inline double equilibration_divisor(
    const double* values, int begin, int end, const int* slots = nullptr)
{
    double maximum = 0.0;
    for (int slot = begin; slot < end; ++slot) {
        const double value = values[slots == nullptr ? slot : slots[slot]];
        if (!std::isfinite(value))
            return std::numeric_limits<double>::quiet_NaN();
        const double magnitude = std::abs(value);
        if (magnitude > maximum) maximum = magnitude;
    }
    // A zero row/column stays zero: the provider/residual gate must reject a
    // singular system rather than manufacture a diagonal or a minimum pivot.
    if (maximum == 0.0) return 1.0;
    for (int slot = begin; slot < end; ++slot) {
        const double value = values[slots == nullptr ? slot : slots[slot]];
        if (value != 0.0 && value / maximum == 0.0)
            return 1.0; // Preserve coefficients that would underflow on scaling.
    }
    return maximum;
}

ARCH_HOST_DEVICE inline bool equilibrated_value(
    double value, double divisor, double& scaled)
{
    if (std::isfinite(value) && std::isfinite(divisor) && divisor > 0.0) {
        scaled = value / divisor;
        if (std::isfinite(scaled) && (value == 0.0 || scaled != 0.0)) return true;
    }
    // A caller must propagate this failure before accepting a linear response.
    // The initialized output keeps invalid arithmetic out of a vendor kernel.
    scaled = 0.0;
    return false;
}

} // namespace arch::linalg
