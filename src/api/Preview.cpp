#include "Preview.h"
#include "Json.h"
#include "ParameterMetadata.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <streambuf>

#include "../core/FileFingerprint.h"
#include "../core/InitialStateConversion.h"
#include "../core/ProblemRegistry.h"
#include "../core/RuntimeParams.h"
#include "../physics/eos/eosdispatch.h"

namespace arch::api {
namespace {
using detail::Json;

class BoundedLog : public std::streambuf {
public:
    std::string text;
    bool truncated = false;
    std::string message() const {
        std::string result = text;
        // Truncation may cut a UTF-8 character. Drop the final multibyte
        // sequence rather than emitting malformed JSON text.
        if (truncated) {
            while (!result.empty() && (static_cast<unsigned char>(result.back()) & 0xc0) == 0x80)
                result.pop_back();
            if (!result.empty() && static_cast<unsigned char>(result.back()) >= 0xc0)
                result.pop_back();
        }
        return result;
    }
protected:
    int_type overflow(int_type c) override {
        if (!traits_type::eq_int_type(c, traits_type::eof())) {
            if (text.size() < 16384) text.push_back(traits_type::to_char_type(c));
            else truncated = true;
        }
        return traits_type::not_eof(c);
    }
};
class CaptureLogs {
public:
    BoundedLog info, warning;
private:
    std::streambuf *out_, *err_;
public:
    CaptureLogs() : out_(std::cout.rdbuf(&info)), err_(std::cerr.rdbuf(&warning)) {}
    ~CaptureLogs() { std::cout.rdbuf(out_); std::cerr.rdbuf(err_); }
};
Json diagnostic(const char *severity, const std::string &code, const std::string &message) {
    return Json::object({{"severity", severity}, {"code", code}, {"message", message}});
}
Json envelope(const PreviewRequest &request) {
    return Json::object({
        {"schemaVersion", preview_schema_version}, {"kind", "initial-state-preview"},
        {"status", "error"}, {"stage", "input"},
        {"identity", Json::object({{"requestId", request.request_id}, {"caseId", request.case_id},
            {"configRevision", core::string_sha256(request.config_text)}})},
        {"execution", Json::object({{"previewBackend", "cpu"}, {"simulationReadiness", "not_checked"},
            {"timeStepping", "not_executed"}, {"scientificOutput", "not_created"}})},
        {"state", Json::object({{"configuration", "not_loaded"}, {"setup", "not_executed"},
            {"grid", Json()}, {"amr", Json()}, {"eos", Json()}, {"species", Json()}})},
        {"data", Json()}, {"diagnostics", Json::array()}});
}

Json grid_snapshot(const SimConfig &config) {
    const auto &g = config.grid;
    const int blocks[] = {g.nblockx1, g.nblockx2, g.nblockx3};
    const int cells[] = {amr::BLOCK_NX, amr::BLOCK_NY, amr::BLOCK_NZ};
    const double lo[] = {g.x1_min, g.x2_min, g.x3_min};
    const double hi[] = {g.x1_max, g.x2_max, g.x3_max};
    const std::string lower[] = {g.x1l_boundary_type, g.x2l_boundary_type, g.x3l_boundary_type};
    const std::string upper[] = {g.x1r_boundary_type, g.x2r_boundary_type, g.x3r_boundary_type};
    auto axes = Json::array();
    for (int axis = 0; axis < g.dim; ++axis) {
        const std::int64_t n = std::int64_t(blocks[axis]) * cells[axis];
        axes.push(Json::object({{"name", "x" + std::to_string(axis + 1)}, {"unit", Json()},
            {"min", lo[axis]}, {"max", hi[axis]}, {"rootBlocks", blocks[axis]},
            {"activeCellsPerBlock", cells[axis]}, {"rootCells", n},
            {"coordinateSpacing", n > 0 ? Json((hi[axis] - lo[axis]) / n) : Json()},
            {"lowerBoundary", lower[axis]}, {"upperBoundary", upper[axis]}}));
    }
    return Json::object({{"status", "configured"}, {"geometry", g.geometry}, {"dimension", g.dim},
        {"axes", axes}, {"hierarchy", "not_constructed"}});
}
Json amr_snapshot(const SimConfig &config) {
    const auto &a = config.amr;
    auto indicators = Json::array();
    const std::pair<const char *, bool> flags[] = {
        {"DENS", a.refine_on_rho}, {"PRES", a.refine_on_p}, {"TEMP", a.refine_on_temp},
        {"VELX", a.refine_on_velx}, {"VELY", a.refine_on_vely}, {"VELZ", a.refine_on_velz},
        {"ENER", a.refine_on_eng}, {"VORT", a.refine_on_vorticity}, {"DIVV", a.refine_on_div_v},
        {"ENTR", a.refine_on_entropy}, {"ENUC", a.refine_on_enuc}, {"JENS", a.refine_on_jeans}};
    for (const auto &[key, active] : flags) if (active) indicators.push(key);
    if (a.refine_on_species) {
        if (a.refine_all_species) indicators.push("SPECIES");
        else for (const auto &name : a.refine_species_names) indicators.push(name);
    }
    return Json::object({{"status", "configured"}, {"enabled", a.lrefinemax > 0},
        {"minLevel", a.lrefinemin}, {"maxLevel", a.lrefinemax},
        {"requestedIndicators", a.refine_var}, {"parsedIndicators", indicators},
        {"indicatorEvaluation", "not_executed"},
        {"refineThreshold", a.refine_threshold}, {"derefineThreshold", a.derefine_threshold},
        {"regridInterval", a.regrid_interval}, {"maxBlocks", config.grid.amr_max_blocks},
        {"effectiveMaxBlocks", config.grid.amr_max_blocks > 0 ? config.grid.amr_max_blocks : 10000},
        {"initialRefinement", "not_executed"}, {"actualHierarchy", Json()}});
}
Json eos_snapshot(const SimConfig &config) {
    return Json::object({{"status", "not_loaded"}, {"requested", config.physics.eos_type},
        {"resolved", Json()}, {"configuredGamma", config.physics.gamma},
        {"tablePath", config.physics.eos_table_path},
        {"componentTablePath", config.physics.eos_helm_table_path},
        {"loadedTablePath", Json()}, {"sourceFingerprint", Json()}});
}
void snapshot(Json &state, const SimConfig &config) {
    state["configuration"] = "parsed";
    state["grid"] = grid_snapshot(config);
    state["amr"] = amr_snapshot(config);
    state["eos"] = eos_snapshot(config);
    state["computeBackendRequested"] = config.execution.compute_backend;
}
void validate_grid(const SimConfig &config) {
    for (const auto &[key, value] : config.custom_params)
        if (!std::isfinite(value))
            throw std::invalid_argument("Non-finite numeric configuration value: " + key);
    const auto &g = config.grid;
    if (g.nblockx1 <= 0 || !std::isfinite(g.x1_min) || !std::isfinite(g.x1_max)
        || !(g.x1_max > g.x1_min) || !std::isfinite(g.x1_max - g.x1_min))
        throw std::invalid_argument("x1 bounds must be finite and ordered, and nblockx1 must be positive");
    if (config.amr.lrefinemin < 0 || config.amr.lrefinemax < config.amr.lrefinemin
        || config.amr.lrefinemax > amr::kMaxRefinementLevel)
        throw std::invalid_argument("AMR levels must satisfy 0 <= lrefinemin <= lrefinemax <= 15");
    const std::uint64_t extent = std::uint64_t(g.nblockx1) << config.amr.lrefinemax;
    if (extent - 1 > amr::kMortonCoordinateMask)
        throw std::invalid_argument("AMR root extent exceeds the supported coordinate range");
    const int max_blocks = g.amr_max_blocks > 0 ? g.amr_max_blocks : 10000;
    if (max_blocks < g.nblockx1)
        throw std::invalid_argument("max_blocks cannot hold the configured root blocks");
    if (!dispatch::parse_boundary(g.x1l_boundary_type).ok
        || !dispatch::parse_boundary(g.x1r_boundary_type).ok)
        throw std::invalid_argument("Unsupported x1 boundary type");
}

Json field(const char *key, const char *name, const std::vector<double> &values) {
    auto data = Json::array();
    for (double value : values) data.push(value);
    const auto [low, high] = std::minmax_element(values.begin(), values.end());
    return Json::object({{"key", key}, {"displayName", name}, {"unit", Json()},
        {"values", data}, {"min", *low}, {"max", *high}});
}
} // namespace

PreviewResponse GeneratePreview(const PreviewRequest &request) {
    CaptureLogs logs;
    Json result = envelope(request);
    int exit_code = 2;
    std::string error_code = "INVALID_REQUEST";
    try {
        if (request.config_text.size() > max_config_bytes || request.config_text.empty()
            || request.request_id.size() > 128 || request.case_id.size() > 128
            || request.sample_count < 2 || request.sample_count > max_sample_count)
            throw std::invalid_argument("Expected nonempty config <= 1 MiB, 2..4096 samples and identifiers <= 128 bytes");
        result["stage"] = "configuration";
        exit_code = 3; error_code = "INVALID_CONFIGURATION";
        auto reads = std::make_shared<preview::ParameterReadTrace>(std::set<std::string>{"x_pos"});
        SimConfig config = RuntimeParams::LoadText(request.config_text, reads);
        auto &state = result["state"];
        snapshot(state, config);
        result["stage"] = "support";
        exit_code = 4; error_code = "UNSUPPORTED_PREVIEW";
        if (request.case_id != "Sod" || config.grid.dim != 1 || config.grid.geometry != "cartesian")
            throw std::invalid_argument("Preview 1.0 supports registered Sod in one-dimensional Cartesian geometry");
        if (config.io.restart)
            throw std::invalid_argument("Initial-state preview does not load restart checkpoints");
        result["stage"] = "configuration";
        exit_code = 3; error_code = "INVALID_CONFIGURATION";
        validate_grid(config);
        result["stage"] = "setup";
        exit_code = 5; error_code = "SETUP_FAILED";
        state["setup"] = "error";
        auto problem = ProblemRegistry::Get().Create(request.case_id);
        if (!problem) throw std::runtime_error("The selected problem is not registered in this binary");
        SpeciesManager specs;
        try {
            problem->Setup(config, specs);
        } catch (...) {
            config.parameter_reads.reset();
            PublishParameterMetadata(result, *reads, problem->PreviewPositions(config), false);
            throw;
        }
        config.parameter_reads.reset();
        const auto positions = problem->PreviewPositions(config);
        PublishParameterMetadata(result, *reads, positions, false);
        snapshot(state, config); // Setup can change the effective configuration.
        state["setup"] = "ready";
        auto species = Json::array();
        for (int i = 0; i < specs.count(); ++i)
            species.push(Json::object({{"index", i}, {"name", specs.get_name(i)}}));
        state["species"] = species;
        if (specs.count() == 0) throw std::runtime_error("Initialization registered no species");
        result["stage"] = "eos";
        error_code = "EOS_FAILED";
        state["eos"]["status"] = "error";
        dispatch::EosId eos_id{};
        if (dispatch::registration_matches<dispatch::Tabular3DPolicy>(config.physics.eos_type)) {
            const int rank = inspect_eos_table_rank(EOSDispatcher::table_path(config, "Tabular"));
            if (rank != 3 && rank != 4) throw std::invalid_argument("EOS table rank must be 3 or 4");
            eos_id = rank == 3 ? dispatch::EosId::Tabular3D : dispatch::EosId::Tabular4D;
        } else {
            const auto parsed = dispatch::parse_registered_policy<dispatch::EosPolicies>(config.physics.eos_type);
            if (!parsed.ok) throw std::invalid_argument("Unknown EOS type: " + config.physics.eos_type);
            eos_id = parsed.value;
        }
        state["eos"]["resolved"] = std::string(dispatch::canonical_policy_name<dispatch::EosPolicies>(eos_id));
        std::vector<double> coordinates;
        std::array<std::vector<double>, 6> values;
        EOSDispatcher::dispatch_eos(eos_id, config, specs, [&](auto &&eos, std::string_view fingerprint) {
            state["eos"]["status"] = "ready";
            if (eos_id != dispatch::EosId::Ideal)
                state["eos"]["loadedTablePath"] = EOSDispatcher::table_path(config, "Preview");
            state["eos"]["sourceFingerprint"] = fingerprint.empty() ? Json() : Json(std::string(fingerprint));
            result["stage"] = "sampling";
            exit_code = 6; error_code = "INITIALIZATION_FAILED";
            PrimitiveData data{};
            const double length = config.grid.x1_max - config.grid.x1_min;
            for (int i = 0; i < request.sample_count; ++i) {
                data = PrimitiveData{};
                data.mass_fractions.resize(specs.count(), 0.0);
                PointCoords point{};
                point.x = config.grid.x1_min + length * ((i + 0.5) / request.sample_count);
                // Sod's supported Cartesian initialization reads x only.
                if (!std::isfinite(point.x) || !(point.x > config.grid.x1_min)
                    || !(point.x < config.grid.x1_max)
                    || (!coordinates.empty() && !(point.x > coordinates.back())))
                    throw std::invalid_argument("Domain cannot represent distinct interior sample coordinates");
                problem->SampleInitialPrimitive(point, data);
                if (data.mass_fractions.size() != std::size_t(specs.count())
                    || !std::isfinite(data.rho) || data.rho <= 0)
                    throw std::runtime_error("Invalid density or composition extent from initializer");
                for (double fraction : data.mass_fractions)
                    if (!std::isfinite(fraction)) throw std::runtime_error("Non-finite initial composition");
                const FluidVector conserved = ProblemHelper::detail::InitialConservedState(data, eos);
                const double pressure = eos.get_pressure(conserved, data.mass_fractions.data());
                const double eint = eos_utils::extract_specific_internal_energy(conserved);
                const double temperature = eos.get_temperature(data.rho, eint, data.mass_fractions.data());
                const double row[] = {data.rho, pressure, temperature, data.u, conserved.eng, eint};
                if (pressure <= 0 || temperature <= 0)
                    throw std::runtime_error("EOS returned non-positive initial pressure or temperature");
                for (std::size_t j = 0; j < values.size(); ++j) {
                    if (!std::isfinite(row[j])) throw std::runtime_error("Non-finite initial field value");
                    values[j].push_back(row[j]);
                }
                coordinates.push_back(point.x);
            }
        });
        auto x = Json::array();
        for (double coordinate : coordinates) x.push(coordinate);
        auto fields = Json::array();
        const char *keys[] = {"DENS", "PRES", "TEMP", "VELX", "ENER", "EINT"};
        const char *names[] = {"Density", "Pressure", "Temperature", "X velocity", "Total energy density", "Specific internal energy"};
        for (std::size_t j = 0; j < values.size(); ++j) fields.push(field(keys[j], names[j], values[j]));
        result["data"] = Json::object({{"dimension", 1}, {"kind", "line"},
            {"sampling", Json::object({{"kind", "uniform"}, {"valueLocation", "init-sample"},
                {"position", "bin-center"}, {"count", request.sample_count},
                {"shape", Json::array({request.sample_count})}, {"order", "x1-fastest"}})},
            {"axes", Json::array({Json::object({{"name", "x1"}, {"unit", Json()}, {"values", x}})})},
            {"fields", fields}});
        PublishParameterMetadata(result, *reads, positions, true);
        result["status"] = "ok"; result["stage"] = "complete";
        exit_code = 0;
    } catch (const std::exception &error) {
        result["diagnostics"].push(diagnostic("error", error_code, error.what()));
    }
    for (const auto &[log, severity] : {std::pair{&logs.info, "info"}, std::pair{&logs.warning, "warning"}}) {
        if (!log->text.empty()) result["diagnostics"].push(diagnostic(severity, "CORE_LOG", log->message()));
        if (log->truncated) result["diagnostics"].push(diagnostic("warning", "LOG_TRUNCATED", "Core log exceeded 16 KiB"));
    }
    std::string json = result.dump();
    if (json.size() > max_response_bytes) return PreviewInputError("Response exceeds 8 MiB");
    return {std::move(json), exit_code};
}

std::string PreviewCapabilities() {
    return Json::object({{"schemaVersion", preview_schema_version}, {"kind", "preview-capabilities"},
        {"status", "ok"}, {"cases", Json::array({"Sod"})}, {"dimensions", Json::array({1})},
        {"geometries", Json::array({"cartesian"})}, {"previewBackend", "cpu"},
        {"eosTypes", Json::array({"ideal", "helmholtz", "tabular"})},
        {"resolvedEosPolicies", Json::array({"ideal", "helmholtz", "tabular3d", "tabular4d"})},
        {"fields", Json::array({"DENS", "PRES", "TEMP", "VELX", "ENER", "EINT"})},
        {"configInput", "stdin-par-text"}, {"defaultSamples", default_sample_count},
        {"minSamples", 2}, {"maxSamples", max_sample_count},
        {"maxConfigBytes", std::int64_t(max_config_bytes)}, {"maxResponseBytes", std::int64_t(max_response_bytes)},
        {"amrHierarchy", false}, {"parameterTracing", false}, {"markers", true},
        {"extensions", ParameterExtensionCapabilities()}}).dump();
}
PreviewResponse PreviewInputError(const std::string &message) {
    return {Json::object({{"schemaVersion", preview_schema_version}, {"kind", "initial-state-preview"},
        {"status", "error"}, {"stage", "input"}, {"identity", Json()}, {"state", Json()}, {"data", Json()},
        {"diagnostics", Json::array({diagnostic("error", "INVALID_REQUEST", message)})}}).dump(), 2};
}
} // namespace arch::api
