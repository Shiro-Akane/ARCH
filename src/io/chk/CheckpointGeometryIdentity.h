#pragma once
#include <string>
#include <string_view>
#include <stdexcept>
namespace io {
// Independent of checkpoint layout/config versions. Empty chart means legacy,
// never authoritative RZ. Version 1 RZ fixes axes (r,z), momenta (r,z,phi),
// and full-ring volume normalization.
struct CheckpointGeometryIdentity {
    int revision = 0;
    std::string chart;
};

inline void require_checkpoint_geometry_compatible(
    int dim, std::string_view geometry,
    const CheckpointGeometryIdentity& saved,
    const CheckpointGeometryIdentity& expected = {1, "existing"})
{
    const auto valid = [dim, geometry](const CheckpointGeometryIdentity& id) {
        return id.revision == 1 &&
            (id.chart == "existing" ||
             (id.chart == "axisymmetric-rz" && dim == 2 && geometry == "cylindrical"));
    };
    if (!valid(expected))
        throw std::runtime_error("Unsupported expected checkpoint geometry semantics");
    if (saved.revision == 0 && saved.chart.empty()) {
        if (expected.chart == "existing") return;
        throw std::runtime_error("Legacy checkpoint has no authoritative RZ geometry identity");
    }
    if (!valid(saved))
        throw std::runtime_error("Unsupported checkpoint geometry semantics");
    if (saved.revision != expected.revision || saved.chart != expected.chart)
        throw std::runtime_error("Checkpoint geometry semantics mismatch");
}
} // namespace io
