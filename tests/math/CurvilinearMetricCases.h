// Independent finite-volume witnesses, consumed by Host and actual CUDA tests.
#pragma once

#include "grid/GridMetrics.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace CurvilinearMetricCases {

struct MeasureCase {
    double radius, radial_width, theta, angular_width;
    double shell, annulus, angle;
};

// Integral reference DATA from 70/90-digit mpmath, rounded once to binary64.
// Endpoints follow the view's binary64 additions, not decimal idealizations.
// Reproduce/check with validation/amr/geometry_reference.py. The oracle
// integrates powers/cosines; it never calls GridMetrics or its identities.
inline constexpr MeasureCase cases[]{
    // BEGIN INDEPENDENT MEASURE DATA
    {0., 1., 0., 0x1.921fb54442d18p+1,
     0x1.5555555555555p-2, 0x1.0000000000000p-1, 0x1.0000000000000p+1},
    {1., 1., 0., 1.e-7,
     0x1.2aaaaaaaaaaabp+1, 0x1.8000000000000p+0, 0x1.6849b86a12b95p-48},
    {1.e6, 1., 1.e-6, 1.e-7,
     0x1.d1a968a480aabp+39, 0x1.e848100000000p+19, 0x1.d8e0c20b38348p-44},
    {1.e10, 1., .7, 1.e-7,
     0x1.5af1d78bedc70p+66, 0x1.2a05f20040000p+33, 0x1.14b07ceb05ce9p-24},
    {1.e10, 1.e3, 0x1.921fb54442d18p+0, 1.e-7,
     0x1.52d02eb683d7ap+76, 0x1.2309cf4824000p+43, 0x1.ad7f29afffff3p-24},
    {1.e-10, 1.e-15, 0x1.921fb46d833cbp+1, 1.e-7,
     0x1.a95b72421f0d2p-117, 0x1.ef2db19ffe96ap-84, 0x1.6849b86517db2p-48},
    {1., 1.e-10, 0x1.921c6e67e56dfp+1, 1.e-7,
     0x1.b7ce0000bce51p-34, 0x1.b7ce00005e728p-34, 0x1.5faaf4cd14979p-37},
    {1.e6, 1.e3, 0x1.921face0c7012p+1, 1.e-7,
     0x1.c733c6a4e9aabp+49, 0x1.dd13590000000p+29, 0x1.abd78af4d02abp-44},
    {1., 1., 0x1.921fb5444244cp+1, 1.e-12,
     0x1.2aaaaaaaaaaabp+1, 0x1.8000000000000p+0, 0x1.359da833012aep-81},
    {1., 1., 0., 1.e-12,
     0x1.2aaaaaaaaaaabp+1, 0x1.8000000000000p+0, 0x1.357c299a88ea7p-81},
    {1., 1., 0., 0x1.921fb54442d18p+0,
     0x1.2aaaaaaaaaaabp+1, 0x1.8000000000000p+0, 0x1.fffffffffffffp-1},
    {1., 1., 0x1.921fb54442d18p+0, 0x1.921fb54442d18p+0,
     0x1.2aaaaaaaaaaabp+1, 0x1.8000000000000p+0, 0x1.0000000000000p+0},
    // END INDEPENDENT MEASURE DATA
};

ARCH_HOST_DEVICE inline double conditioning_error(const MeasureCase& sample)
{
    double maximum = 0.0;
    GridMetrics::GeometryView grid{};
    grid.geometry = GridMetrics::Geometry::Spherical;
    grid.dim = 3;
    grid.x1_min = sample.radius;
    grid.dx1 = sample.radial_width;
    grid.x2_min = sample.theta;
    grid.dx2 = sample.angular_width;
    grid.dx3 = 0.25;
    const double left = sample.radius;
    const double right = left + sample.radial_width;
    const double actual[]{
        GridMetrics::radial_shell_volume(left, right),
        GridMetrics::cylindrical_annulus_volume(left, right),
        GridMetrics::CellVolume(grid, 0, 0, 0),
        GridMetrics::FaceArea(grid, 0, 0, 0, 0, true),
        GridMetrics::InverseRadiusVolumeAverage(grid, 0)};
    const double expected[]{sample.shell, sample.annulus,
        sample.shell * sample.angle * grid.dx3,
        right * right * sample.angle * grid.dx3,
        sample.annulus / sample.shell};
    for (unsigned index = 0; index < sizeof(actual) / sizeof(actual[0]); ++index) {
        if (!std::isfinite(actual[index]) || actual[index] <= 0.0)
            return std::numeric_limits<double>::infinity();
        // Relative even for tiny cells: an absolute max(1, expected)
        // budget would silently accept a zero volume at either pole.
        maximum = std::max(maximum,
            std::abs((actual[index] - expected[index]) / expected[index]));
    }
    return maximum;
}

inline double conditioning_error()
{
    double maximum = 0.0;
    for (const auto& sample : cases)
        maximum = std::max(maximum, conditioning_error(sample));
    return maximum;
}

} // namespace CurvilinearMetricCases
