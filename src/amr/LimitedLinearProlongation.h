/**
 * @file LimitedLinearProlongation.h
 * @brief Shared conservative scalar reconstruction for 2:1 AMR prolongation.
 */

#pragma once

#include "core/ArchPortability.h"

namespace amr::prolongation_math {

ARCH_HOST_DEVICE inline double minimum(double left, double right)
{
    return left < right ? left : right;
}

ARCH_HOST_DEVICE inline double maximum(double left, double right)
{
    return left > right ? left : right;
}

ARCH_HOST_DEVICE inline double absolute(double value)
{
    return value < 0.0 ? -value : value;
}

ARCH_HOST_DEVICE inline bool finite_number(double value)
{
    constexpr double largest = 0x1.fffffffffffffp+1023;
    return value == value && value >= -largest && value <= largest;
}

ARCH_HOST_DEVICE inline double minmod(double left, double right)
{
    if (left * right <= 0.0) return 0.0;
    return left > 0.0
        ? minimum(left, right) : maximum(left, right);
}

/**
 * Reconstruct one fine-cell value at offsets +/-1/4 of a coarse cell.
 * One common multidimensional limiter keeps all 2^dim children inside the
 * coarse stencil bounds.  Because the limited slopes and limiter are shared
 * by the symmetric children, their arithmetic mean is exactly the parent.
 */
ARCH_HOST_DEVICE inline double limited_linear_value(
    double center, const double lower[3], const double upper[3],
    const double position[3], int dimension)
{
    double slopes[3]{0.0, 0.0, 0.0};
    double stencil_min = center;
    double stencil_max = center;
    double maximum_excursion = 0.0;
    for (int axis = 0; axis < dimension; ++axis) {
        stencil_min = minimum(stencil_min, minimum(lower[axis], upper[axis]));
        stencil_max = maximum(stencil_max, maximum(lower[axis], upper[axis]));
        slopes[axis] = minmod(
            center - lower[axis], upper[axis] - center);
        maximum_excursion += 0.25 * absolute(slopes[axis]);
    }
    double theta = 1.0;
    if (maximum_excursion > 0.0) {
        theta = minimum(
            theta, (stencil_max - center) / maximum_excursion);
        theta = minimum(
            theta, (center - stencil_min) / maximum_excursion);
        theta = maximum(0.0, theta);
    }
    double result = center;
    for (int axis = 0; axis < dimension; ++axis)
        result += theta * position[axis] * slopes[axis];
    return result;
}

} // namespace amr::prolongation_math
