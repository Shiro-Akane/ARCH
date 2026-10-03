/**
 * @file RzMetricCases.h
 * @brief Independent full-rotation finite-volume references for shared math.
 *
 * Expected values integrate the full source measure with 70/100-digit Decimal,
 * then round once to binary64. Endpoints are the exact binary64 input values.
 * Reproduce with validation/amr/rz_metric_reference.py; no production formulas
 * are called by that oracle. This does not qualify hydro/gravity/AMR evolution.
 */
#pragma once
#include "grid/GridMetrics.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace RzMetricCases {
struct MeasureCase {
    double lower, upper, dz;
    double volume, lower_radial_area, upper_radial_area, axial_area;
};
inline constexpr MeasureCase cases[]{
    // BEGIN INDEPENDENT RZ MEASURE DATA
    {0x0.0p+0, 0x1.0000000000000p-2, 0x1.0000000000000p-3, 0x1.921fb54442d18p-6, 0x0.0p+0, 0x1.921fb54442d18p-3, 0x1.921fb54442d18p-3},
    {0x0.0p+0, 0x1.0000000000000p+0, 0x1.0000000000000p+0, 0x1.921fb54442d18p+1, 0x0.0p+0, 0x1.921fb54442d18p+2, 0x1.921fb54442d18p+1},
    {0x1.0000000000000p+0, 0x1.0000000000000p+1, 0x1.0000000000000p-1, 0x1.2d97c7f3321d2p+2, 0x1.921fb54442d18p+1, 0x1.921fb54442d18p+2, 0x1.2d97c7f3321d2p+3},
    {0x1.e848000000000p+19, 0x1.e848200000000p+19, 0x1.0624dd2f1a9fcp-10, 0x1.88b303e2dc1bep+12, 0x1.88b2f704a940ap+12, 0x1.88b310c10ef73p+12, 0x1.7f7ed1cb8af34p+22},
    {0x1.2a05f20000000p+33, 0x1.2a05f20080000p+33, 0x1.0000000000000p+0, 0x1.d4223fc25dffap+35, 0x1.d4223fc1f977cp+35, 0x1.d4223fc2c2879p+35, 0x1.d4223fc25dffap+35},
    {0x1.b7cdfd9d7bdbbp-34, 0x1.b7cf1dd875ca2p-34, 0x1.5798ee2308c3ap-27, 0x1.04fe9ede0437bp-107, 0x1.cf9e03ca01d95p-58, 0x1.cf9f33a012e9ep-58, 0x1.84e98b0a8c106p-81},
    {0x1.0000000000000p+0, 0x1.000000006df38p+0, 0x1.0000000000000p+0, 0x1.596bfaae3fef5p-31, 0x1.921fb54442d18p+2, 0x1.921fb544ef878p+2, 0x1.596bfaae3fef5p-31},
    {0x1.38d352e5096afp+498, 0x1.38d352e58fc67p+498, 0x1.bff2ee48e0530p-333, 0x1.c344001dcc759p+633, 0x1.ade9b959b7cd0p+168, 0x1.ade9b95a70726p+168, 0x1.01e53ceaa01d1p+966},
    {0x1.bff2ee48e0530p-333, 0x1.bff2ee48e0530p-332, 0x1.bff2ee48e0530p-333, 0x1.93f3009f121a0p-994, 0x1.33ce50895f104p-662, 0x1.33ce50895f104p-661, 0x1.cdb578ce0e987p-662},
    {0x1.a2fe76a3f9475p-499, 0x1.a2fe76a3f9475p-498, 0x1.249ad2594c37dp+332, 0x1.cdb578ce0e987p-662, 0x1.782188df45c21p-164, 0x1.782188df45c21p-163, 0x1.93f3009f1219fp-994},
    // END INDEPENDENT RZ MEASURE DATA
};

ARCH_HOST_DEVICE inline double relative_error(const MeasureCase& c)
{
    const double actual[]{
        GridMetrics::Rz::CellVolume(c.lower,c.upper,c.dz),
        GridMetrics::Rz::RadialFaceArea(c.lower,c.dz),
        GridMetrics::Rz::RadialFaceArea(c.upper,c.dz),
        GridMetrics::Rz::AxialFaceArea(c.lower,c.upper)};
    const double expected[]{c.volume,c.lower_radial_area,c.upper_radial_area,c.axial_area};
    double error=0.;
    for (unsigned i=0;i<4;++i) {
        if (!std::isfinite(actual[i])) return std::numeric_limits<double>::infinity();
        if (expected[i]==0.) {
            if (actual[i]!=0. || std::signbit(actual[i]))
                return std::numeric_limits<double>::infinity();
        } else {
            if (actual[i]<=0.) return std::numeric_limits<double>::infinity();
            error=std::max(error,std::abs((actual[i]-expected[i])/expected[i]));
        }
    }
    return error;
}
inline double conditioning_error()
{
    double error=0.;
    for (const auto& c:cases) error=std::max(error,relative_error(c));
    return error;
}
} // namespace RzMetricCases
