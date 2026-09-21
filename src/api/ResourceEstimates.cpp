#include "ResourceEstimates.h"
#include "../amr/AmrDefines.h"
#include "LogCapture.h"
#include "Response.h"
#include "../core/FileFingerprint.h"
#include "../core/RuntimeParams.h"
#include <limits>

namespace arch::api {
using detail::Json;
std::int64_t PaddedCells(int dimension) {
    return std::int64_t(amr::PAD_NX) * (dimension >= 2 ? amr::BLOCK_NY + 2*amr::MAX_NG : 1)
        * (dimension == 3 ? amr::BLOCK_NZ + 2*amr::MAX_NG : 1);
}
namespace {
using Count = std::optional<std::int64_t>;
Count multiply(Count value, std::int64_t factor) {
    if (!value || factor < 0 || (factor && *value > std::numeric_limits<std::int64_t>::max()/factor)) return {};
    return *value * factor;
}
Json number(Count value) { return value ? Json(*value) : Json(); }
}
Json AmrResourceMetadata(const SimConfig& c, int species_count) {
    const auto dim = c.grid.dim;
    Count roots = c.grid.nblockx1;
    if (dim >= 2) roots = multiply(roots, c.grid.nblockx2);
    if (dim == 3) roots = multiply(roots, c.grid.nblockx3);
    const auto padded = PaddedCells(dim);
    const auto base_bytes = padded * 6 * 3 * 8;
    const auto cells = amr::BLOCK_NX * (dim >= 2 ? amr::BLOCK_NY : 1) * (dim == 3 ? amr::BLOCK_NZ : 1);
    auto levels = Json::array();
    Count leaves = roots;
    for (int level = 0; level <= c.amr.lrefinemax; ++level) {
        const auto base = multiply(leaves, base_bytes);
        const auto with_species = species_count < 0 ? Count{} : multiply(leaves, padded * (6LL + species_count) * 3 * 8);
        levels.push(Json::object({{"level", level}, {"fullDomainLeafBlocks", number(leaves)},
            {"activeCells", number(multiply(leaves, cells))}, {"baseStateBytes", number(base)},
            {"stateBytesIncludingSpecies", number(with_species)},
            {"overflow", !leaves || !base || (species_count >= 0 && !with_species)}}));
        leaves = multiply(leaves, 1 << dim);
    }
    const auto capacity = c.grid.amr_max_blocks > 0 ? c.grid.amr_max_blocks : 10000;
    return Json::object({{"version", "1"}, {"scope", "one-global-domain; current single-process storage layout"},
        {"advisoryOnly", true}, {"unit", "byte"}, {"rootBlocks", number(roots)},
        {"dimension", dim}, {"paddedCellsPerBlock", padded}, {"stateSlots", 3}, {"baseArraysPerState", 6},
        {"speciesCount", species_count < 0 ? Json() : Json(species_count)},
        {"configuredPoolCapacity", capacity}, {"poolPreallocatedBaseBytes", number(multiply(Count{capacity}, base_bytes))},
        {"levels", levels}, {"assumption", "entire domain refined to each listed level; not a local refinement prediction"},
        {"excludes", Json::array({"EOS tables", "AMR tree and transfer plans", "temporary arrays", "allocator overhead", "output buffers", "MPI layout", "thread workspaces"})},
        {"oomPrediction", "not-provided"}});
}
PreviewResponse EstimateAmrResources(const PreviewRequest& request) {
    detail::CaptureLogs logs;
    auto out = Json::object({{"schemaVersion", "1.0"}, {"version", "1"}, {"kind", "amr-resource-estimate"},
        {"status", "error"}, {"identity", Json::object({{"requestId", request.request_id}, {"caseId", request.case_id},
            {"configRevision", core::string_sha256(request.config_text)}})},
        {"execution", Json::object({{"setup", "not_executed"}, {"eos", "not_loaded"}, {"cuda", "not_initialized"}})},
        {"diagnostics", Json::array()}, {"data", Json()}});
    try {
        const auto config = RuntimeParams::LoadText(request.config_text);
        if (config.amr.lrefinemin < 0 || config.amr.lrefinemax < config.amr.lrefinemin || config.amr.lrefinemax > 15)
            throw std::invalid_argument("Require 0 <= lrefinemin <= lrefinemax <= 15");
        out["data"] = AmrResourceMetadata(config);
        out["status"] = "ok";
        return SerializePreviewResponse(out, 0);
    } catch (const std::exception& e) {
        out["diagnostics"].push(Json::object({{"severity", "error"}, {"code", "INVALID_CONFIGURATION"}, {"message", e.what()}}));
        return SerializePreviewResponse(out, 3);
    }
}
} // namespace arch::api
