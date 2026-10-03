/**
 * @file ResourceEstimates.cpp
 * @brief Estimate bounded memory and cell counts from the resolved mesh plan.
 *
 * Workflow:
 * 1. Accept a bounded, verified request at the read-only API boundary.
 * 2. Estimate bounded memory and cell counts from the resolved mesh plan.
 * 3. Return typed evidence or an explicit error; do not start the simulation Driver.
 */

#include <limits>
#include <array>
#include <sstream>

#include "api/preview/ResourceEstimates.h"

#include "amr/topology/AmrDefines.h"
#include "api/protocol/LogCapture.h"
#include "api/protocol/Response.h"
#include "core/config/InputResolution.h"
#include "core/files/FileFingerprint.h"

namespace arch::api {
using detail::Json;
/** Count native padded cells per block for the requested dimensionality. */
std::int64_t PaddedCells(int dimension) {
    return std::int64_t(amr::PAD_NX) * (dimension >= 2 ? amr::BLOCK_NY + 2*amr::MAX_NG : 1)
        * (dimension == 3 ? amr::BLOCK_NZ + 2*amr::MAX_NG : 1);
}
namespace {
using Count = std::optional<std::int64_t>;
/** Multiply resource counts with overflow detection. */
Count multiply(Count value, std::int64_t factor) {
    if (!value || factor < 0 || (factor && *value > std::numeric_limits<std::int64_t>::max()/factor)) return {};
    return *value * factor;
}
/** Represent an unavailable overflowed count as JSON null. */
Json number(Count value) { return value ? Json(*value) : Json(); }
}
/** Count only the supplied mesh plan; no runtime configuration is constructed. */
static Json ResourceCounts(int dim, const std::array<int, 3>& blocks, int maximum_level,
                    int configured_capacity, int species_count) {
    Count roots = RootBlockCount(dim, blocks);
    const auto padded = PaddedCells(dim);
    const auto base_bytes = padded * 6 * 3 * 8;
    const auto cells = amr::BLOCK_NX * (dim >= 2 ? amr::BLOCK_NY : 1) * (dim == 3 ? amr::BLOCK_NZ : 1);
    auto levels = Json::array();
    Count leaves = roots;
    for (int level = 0; level <= maximum_level; ++level) {
        const auto base = multiply(leaves, base_bytes);
        const auto with_species = species_count < 0 ? Count{} : multiply(leaves, padded * (6LL + species_count) * 3 * 8);
        levels.push(Json::object({{"level", level}, {"fullDomainLeafBlocks", number(leaves)},
            {"activeCells", number(multiply(leaves, cells))}, {"baseStateBytes", number(base)},
            {"stateBytesIncludingSpecies", number(with_species)},
            {"overflow", !leaves || !base || (species_count >= 0 && !with_species)}}));
        leaves = multiply(leaves, 1 << dim);
    }
    const auto capacity = configured_capacity > 0 ? configured_capacity : 10000;
    return Json::object({{"version", "1"}, {"scope", "one-global-domain; current single-process storage layout"},
        {"advisoryOnly", true}, {"unit", "byte"}, {"rootBlocks", number(roots)},
        {"dimension", dim}, {"paddedCellsPerBlock", padded}, {"stateSlots", 3}, {"baseArraysPerState", 6},
        {"speciesCount", species_count < 0 ? Json() : Json(species_count)},
        {"configuredPoolCapacity", capacity}, {"poolPreallocatedBaseBytes", number(multiply(Count{capacity}, base_bytes))},
        {"levels", levels}, {"assumption", "entire domain refined to each listed level; not a local refinement prediction"},
        {"excludes", Json::array({"EOS tables", "AMR tree and transfer plans", "temporary arrays", "allocator overhead", "output buffers", "MPI layout", "thread workspaces", "self-gravity potential/acceleration, face stencils and MG/FGMRES workspaces"})},
        {"oomPrediction", "not-provided"}});
}
/** Runtime callers share the same counting formulas as partial inspection. */
Json AmrResourceMetadata(const SimConfig& c, int species_count) {
    return ResourceCounts(c.grid.dim, {c.grid.nblockx1, c.grid.nblockx2, c.grid.nblockx3},
                          c.amr.lrefinemax, c.grid.amr_max_blocks, species_count);
}
/** Answer a resource-estimate request without allocating the full simulation. */
PreviewResponse EstimateAmrResources(const PreviewRequest& request) {
    detail::CaptureLogs logs;
    auto out = Json::object({{"schemaVersion", "1.0"}, {"version", "1"}, {"kind", "amr-resource-estimate"},
        {"status", "error"}, {"identity", Json::object({{"requestId", request.request_id}, {"caseId", request.case_id},
            {"configRevision", core::string_sha256(request.config_text)}})},
        {"execution", Json::object({{"setup", "not_executed"}, {"eos", "not_loaded"}, {"cuda", "not_initialized"},
            {"simulationReadiness", "not_checked"}, {"filesystem", "not_accessed"},
            {"configurationScope", "resource-count-inputs"}})},
        {"diagnostics", Json::array()}, {"data", Json()}});
    try {
        ConfigParser parser;
        std::istringstream stream(request.config_text);
        parser.Read(stream, "stdin");
        config::InputContext context;
        context.purpose = config::ConfigurationPurpose::InitialState;
        const auto input = config::ResolveStandardInput(parser, context);
        // Unrelated missing physical controls do not prevent a count estimate.
        // Explicit syntax/type/range errors still fail; no invalid value is used.
        std::vector<ConfigInputDiagnostic> errors;
        for (const auto& diagnostic : input.diagnostics)
            if (diagnostic.code != "MISSING_PARAMETER") errors.push_back(diagnostic);
        const char* required[] = {"nblockx1", "nblockx2", "nblockx3",
                                  "lrefinemin", "lrefinemax", "max_blocks"};
        for (const auto key : required) {
            const auto& record = input.parameters.at(key);
            if (record.state == config::InputState::Missing && !record.resolved)
                errors.push_back({key, "MISSING_PARAMETER",
                    "Resource estimation requires this explicit mesh input.", {}});
        }
        if (!errors.empty()) throw ConfigInputError(errors);
        const auto value = [&](const char* key) {
            const auto* result = config::input_detail::get<int>(input, key);
            if (!result) throw ConfigValueError(key, "UNRESOLVED_DEPENDENCY",
                "Resource-count input is unresolved.");
            return *result;
        };
        const int dim = value("nblockx3") > 0 ? 3 : value("nblockx2") > 0 ? 2 : 1;
        out["data"] = ResourceCounts(dim, {value("nblockx1"), value("nblockx2"), value("nblockx3")},
                                    value("lrefinemax"), value("max_blocks"), -1);
        out["status"] = "ok";
        return SerializePreviewResponse(out, 0);
    } catch (const ConfigInputError& e) {
        for (const auto& diagnostic : e.diagnostics) {
            auto positions = Json::array();
            for (const auto& location : diagnostic.locations)
                positions.push(Json::object({{"source", location.source},
                    {"line", std::int64_t(location.line)}, {"column", std::int64_t(location.column)},
                    {"endColumn", std::int64_t(location.end_column)}}));
            out["diagnostics"].push(Json::object({{"severity", "error"}, {"code", diagnostic.code},
                {"parameterKey", diagnostic.key.empty() ? Json() : Json(diagnostic.key)},
                {"message", diagnostic.message}, {"locations", positions}}));
        }
        return SerializePreviewResponse(out, 3);
    } catch (const std::exception& e) {
        out["diagnostics"].push(Json::object({{"severity", "error"}, {"code", "INVALID_CONFIGURATION"}, {"message", e.what()}}));
        return SerializePreviewResponse(out, 3);
    }
}
} // namespace arch::api
