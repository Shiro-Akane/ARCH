/**
 * @file CheckpointGeometryIdentity.h
 * @brief Native chart identity, independent of CHK layout and build identity.
 * Workflow:
 * 1. Select the current chart from the requested geometry and dimension.
 * 2. Require exact stored/current semantics before reading or mutating state.
 * 3. Permit legacy empty identity only for the eight retained charts.
 *
 * The retired polar and axisymmetric RZ artifacts cannot be reinterpreted as
 * the new three-momentum PWC chart. No source/build/profile gate is added to CHK.
 */
#pragma once
#include <string>
#include <string_view>
#include <stdexcept>
#include <vector>
#include <cmath>
namespace io {
struct CheckpointGeometryIdentity {
    int revision = 0;
    std::string chart;
};

inline CheckpointGeometryIdentity checkpoint_geometry_identity(int dim, std::string_view geometry)
{
    return {1, dim == 2 && geometry == "cylindrical" ? "cylindrical-rz-pwc" : "existing"};
}

inline void require_checkpoint_geometry_compatible(
    int dim, std::string_view geometry,
    const CheckpointGeometryIdentity& saved,
    const CheckpointGeometryIdentity& expected)
{
    const auto current = checkpoint_geometry_identity(dim, geometry);
    if (expected.revision != current.revision || expected.chart != current.chart)
        throw std::runtime_error("Unsupported expected checkpoint geometry semantics");
    if (saved.revision == 0 && saved.chart.empty() && current.chart == "existing")
        return;
    if (saved.revision != current.revision || saved.chart != current.chart)
        throw std::runtime_error("Checkpoint geometry semantics mismatch: expected " + current.chart);
}

inline void require_checkpoint_geometry_compatible(
    int dim, std::string_view geometry, const CheckpointGeometryIdentity& saved)
{
    require_checkpoint_geometry_compatible(dim, geometry, saved,
                                          checkpoint_geometry_identity(dim, geometry));
}
} // namespace io
