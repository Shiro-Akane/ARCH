#pragma once
#include <string>
#include <string_view>
#include <stdexcept>
namespace io {
// Independent of checkpoint layout/config versions. Empty chart means legacy,
// never authoritative RZ. Version 2 RZ retains axes/basis/full-ring geometry
// and identifies the unique mom_w=m_phi=J/W state. Revision 1 cannot prove
// this representation and must never be silently migrated.
struct CheckpointGeometryIdentity {
    int revision = 0;
    std::string chart;
};

inline constexpr int rz_checkpoint_revision = 2;
inline constexpr std::string_view rz_checkpoint_state_semantics =
    "rz-m-phi-j-over-w-v1";
inline CheckpointGeometryIdentity current_rz_checkpoint_geometry() {
    return {rz_checkpoint_revision,"axisymmetric-rz"};
}
inline void require_checkpoint_geometry_compatible(
    int dim, std::string_view geometry,
    const CheckpointGeometryIdentity& saved,
    const CheckpointGeometryIdentity& expected = {1, "existing"})
{
    const auto valid = [dim, geometry](const CheckpointGeometryIdentity& id) {
        return (id.revision == 1 && id.chart == "existing") ||
            (id.revision == rz_checkpoint_revision && id.chart == "axisymmetric-rz"
                && dim == 2 && geometry == "cylindrical");
    };
    if (!valid(expected))
        throw std::runtime_error("Unsupported expected checkpoint geometry semantics");
    if (saved.revision == 0 && saved.chart.empty()) {
        if (expected.chart == "existing") return;
        throw std::runtime_error("Legacy checkpoint has no authoritative RZ geometry identity");
    }
    if (saved.chart == "axisymmetric-rz" && saved.revision == 1)
        throw std::runtime_error("RZ checkpoint revision 1 does not identify m_phi=J/W; explicit rejection required");
    if (!valid(saved))
        throw std::runtime_error("Unsupported checkpoint geometry semantics");
    if (saved.revision != expected.revision || saved.chart != expected.chart)
        throw std::runtime_error("Checkpoint geometry semantics mismatch");
}
} // namespace io
