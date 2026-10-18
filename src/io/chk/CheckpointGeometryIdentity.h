#pragma once
#include <string>
#include <string_view>
#include <stdexcept>
#include <vector>
#include <cmath>
namespace io {
// Independent of checkpoint layout/config versions. Empty chart means legacy,
// never authoritative geometry; the retained charts share the revision-1
// "existing" identity. Retired computational cylindrical2D polar has no
// remaining source representation, so it is refused before any state
// mutation instead of being migrated to another chart.
struct CheckpointGeometryIdentity {
    int revision = 0;
    std::string chart;
};

inline void require_checkpoint_geometry_compatible(
    int dim, std::string_view geometry,
    const CheckpointGeometryIdentity& saved,
    const CheckpointGeometryIdentity& expected = {1, "existing"})
{
    // Retired computational cylindrical2D polar (r,phi) has no authoritative
    // source identity any more; it is refused rather than implicitly mapped.
    if (dim == 2 && geometry == "cylindrical")
        throw std::runtime_error("Cylindrical2D checkpoints are no longer supported");
    const auto valid = [](const CheckpointGeometryIdentity& id) {
        return id.revision == 1 && id.chart == "existing";
    };
    if (!valid(expected))
        throw std::runtime_error("Unsupported expected checkpoint geometry semantics");
    if (saved.revision == 0 && saved.chart.empty()) {
        if (expected.chart == "existing") return;
        throw std::runtime_error("Legacy checkpoint has no authoritative geometry identity");
    }
    if (!valid(saved))
        throw std::runtime_error("Unsupported checkpoint geometry semantics");
    if (saved.revision != expected.revision || saved.chart != expected.chart)
        throw std::runtime_error("Checkpoint geometry semantics mismatch");
}
} // namespace io
