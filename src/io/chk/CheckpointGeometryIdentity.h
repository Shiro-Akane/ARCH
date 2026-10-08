#pragma once
#include <string>
#include <string_view>
#include <stdexcept>
#include <vector>
#include <cmath>
namespace io {
// Independent of checkpoint layout/config versions. Empty chart means legacy,
// never authoritative RZ. Version 2 RZ retains axes/basis/full-ring geometry
// and identifies the unique mom_w=m_phi=J/W state. Revision 1 cannot prove
// this representation and must never be silently migrated. For cylindrical2D
// the revision-1/empty "existing" identity likewise cannot certify the
// canonical RZ state, so it is refused before any state mutation.
struct CheckpointGeometryIdentity {
    int revision = 0;
    std::string chart;
};

/** Native geometry needed to interpret W and native storage coordinates.
 * Empty vectors mean unknown, never implicit/default RZ geometry.
 */
struct CheckpointNativeDomainIdentity {
    std::vector<double> bounds; // r_min,r_max,z_min,z_max, in cm
    std::vector<int> root_blocks; // nr,nz
    std::vector<int> cell_shape; // BLOCK_NX,BLOCK_NY; not just their product
};
inline void require_valid_rz_checkpoint_domain(const CheckpointNativeDomainIdentity& id) {
    if(id.bounds.size()!=4||id.root_blocks.size()!=2||id.cell_shape.size()!=2)
        throw std::runtime_error("RZ checkpoint native domain identity is missing or malformed");
    for(double v:id.bounds)if(!std::isfinite(v))
        throw std::runtime_error("RZ checkpoint native domain requires finite bounds");
    if(id.bounds[0]<0.||id.bounds[1]<=id.bounds[0]||id.bounds[3]<=id.bounds[2]
        ||id.root_blocks[0]<=0||id.root_blocks[1]<=0||id.cell_shape[0]<=0||id.cell_shape[1]<=0)
        throw std::runtime_error("RZ checkpoint native domain identity is invalid");
}
inline void require_rz_checkpoint_domain_compatible(
    const CheckpointNativeDomainIdentity& saved,const CheckpointNativeDomainIdentity& expected) {
    require_valid_rz_checkpoint_domain(saved);require_valid_rz_checkpoint_domain(expected);
    if(saved.bounds!=expected.bounds||saved.root_blocks!=expected.root_blocks||saved.cell_shape!=expected.cell_shape)
        throw std::runtime_error("RZ checkpoint native domain/measure identity mismatch");
}

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
    // Retired computational cylindrical2D polar (r,phi) is now canonical
    // axisymmetric RZ: only the RZ identity may certify it, so an Existing
    // (revision 1) or legacy (empty) saved identity is refused here. Existing
    // stays valid for every other supported geometry.
    const bool cylindrical2d = dim == 2 && geometry == "cylindrical";
    const auto valid = [cylindrical2d](const CheckpointGeometryIdentity& id) {
        if (id.revision == 1 && id.chart == "existing")
            return !cylindrical2d;
        return id.revision == rz_checkpoint_revision && id.chart == "axisymmetric-rz"
            && cylindrical2d;
    };
    if (!valid(expected))
        throw std::runtime_error("Unsupported expected checkpoint geometry semantics");
    if (saved.revision == 0 && saved.chart.empty()) {
        if (expected.chart == "existing" && !cylindrical2d) return;
        throw std::runtime_error("Legacy checkpoint has no authoritative RZ geometry identity");
    }
    if (saved.chart == "axisymmetric-rz" && saved.revision == 1)
        throw std::runtime_error("RZ checkpoint revision 1 does not identify m_phi=J/W; explicit rejection required");
    if (cylindrical2d && saved.revision == 1 && saved.chart == "existing")
        throw std::runtime_error("Existing checkpoint revision 1 cannot certify cylindrical2D; RZ identity required");
    if (!valid(saved))
        throw std::runtime_error("Unsupported checkpoint geometry semantics");
    if (saved.revision != expected.revision || saved.chart != expected.chart)
        throw std::runtime_error("Checkpoint geometry semantics mismatch");
}
} // namespace io
