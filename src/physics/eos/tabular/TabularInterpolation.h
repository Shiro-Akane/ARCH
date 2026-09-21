/**
 * @file TabularInterpolation.h
 * @brief Shared coordinate location and field decoding for tabular EOS views.
 *
 * Explicit nodes retain native nonuniform grids. Encoded fields are interpolated
 * before decoding; the same chain rule supplies composition/thermal derivatives.
 */
#pragma once

#include "core/ArchPortability.h"
#include <algorithm>
#include <cmath>

namespace tabular_eos {

struct AxisCell {
    int lower;
    double fraction;
    double width;
};

ARCH_INLINE AxisCell locate_axis(double value, int count, double minimum,
                                double spacing, const double* nodes)
{
    int lower;
    if (nodes) {
        int left = 0, right = count - 1;
        while (right - left > 1) {
            const int middle = left + (right - left) / 2;
            if (value < nodes[middle]) right = middle;
            else left = middle;
        }
        lower = left;
        spacing = nodes[lower + 1] - nodes[lower];
        minimum = nodes[lower];
    } else {
        lower = static_cast<int>((value - minimum) / spacing);
        lower = std::max(0, std::min(lower, count - 2));
        minimum += lower * spacing;
    }
    return {lower, (value - minimum) / spacing, spacing};
}

struct FieldTransform {
    bool logarithmic = false;
    double offset = 0.0;

    ARCH_INLINE double decode(double value, double* composition = nullptr,
                              double* temperature = nullptr,
                              double* mixed = nullptr,
                              double* curvature = nullptr) const
    {
        if (!logarithmic) return value + offset;
        const double decoded = std::pow(10.0, value);
        const double factor = std::log(10.0) * decoded;
        const double dc = composition ? *composition : 0.0;
        const double dt = temperature ? *temperature : 0.0;
        if (curvature) *curvature = factor * std::log(10.0) * dc * dc;
        if (mixed) *mixed = factor * (*mixed + std::log(10.0) * dc * dt);
        if (composition) *composition *= factor;
        if (temperature) *temperature *= factor;
        return decoded + offset;
    }
};

} // namespace tabular_eos
