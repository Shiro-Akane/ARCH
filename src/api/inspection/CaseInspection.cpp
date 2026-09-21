#include "api/protocol/Progress.h"
#include "api/CaseInspection.h"
#include "api/configuration/ParameterMetadata.h"
#include "api/preview/StateSnapshot.h"
#include "api/preview/ResourceEstimates.h"
#include "amr/topology/Morton.h"
#include "api/protocol/LogCapture.h"
#include "api/protocol/Response.h"
#include "api/configuration/ValueDomain.h"
#include "core/files/FileFingerprint.h"
#include "core/config/RuntimeParams.h"
#include "physics/eos/eosdispatch.h"

namespace arch::api {
namespace {
using detail::Json;
struct SinkProbe final : preview::InitializationObserver {
    Json samples = Json::array();
    std::map<std::string, ValueDomain> domains;
    int count = 0;
    void initial_primitive(const PointCoords& point, const PrimitiveData& p) override {
        auto fields = Json::array();
        const auto add = [&](const std::string& key, double value, const std::string& unit, bool consumed = true) {
            if (consumed && !std::isfinite(value))
                throw std::runtime_error("Non-finite initialization sink: " + key);
            fields.push(Json::object({{"key", key}, {"value", value}, {"unit", unit}, {"consumedByConversion", consumed}}));
            if (consumed) domains[key].observe(value);
        };
        if (!(p.rho > 0) || !(p.has_temperature ? p.temperature > 0 : p.p > 0))
            throw std::runtime_error("Initialization density and selected pressure/temperature must be positive");
        add("DENS", p.rho, FieldUnit("DENS", "cgs"));
        add("PRES", p.p, FieldUnit("PRES", "cgs"), !p.has_temperature);
        add("TEMP", p.temperature, FieldUnit("TEMP", "cgs"), p.has_temperature);
        add("VELX", p.u, FieldUnit("VELX", "cgs"));
        add("VELY", p.v, FieldUnit("VELY", "cgs"));
        add("VELZ", p.w, FieldUnit("VELZ", "cgs"));
        auto fractions = Json::array();
        for (double x : p.mass_fractions) {
            if (!std::isfinite(x) || x < 0) throw std::runtime_error("Invalid initial mass fraction");
            fractions.push(x);
        }
        samples.push(Json::object({{"cartesianPosition", Json::array({point.x, point.y, point.z})},
            {"positionUnit", "cm"}, {"thermodynamicInput", p.has_temperature ? "temperature" : "pressure"},
            {"fields", fields}, {"massFractions", fractions}, {"massFractionUnit", "1"}}));
        ++count;
    }
    Json result() const {
        auto domain = Json::object();
        for (const auto& [key, value] : domains) domain[key] = value.json();
        return Json::object({{"kind", "initial-primitive-probe"}, {"sampleCount", count},
            {"samples", samples}, {"logDomains", domain}, {"completeFieldCoverage", false},
            {"sampling", "tensor product of native-axis quarter, midpoint and three-quarter positions; inactive coordinates follow Grid"},
            {"valueLocation", "actual Init output before EOS conversion; not field preview or cell average"},
            {"velocityBasis", "native orthonormal components"}});
    }
};
void validate_probe_domain(const SimConfig& c) {
    if (c.amr.lrefinemin < 0 || c.amr.lrefinemax < c.amr.lrefinemin || c.amr.lrefinemax > amr::kMaxRefinementLevel)
        throw std::invalid_argument("Require 0 <= lrefinemin <= lrefinemax <= 15");
    const double lo[] = {c.grid.x1_min, c.grid.x2_min, c.grid.x3_min};
    const double hi[] = {c.grid.x1_max, c.grid.x2_max, c.grid.x3_max};
    for (int i = 0; i < c.grid.dim; ++i)
        if (!std::isfinite(lo[i]) || !std::isfinite(hi[i]) || !(hi[i] > lo[i]) || !std::isfinite(hi[i]-lo[i]))
            throw std::invalid_argument("Probe requires finite ordered active-axis bounds");
    for (const auto& [key, value] : c.custom_params)
        if (!std::isfinite(value)) throw std::invalid_argument("Non-finite parameter: " + key);
}
} // namespace
PreviewResponse InspectCase(const PreviewRequest& request) {
    detail::CaptureLogs logs;
    auto out = Json::object({{"schemaVersion", contract::schema_version}, {"version", contract::initialization_version},
        {"kind", "case-inspection"}, {"status", "error"}, {"stage", "configuration"},
        {"identity", Json::object({{"caseId", request.case_id}, {"requestId", request.request_id},
            {"configRevision", core::string_sha256(request.config_text)}})},
        {"execution", Json::object({{"previewBackend", "cpu"}, {"cuda", "not_initialized"},
            {"timeStepping", "not_executed"}, {"driverScientificOutput", "not_created"},
            {"simulationReadiness", "not_checked"}, {"eosConversion", "not_executed"},
            {"setupMayLoadEos", true}, {"eosSourceConsistency", "not_checked"}})},
        {"state", Json::object({{"setup", "not_executed"}})}, {"data", Json()}, {"diagnostics", Json::array()}});
    auto reads = std::make_shared<preview::ParameterReadTrace>(std::set<std::string>{}, true);
    auto* registration = ProblemRegistry::Get().Registration(request.case_id);
    SimConfig config;
    SpeciesManager species;
    int code = 3;
    const char* error = "INVALID_CONFIGURATION";
    try {
        config = RuntimeParams::LoadText(request.config_text, reads);
        PublishStateSnapshot(out["state"], config);
        validate_probe_domain(config);
        code = 4; error = "UNSUPPORTED_CASE_INSPECTION"; ReportStage(request, out, "support");
        if (!registration) throw std::invalid_argument("Case is not registered in this binary");
        out["capability"] = CaseInspectionCapability(request.case_id, *registration);
        if (config.io.restart) throw std::invalid_argument("Initial inspection does not load restart checkpoints");
        EOSDispatcher::InspectionScope eos_sources;
        auto problem = ProblemRegistry::Get().Create(request.case_id);
        code = 5; error = "SETUP_FAILED"; ReportStage(request, out, "setup");
        out["state"]["setup"] = "error";
        out["state"]["eos"]["status"] = "setup-managed-not-inspected";
        problem->InspectSetup(config, species, reads);
        PublishStateSnapshot(out["state"], config);
        out["state"]["setup"] = "ready";
        out["state"]["species"] = SpeciesSnapshot(species);
        // Setup may use EOS helpers. The probe itself does not resolve an EOS;
        // do not mislabel the helper's cache as unloaded or runtime-ready.
        out["state"]["eos"]["status"] = "setup-managed-not-inspected";
        out["state"]["resources"] = AmrResourceMetadata(config, species.count());
        validate_probe_domain(config);
        code = 6; error = "INITIALIZATION_PROBE_FAILED"; ReportStage(request, out, "initialization");
        if (registration->point_initialization) {
            SinkProbe probe;
            const double lo[] = {config.grid.x1_min, config.grid.x2_min, config.grid.x3_min};
            const double hi[] = {config.grid.x1_max, config.grid.x2_max, config.grid.x3_max};
            const auto coordinate = [&](int axis, int index) {
                return axis < config.grid.dim ? lo[axis]+(hi[axis]-lo[axis])*(index+1)/4.0 : 0.0;
            };
            for (int k = 0; k < (config.grid.dim == 3 ? 3 : 1); ++k)
                for (int j = 0; j < (config.grid.dim >= 2 ? 3 : 1); ++j)
                    for (int i = 0; i < 3; ++i) {
                        PrimitiveData p;
                        p.mass_fractions.resize(species.count(), 0.0);
                        const auto point = Grid::PhysicalCoordsFromNative(config.grid.dim, config.grid.geometry,
                            coordinate(0, i), coordinate(1, j), coordinate(2, k));
                        problem->InspectInitialPrimitive(point, p, probe);
                    }
            out["data"] = probe.result();
        }
        ReportStage(request, out, "source-validation"); error = "EOS_SOURCE_CHANGED";
        eos_sources.validate();
        out["execution"]["eosSourceConsistency"] = "checked-after-probe";
        out["state"]["initializationProbe"] = registration->point_initialization ? "sampled" : "unavailable";
        ReportStage(request, out, "complete"); out["status"] = "ok"; code = 0;
    } catch (const std::exception& e) {
        out["data"] = Json();
        out["diagnostics"].push(Json::object({{"severity", "error"}, {"code", error}, {"message", e.what()}}));
    }
    config.parameter_reads.reset();
    if (registration) AddAuditedCaseUnits(request.case_id, *registration, config.grid.dim, *reads);
    PublishParameterMetadata(out, *reads, {}, false);
    out["parameterMetadata"]["coverage"] = "executed-setup-and-sampled-init-reads";
    out["parameterMetadata"]["automaticExpressionInference"] = false;
    auto unobserved = Json::array();
    for (const auto& [key, raw] : reads->raw_input()) {
        bool standard = false;
        for (const auto& definition : config::standard_parameters) if (definition.key == key) { standard = true; break; }
        if (!standard && !reads->reads().contains(key)) unobserved.push(key);
    }
    out["parameterMetadata"]["unobservedInputKeys"] = unobserved;
    out["parameterMetadata"]["unobservedMeaning"] = "not observed in this request; not proof of an unused key";
    if (!logs.info.text.empty()) out["diagnostics"].push(Json::object({{"severity", "info"}, {"code", "CORE_LOG"}, {"message", logs.info.message()}}));
    if (!logs.warning.text.empty()) out["diagnostics"].push(Json::object({{"severity", "warning"}, {"code", "CORE_LOG"}, {"message", logs.warning.message()}}));
    return SerializePreviewResponse(out, code);
}
} // namespace arch::api
