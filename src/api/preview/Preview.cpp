/**
 * @file Preview.cpp
 * @brief Resolve and sample requested initial state on the CPU preview path.
 *
 * Workflow:
 * 1. Accept a bounded, verified request at the read-only API boundary.
 * 2. Resolve and sample requested initial state on the CPU preview path.
 * 3. Return typed evidence or an explicit error; do not start the simulation Driver.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <streambuf>

#include "api/Preview.h"

#include "api/Configuration.h"
#include "api/PreviewSession.h"
#include "api/configuration/ParameterMetadata.h"
#include "api/configuration/ValueDomain.h"
#include "api/preview/InitialMesh.h"
#include "api/preview/Sampling.h"
#include "api/preview/StateSnapshot.h"
#include "api/protocol/Json.h"
#include "api/protocol/LogCapture.h"
#include "api/protocol/Progress.h"
#include "api/protocol/Response.h"
#include "api/session/InitialSampleCache.h"
#include "core/config/RuntimeParams.h"
#include "core/files/FileFingerprint.h"
#include "core/problem/InitialStateConversion.h"
#include "core/problem/ProblemHelper.h"
#include "core/problem/ProblemRegistry.h"
#include "grid/Grid.h"
#include "physics/eos/eosdispatch.h"

namespace arch::api {
namespace {
using detail::Json;

using detail::CaptureLogs;
/** Create a structured preview diagnostic with severity and code. */
Json diagnostic(const char *severity, const std::string &code, const std::string &message) {
    return Json::object({{"severity", severity}, {"code", code}, {"message", message}});
}
/** Initialize a versioned preview response with request identity and CPU-only status. */
Json envelope(const PreviewRequest &request) {
    return Json::object({
        {"schemaVersion", preview_schema_version}, {"kind", request.initial_mesh ? "initial-amr-preview" : "initial-state-preview"},
        {"status", "error"}, {"stage", "input"},
        {"identity", Json::object({{"requestId", request.request_id}, {"caseId", request.case_id},
            {"configRevision", core::string_sha256(request.config_text)}})},
        {"execution", Json::object({{"previewBackend", "cpu"}, {"simulationReadiness", "not_checked"},
            {"timeStepping", "not_executed"}, {"scientificOutput", "not_created"}})},
        {"state", Json::object({{"configuration", "not_loaded"}, {"setup", "not_executed"},
            {"grid", Json()}, {"amr", Json()}, {"eos", Json()}, {"species", Json()}})},
        {"data", Json()}, {"diagnostics", Json::array()}});
}

/** Reject invalid root blocks, domain bounds and AMR coordinate ranges. */
void validate_grid(const SimConfig &config) {
    for (const auto &[key, value] : config.custom_params)
        if (!std::isfinite(value))
            throw std::invalid_argument("Non-finite numeric configuration value: " + key);
    const auto &g = config.grid;
    if (config.amr.lrefinemin < 0 || config.amr.lrefinemax < config.amr.lrefinemin
        || config.amr.lrefinemax > amr::kMaxRefinementLevel)
        throw std::invalid_argument("AMR levels must satisfy 0 <= lrefinemin <= lrefinemax <= 15");
    const int blocks[] = {g.nblockx1, g.nblockx2};
    const double lo[] = {g.x1_min, g.x2_min}, hi[] = {g.x1_max, g.x2_max};
    const std::string lower[] = {g.x1l_boundary_type, g.x2l_boundary_type};
    const std::string upper[] = {g.x1r_boundary_type, g.x2r_boundary_type};
    std::uint64_t roots = 1;
    for (int axis = 0; axis < g.dim; ++axis) {
        if (blocks[axis] <= 0 || !std::isfinite(lo[axis]) || !std::isfinite(hi[axis])
            || !(hi[axis] > lo[axis]) || !std::isfinite(hi[axis] - lo[axis]))
            throw std::invalid_argument("Active axis bounds must be finite and ordered, with positive root blocks");
        const std::uint64_t extent = std::uint64_t(blocks[axis]) << config.amr.lrefinemax;
        if (extent - 1 > amr::kMortonCoordinateMask)
            throw std::invalid_argument("AMR root extent exceeds the supported coordinate range");
        roots *= std::uint64_t(blocks[axis]);
        if (!dispatch::parse_boundary(lower[axis]).ok || !dispatch::parse_boundary(upper[axis]).ok)
            throw std::invalid_argument("Unsupported active-axis boundary type");
    }
    const int max_blocks = g.amr_max_blocks > 0 ? g.amr_max_blocks : 10000;
    if (std::uint64_t(max_blocks) < roots)
        throw std::invalid_argument("max_blocks cannot hold the configured root blocks");
}

/** Place distinct finite sampling coordinates at interior cell centers. */
std::vector<double> sample_axis(double lo, double hi, int count) {
    std::vector<double> coordinates;
    coordinates.reserve(count);
    for (int i = 0; i < count; ++i) {
        const double x = lo + (hi - lo) * ((i + 0.5) / count);
        if (!std::isfinite(x) || !(x > lo) || !(x < hi)
            || (!coordinates.empty() && !(x > coordinates.back())))
            throw std::invalid_argument("Domain cannot represent distinct interior sample coordinates");
        coordinates.push_back(x);
    }
    return coordinates;
}
/** Serialize one coordinate axis and its CGS unit. */
Json axis_json(const char *name, const std::vector<double> &coordinates, const std::string& system) {
    auto values = Json::array();
    for (double x : coordinates) values.push(x);
    return Json::object({{"name", name}, {"unit", AxisUnit("x", system)}, {"values", values}});
}

/** Serialize one sampled field with its finite range and unit. */
Json field(const char *key, const char *name, const std::vector<double> &values, const std::string& system) {
    auto data = Json::array();
    ValueDomain domain;
    for (double value : values) { data.push(value); domain.observe(value); }
    const auto [low, high] = std::minmax_element(values.begin(), values.end());
    return Json::object({{"key", key}, {"displayName", name}, {"unit", FieldUnit(key, system)},
        {"values", data}, {"min", *low}, {"max", *high}, {"logDomain", domain.json()}});
}
} // namespace

/** Run bounded case setup and initial-field sampling through CPU-owned resources. */
PreviewResponse GeneratePreview(const PreviewRequest &request) {
    CaptureLogs logs;
    Json result = envelope(request);
    int exit_code = 2;
    std::string error_code = "INVALID_REQUEST";
    try {
        EOSDispatcher::InspectionScope eos_sources;
        if (request.config_text.size() > max_config_bytes || request.config_text.empty()
            || request.request_id.size() > 128 || request.case_id.size() > 128)
            throw std::invalid_argument("Expected nonempty config <= 1 MiB and identifiers <= 128 bytes");
        SamplingPlan sampling;
        try { sampling = ResolveSampling(request); }
        catch (const SamplingLimitError &) { error_code = "SAMPLING_LIMIT_EXCEEDED"; throw; }
        ReportStage(request, result, "configuration");
        exit_code = 3; error_code = "INVALID_CONFIGURATION";
        std::shared_ptr<preview::ParameterReadTrace> reads;
        if (request.case_id == "Sod")
            reads = std::make_shared<preview::ParameterReadTrace>(std::set<std::string>{"x_pos"});
        SimConfig config = RuntimeParams::LoadText(request.config_text, reads);
        auto &state = result["state"];
        PublishStateSnapshot(state, config);
        ReportStage(request, result, "support");
        exit_code = 4; error_code = "UNSUPPORTED_PREVIEW";
        if (config.grid.geometry != "cartesian"
            || !((request.case_id == "Sod" && config.grid.dim == 1)
                 || (request.case_id == "CellularDet" && config.grid.dim == 2)))
            throw std::invalid_argument("Preview supports Cartesian Sod 1D and CellularDet 2D");
        if (config.io.restart)
            throw std::invalid_argument("Initial-state preview does not load restart checkpoints");
        ReportStage(request, result, "configuration");
        exit_code = 3; error_code = "INVALID_CONFIGURATION";
        validate_grid(config);
        if (sampling.two_dimensional) {
            auto it = config.custom_params.find("shock_dir");
            if (it != config.custom_params.end()
                && (std::trunc(it->second) != it->second || it->second < std::numeric_limits<int>::min()
                    || it->second > std::numeric_limits<int>::max()))
                throw std::invalid_argument("shock_dir must be a whole number within the integer range");
            ReportStage(request, result, "support");
            exit_code = 4; error_code = "UNSUPPORTED_PREVIEW";
            const int direction = config.Get<int>("shock_dir", 0);
            if (direction != 0 && direction != 1)
                throw std::invalid_argument("CellularDet 2D supports shock_dir=0 or 1 only");
        }
        ReportStage(request, result, "setup");
        exit_code = 5; error_code = "SETUP_FAILED";
        state["setup"] = "error";
        auto problem = ProblemRegistry::Get().Create(request.case_id);
        if (!problem) throw std::runtime_error("The selected problem is not registered in this binary");
        SpeciesManager specs;
        try {
            problem->Setup(config, specs);
        } catch (const ProblemHelper::InitialEosError &) {
            config.parameter_reads.reset();
            state["species"] = SpeciesSnapshot(specs);
            state["eos"]["status"] = "error";
            ReportStage(request, result, "eos");
            error_code = "EOS_FAILED";
            throw;
        } catch (...) {
            config.parameter_reads.reset();
            if (reads) PublishParameterMetadata(result, *reads, problem->PreviewPositions(config), false);
            throw;
        }
        config.parameter_reads.reset();
        const auto positions = problem->PreviewPositions(config);
        if (reads) PublishParameterMetadata(result, *reads, positions, false);
        PublishStateSnapshot(state, config); // Setup can change the effective configuration.
        state["setup"] = "ready";
        state["species"] = SpeciesSnapshot(specs);
        if (specs.count() == 0) throw std::runtime_error("Initialization registered no species");
        ReportStage(request, result, "eos");
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
        std::optional<MeshResult> mesh;
        std::vector<double> x_coordinates, y_coordinates;
        const std::size_t field_count = sampling.two_dimensional ? max_preview_fields : 6;
        std::vector<std::vector<double>> values(field_count);
        for (auto &field_values : values) field_values.reserve(sampling.count);
        EOSDispatcher::dispatch_eos(eos_id, config, specs, [&](auto &&eos, std::string_view fingerprint) {
            state["eos"]["status"] = "ready";
            if (eos_id != dispatch::EosId::Ideal)
                state["eos"]["loadedTablePath"] = EOSDispatcher::table_path(config, "Preview");
            state["eos"]["sourceFingerprint"] = fingerprint.empty() ? Json() : Json(std::string(fingerprint));
            if (request.initial_mesh) {
                ReportStage(request, result, "initial-refinement");
                error_code = "INITIAL_MESH_FAILED"; exit_code = 6;
                mesh = BuildInitialMesh(*problem, config, specs, eos_id, eos, request);
                return;
            }
            ReportStage(request, result, "sampling");
            exit_code = 6; error_code = "INITIALIZATION_FAILED";
            PrimitiveData data{};
            InitialSampleCache conversions;
            x_coordinates = sample_axis(config.grid.x1_min, config.grid.x1_max, sampling.nx);
            if (sampling.two_dimensional)
                y_coordinates = sample_axis(config.grid.x2_min, config.grid.x2_max, sampling.ny);
            for (int j = 0; j < sampling.ny; ++j) {
                for (int i = 0; i < sampling.nx; ++i) {
                    data = PrimitiveData{};
                    data.mass_fractions.resize(specs.count(), 0.0);
                    const PointCoords point = Grid::PhysicalCoordsFromNative(config.grid.dim, config.grid.geometry,
                        x_coordinates[i], sampling.two_dimensional ? y_coordinates[j] : 0.0);
                    problem->SampleInitialPrimitive(point, data);
                    if (data.mass_fractions.size() != std::size_t(specs.count())
                        || !std::isfinite(data.rho) || data.rho <= 0)
                        throw std::runtime_error("Invalid density or composition extent from initializer");
                    for (double fraction : data.mass_fractions)
                        if (!std::isfinite(fraction)) throw std::runtime_error("Non-finite initial composition");
                    const auto row = conversions.evaluate(data, [&] {
                        const FluidVector conserved = ProblemHelper::detail::InitialConservedState(data, eos, config.numerics);
                        const double pressure = eos.get_pressure(conserved, data.mass_fractions.data());
                        const double eint = eos_utils::extract_specific_internal_energy(conserved);
                        const double temperature = eos.get_temperature(conserved.rho, eint, data.mass_fractions.data());
                        const InitialSampleCache::Row converted{conserved.rho, pressure, temperature, data.u, conserved.eng, eint, data.v};
                        if (pressure <= 0 || temperature <= 0)
                            throw std::runtime_error("EOS returned non-positive initial pressure or temperature");
                        for (double value : converted)
                            if (!std::isfinite(value)) throw std::runtime_error("Non-finite initial field value");
                        return converted;
                    });
                    for (std::size_t f = 0; f < values.size(); ++f) {
                        values[f].push_back(row[f]);
                    }
                }
            }
            if (request.sample_evaluation)
                request.sample_evaluation(sampling.count, conversions.conversions, conversions.hits);
        });
        ReportStage(request, result, "source-validation");
        error_code = "EOS_SOURCE_CHANGED"; exit_code = 6;
        eos_sources.validate();
        if (mesh) {
            if (reads && mesh->constructed) PublishParameterMetadata(result, *reads, positions, true);
            result["data"] = std::move(mesh->data);
            result["status"] = mesh->complete ? "ok" : "limited";
            ReportStage(request, result, "complete");
            state["grid"]["hierarchy"] = mesh->constructed ? "constructed" : "not_constructed";
            state["amr"]["actualHierarchy"] = mesh->constructed ? Json::object({{"location", "data.leaves"}}) : Json();
            state["amr"]["initialRefinement"] = mesh->complete ? "complete" : "limited";
            state["amr"]["indicatorEvaluation"] = "see-data-completedPasses";
            if (!mesh->complete) result["diagnostics"].push(diagnostic("warning", "PREVIEW_BUDGET_LIMIT", "Preview stopped at its working budget; this is not an OOM prediction."));
            for (const auto& [log, severity] : {std::pair{&logs.info, "info"}, std::pair{&logs.warning, "warning"}}) {
                if (!log->text.empty()) result["diagnostics"].push(diagnostic(severity, "CORE_LOG", log->message()));
                if (log->truncated) result["diagnostics"].push(diagnostic("warning", "LOG_TRUNCATED", "Core log exceeded 16 KiB"));
            }
            return SerializePreviewResponse(result, 0);
        }
        auto axes = Json::array({axis_json("x1", x_coordinates, UnitSystem(config))});
        auto shape = Json::array({sampling.nx});
        if (sampling.two_dimensional) {
            axes.push(axis_json("x2", y_coordinates, UnitSystem(config)));
            shape = Json::array({sampling.ny, sampling.nx});
        }
        auto fields = Json::array();
        const char *keys[] = {"DENS", "PRES", "TEMP", "VELX", "ENER", "EINT", "VELY"};
        const char *names[] = {"Density", "Pressure", "Temperature", "X velocity", "Total energy density", "Specific internal energy", "Y velocity"};
        for (std::size_t j = 0; j < values.size(); ++j) fields.push(field(keys[j], names[j], values[j], UnitSystem(config)));
        auto sampling_json = Json::object({{"kind", "uniform"}, {"valueLocation", "init-sample"},
            {"position", "bin-center"}, {"count", std::int64_t(sampling.count)},
            {"shape", shape}, {"order", "x1-fastest"}});
        if (sampling.two_dimensional)
            sampling_json["fixedCoordinates"] = Json::array({Json::object({{"name", "x3"}, {"value", 0}, {"unit", AxisUnit("x", UnitSystem(config))}})});
        result["data"] = Json::object({{"dimension", config.grid.dim}, {"kind", sampling.two_dimensional ? "grid" : "line"},
            {"sampling", sampling_json}, {"axes", axes},
            {"fields", fields}});
        if (reads) PublishParameterMetadata(result, *reads, positions, true);
        result["status"] = "ok"; ReportStage(request, result, "complete");
        exit_code = 0;
    } catch (const ConfigValueError& error) {
        auto item = diagnostic("error", error_code, error.what());
        item["parameterKey"] = error.key; item["detailCode"] = error.code;
        result["diagnostics"].push(std::move(item));
    } catch (const std::exception &error) {
        result["diagnostics"].push(diagnostic("error", error_code, error.what()));
    }
    for (const auto &[log, severity] : {std::pair{&logs.info, "info"}, std::pair{&logs.warning, "warning"}}) {
        if (!log->text.empty()) result["diagnostics"].push(diagnostic(severity, "CORE_LOG", log->message()));
        if (log->truncated) result["diagnostics"].push(diagnostic("warning", "LOG_TRUNCATED", "Core log exceeded 16 KiB"));
    }
    return SerializePreviewResponse(result, exit_code);
}

std::string PreviewCapabilities() {
    auto models = Json::array({
        Json::object({{"caseId", "Sod"}, {"dimensions", Json::array({1})},
            {"geometries", Json::array({"cartesian"})}, {"previewBackend", "cpu"},
            {"sampling", Json::object({{"defaultShape", Json::array({default_sample_count})},
                {"minPerAxis", 2}, {"maxPerAxis", max_sample_count}, {"maxTotalSamples", max_sample_count}})},
            {"fields", Json::array({"DENS", "PRES", "TEMP", "VELX", "ENER", "EINT"})},
            {"maxFields", 6}, {"maxResponseBytes", std::int64_t(max_response_bytes)}}),
        Json::object({{"caseId", "CellularDet"}, {"dimensions", Json::array({2})},
            {"geometries", Json::array({"cartesian"})}, {"previewBackend", "cpu"},
            {"supportedShockDirections", Json::array({0, 1})},
            {"sampling", Json::object({{"defaultShape", Json::array({default_samples_2d, default_samples_2d})},
                {"minPerAxis", 2}, {"maxPerAxis", max_samples_per_axis_2d},
                {"maxTotalSamples", std::int64_t(max_total_samples_2d)}})},
            {"fields", Json::array({"DENS", "PRES", "TEMP", "VELX", "ENER", "EINT", "VELY"})},
            {"maxFields", std::int64_t(max_preview_fields)}, {"maxResponseBytes", std::int64_t(max_response_bytes)}})
    });
    // Preserve the flat Sod capability view for existing 1D clients. New
    // clients use modelCapabilities to negotiate each case independently.
    auto extensions = ParameterExtensionCapabilities();
    extensions["session"] = PreviewSessionCapability();
    extensions["configuration"] = ConfigurationExtensions();
    extensions["discovery"] = Json::object({{"version", "1"}, {"command", "--list-cases"}});
    extensions["amr"] = Json::object({{"version", "1"}, {"resourcesCommand", "--amr-resources"},
        {"meshCommand", "--preview-amr"}, {"workerPlatform", "linux"},
        {"cases", Json::array({"Sod", "CellularDet"})}, {"geometries", Json::array({"cartesian"})},
        {"defaultMaxBlocks", contract::mesh_default_blocks}, {"defaultMemoryMiB", contract::mesh_default_memory_mib}, {"processAddressSpaceMiB", contract::worker_address_space_mib},
        {"processCpuSeconds", contract::worker_cpu_seconds}, {"hostWallTimeoutRequired", true}});
    extensions["caseInspection"] = Json::object({{"version", contract::initialization_version},
        {"command", "--inspect-case"}, {"caseDiscovery", "--list-cases"},
        {"automaticExpressionInference", false}, {"workerPlatform", "linux"},
        {"hostWallTimeoutSeconds", contract::case_wall_seconds}, {"processCpuSeconds", contract::case_cpu_seconds},
        {"processAddressSpaceMiB", contract::worker_address_space_mib}, {"primitiveProbeSamplesPerAxis", 3}});
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
        {"modelCapabilities", models},
        {"extensions", extensions}}).dump();
}
/** Return an input-stage error envelope without starting setup. */
PreviewResponse PreviewInputError(const std::string &message) {
    return {Json::object({{"schemaVersion", preview_schema_version}, {"kind", "initial-state-preview"},
        {"status", "error"}, {"stage", "input"}, {"identity", Json()}, {"state", Json()}, {"data", Json()},
        {"diagnostics", Json::array({diagnostic("error", "INVALID_REQUEST", message)})}}).dump(), 2};
}
} // namespace arch::api
