/**
 * @file Configuration.cpp
 * @brief Resolve a parameter file and serialize typed values, defaults and validation evidence.
 *
 * Workflow:
 * 1. Accept a bounded, verified request at the read-only API boundary.
 * 2. Resolve a parameter file and serialize typed values, defaults and validation evidence.
 * 3. Return typed evidence or an explicit error; do not start the simulation Driver.
 */

#include <algorithm>
#include <limits>
#include <sstream>

#include "api/Configuration.h"

#include "amr/topology/Morton.h"
#include "api/configuration/ParameterPresentation.h"
#include "api/protocol/LogCapture.h"
#include "api/protocol/Response.h"
#include "core/config/ConfigurationInput.h"
#include "core/config/RefinementSelection.h"
#include "core/files/FileFingerprint.h"
#include "driver/dispatch/PolicyDescriptor.h"

namespace arch::api {
namespace {
using detail::Json;
using config::ParameterDefinition;
template<class List> struct Options;
template<class Id, dispatch::UnknownPolicyBehavior Behavior, class... Registrations>
struct Options<dispatch::TypeList<Id, Behavior, Registrations...>> {
    static Json get() {
        auto values = Json::array();
        const auto add = [&]<class R>() {
            auto names = Json::array();
            for (auto name : dispatch::PolicyRegistration<R>::parse_names)
                names.push(std::string(name));
            const auto info = dispatch::describe_policy<R>();
            values.push(Json::object({{"value", std::string(info.canonical_name)},
                {"displayName", OptionDisplayName(info.canonical_name)}, {"acceptedNames", names}, {"cpuSupported", info.cpu_supported},
                {"cudaSupported", info.cuda_supported}}));
        };
        (add.template operator()<Registrations>(), ...);
        return Json::object({{"caseSensitive", false}, {"choices", values},
            {"unknownBehavior", "error"},
            {"availability", "registration-only; build and runtime requirements not checked"}});
    }
};
/** Build canonical enum values for a simple choice list. */
Json simple_options(std::initializer_list<const char*> names) {
    auto choices = Json::array();
    for (auto name : names) choices.push(Json::object({{"value", name}, {"displayName", OptionDisplayName(name)}, {"acceptedNames", Json::array({name})}}));
    return Json::object({{"caseSensitive", false}, {"choices", choices}, {"unknownBehavior", "error"}});
}
/** Expose accepted spellings and display names for one enum key. */
Json options(const std::string& key) {
    if (key == "hll_wave_speed") return simple_options({"roe", "davis"});
    if (key == "solver") return Options<dispatch::FluxPolicies>::get();
    if (key == "reconstruct") return Options<dispatch::ReconstructionPolicies>::get();
    if (key == "limiter") return Options<dispatch::LimiterPolicies>::get();
    if (key == "time_integrator") return Options<dispatch::TimeIntegratorPolicies>::get();
    if (key == "network_name") return Options<dispatch::NetworkPolicies>::get();
    if (key == "ode_solver") return Options<dispatch::OdeSolverPolicies>::get();
    if (key == "linear_solver") {
        auto result = Options<dispatch::LinearSolverPolicies>::get();
        result["choices"].push(Json::object({{"value", "auto"}, {"displayName", "Automatic"}, {"acceptedNames", Json::array({"auto"})}}));
        return result;
    }
    if (key == "diff_integrator") return Options<dispatch::DiffusionIntegratorPolicies>::get();
    if (key == "geometry") return simple_options({"cartesian", "cylindrical", "spherical"});
    if (key == "eos_type") return simple_options({"ideal", "helmholtz", "tabular"});
    if (key == "compute_backend") return simple_options({"cpu", "cuda", "auto"});
    if (key == "gravity_type") return simple_options({"none", "external", "self"});
    if (key == "gravity_boundary") return simple_options({"periodic", "isolated", "dirichlet", "neumann", "user"});
    if (key == "use_nse") return simple_options({"true", "false", "auto"});
    if (key.ends_with("_boundary_type")) return Json::object({{"caseSensitive", false}, {"unknownBehavior", "error"},
        {"choices", Json::array({
            Json::object({{"value", "user"}, {"displayName", "User primitive"}, {"acceptedNames", Json::array({"user", "inflow", "dirichlet"})}}),
            Json::object({{"value", "neumann"}, {"displayName", "Zero normal gradient"}, {"acceptedNames", Json::array({"neumann"})}}),
            Json::object({{"value", "outflow"}, {"displayName", "Outflow"}, {"acceptedNames", Json::array({"outflow"})}}),
            Json::object({{"value", "periodic"}, {"displayName", "Periodic"}, {"acceptedNames", Json::array({"periodic"})}}),
            Json::object({{"value", "reflect"}, {"displayName", "Reflecting"}, {"acceptedNames", Json::array({"reflect", "reflecting"})}})})}});
    return Json();
}
/** Describe the typed range and cross-field conditions of one parameter. */
Json constraints(const ParameterDefinition& d) {
    const std::string key(d.key);
    auto out = Json::object();
    if (d.type == "int") {
        out["storageMin"] = std::numeric_limits<int>::min();
        out["storageMax"] = std::numeric_limits<int>::max();
        out["syntax"] = "[+-]?[0-9]+";
    }
    if (d.type == "float") out["syntax"] = "finite decimal or scientific notation; full token";
    if (d.type == "expression") out["syntax"] = "number, pi, -pi, number*pi, pi*number, pi/number, exp(number); finite result";
    if (key == "nblockx1" || key == "regrid_interval") { out["min"] = 1; out["minInclusive"] = true; }
    if (key == "nblockx2" || key == "nblockx3" || key == "lrefinemin" || key == "lrefinemax" || key == "nseDensThreshold") {
        out["min"] = 0; out["minInclusive"] = true;
    }
    if (key == "jeans_cells") { out["min"] = 4; out["minInclusive"] = true; }
    if (key == "lrefinemin" || key == "lrefinemax") { out["max"] = amr::kMaxRefinementLevel; out["maxInclusive"] = true; }
    if (key == "eos_coulomb_mult" || key == "refine_threshold" || key == "derefine_threshold") {
        out["min"] = 0; out["max"] = 1; out["minInclusive"] = true; out["maxInclusive"] = true;
    }
    if (key == "sml_rho" || key == "min_eint" || key == "nseTempThreshold") {
        out["min"] = 0; out["minInclusive"] = false;
    }
    if (key == "dt_max") {
        out["disabledValue"] = -1;
        out["crossField"] = "Otherwise finite and at least dt_min";
    }
    out["complete"] = false;
    return out;
}
/** Classify path-valued parameters for client-side browsing. */
Json path_role(const std::string& key) {
    if (key != "eos_table_path" && key != "eos_helm_table_path" && key != "restart_file" && key != "out_dir" && key != "log_dir") return Json();
    return Json::object({{"role", (key == "out_dir" || key == "log_dir") ? "output-directory" : "input-file"},
        {"relativeTo", "process-working-directory"}, {"existenceChecked", false},
        {"checkOwner", "local-host"}, {"targetMayBeNew", key == "out_dir" || key == "log_dir"}});
}
/** Describe the CGS unit and symbol of one parameter. */
Json unit_info(const std::string& key, const std::string& system = "cgs") {
    if (key == "nuclearTempMin" || key == "smallt" || key == "nseTempThreshold")
        return Json::object({{"unit", "K"}, {"status", "known"}});
    if (key == "nuclearDensMin" || key == "nseDensThreshold")
        return Json::object({{"unit", "g/cm^3"}, {"status", "known"}});
    if (key == "gravity_atol") return Json::object({{"unit", "1/s^2"}, {"status", "known"}});
    if (key.starts_with("gravity_g_"))
        return Json::object({{"unit", system == "cgs" ? Json("cm/s^2") : Json()}, {"status", system == "unknown" ? "model-dependent" : "known"}});
    if (key == "nu_visc" || key == "alpha_therm" || key == "D_spec")
        return Json::object({{"unit", system == "cgs" ? Json("cm^2/s") : Json()}, {"status", system == "unknown" ? "model-dependent" : "known"}});
    if (key == "jeans_cells" || key == "eos_coulomb_mult" || key == "tstep_change_factor" || key == "gamma" || key == "smallx" || key == "cfl" || key == "diff_cfl"
        || key == "gravity_rtol" || key == "ode_rtol" || key == "refine_threshold" || key == "derefine_threshold"
        || key == "enucDtFactor" || key == "EntropyFixCoefficient" || key.starts_with("ode_dt_") || key == "ode_initial_dt_frac")
        return Json::object({{"unit", "1"}, {"status", "dimensionless"}});
    std::string field;
    if (key == "sml_rho") field = "DENS";
    if (key == "min_eint" || key == "max_eint") field = "EINT";
    if (!field.empty()) {
        const auto unit = FieldUnit(field, system);
        return Json::object({{"unit", unit.empty() ? Json() : Json(unit)}, {"status", system == "unknown" ? "model-dependent" : "known"}});
    }
    if (key == "tmax" || key == "plt_dt" || key == "chk_dt" || key == "dt_init" || key == "dt_min" || key == "dt_max")
        return Json::object({{"unit", system == "cgs" ? Json("s") : Json()}, {"status", system == "unknown" ? "model-dependent" : "known"}});
    if (key == "ode_atol")
        return Json::object({{"unit", Json()}, {"status", "mixed-state"},
            {"reason", "Absolute tolerance for temperature and abundance components; no single scalar unit."}});
    if (key.size() > 2 && key[0] == 'x' && (key.ends_with("_min") || key.ends_with("_max")))
        return Json::object({{"unit", Json()}, {"status", "coordinate-dependent"},
            {"axis", key.substr(0, 2)}});
    const auto type = config::Definition(key).type;
    if (type == "int") return Json::object({{"unit", "1"}, {"status", "dimensionless"}});
    if (type == "bool" || type == "string") return Json::object({{"unit", Json()}, {"status", "not-applicable"}});
    return Json::object({{"unit", Json()}, {"status", "not-specified"}});
}
/** Transport helpers serialize partial records without constructing SimConfig. */
Json strings(const std::vector<std::string>& values) {
    auto out = Json::array();
    for (const auto& value : values) out.push(value);
    return out;
}
Json scalar(const std::optional<config::InputValue>& value) {
    if (!value) return Json();
    return std::visit([](const auto& item) -> Json { return item; }, *value);
}
Json locations(const std::vector<ConfigSourceLocation>& values) {
    auto out = Json::array();
    for (const auto& value : values)
        out.push(Json::object({{"source", value.source}, {"line", std::int64_t(value.line)},
            {"column", std::int64_t(value.column)}, {"endColumn", std::int64_t(value.end_column)},
            {"rawValue", value.raw_value ? Json(*value.raw_value) : Json()}}));
    return out;
}
std::string condition_id(const ParameterDefinition& d, bool applicability = false) {
    if (d.requirement == config::RequirementKind::Optional)
        return applicability ? "consumes:" + std::string(d.key) : "optional";
    if (d.condition == config::InputCondition::Always) return "always";
    if (d.condition == config::InputCondition::Evolution) return "formal-evolution";
    return "requires:" + std::string(d.key);
}
Json condition_state(const std::string& id, const config::ConditionResult& state,
                     bool requirement = false) {
    auto result = Json::object({{"conditionId", id},
        {"state", !state.value ? "unknown-dependency" : *state.value ? "satisfied" : "not-applicable"},
        {"missingDependencies", strings(state.missing_dependencies)}});
    if (requirement) result["required"] = state.value ? Json(*state.value) : Json();
    return result;
}
Json condition_schema(const std::string& id, const std::vector<std::string>& deps,
                      const std::string& description) {
    return Json::object({{"id", id}, {"dependencies", strings(deps)}, {"description", description}});
}
// Dependency names document the Core predicate, including optional controlling
// inputs whose registered default would otherwise hide the dependency in an empty query.
Json standard_condition(const ParameterDefinition& d, bool applicability) {
    using C = config::InputCondition;
    std::vector<std::string> deps;
    switch (d.condition) {
    case C::Always: case C::Evolution: break;
    case C::Axis1: deps = {"nblockx1"}; break;
    case C::Axis2: deps = {"nblockx2"}; break;
    case C::Axis3: deps = {"nblockx3"}; break;
    case C::Limiter: deps = {"reconstruct"}; break;
    case C::Roe: case C::Hll: deps = {"solver"}; break;
    case C::Ideal: case C::Tabular: case C::Helm: deps = {"eos_type"}; break;
    case C::Network: deps = {"use_burn", "case:network-consumer"}; break;
    case C::Burn: deps = {"use_burn"}; break;
    case C::TemperatureFloor: deps = {"use_burn", "case:temperature-floor-consumer"}; break;
    case C::CompositionFloor: deps = {"use_burn", "case:composition-floor-consumer"}; break;
    case C::Nse: deps = {"use_burn", "use_nse", "network_name"}; break;
    case C::Diffusion: deps = {"use_diffusion"}; break;
    case C::Thermal: case C::Viscous: case C::Species:
        deps = {"use_diffusion", d.condition == C::Thermal ? "use_thermal_diff"
            : d.condition == C::Viscous ? "use_viscous_diff" : "use_species_diff",
            "eos_type", "material:constant-transport"}; break;
    case C::SelfGravity: case C::ExternalGravity: deps = {"gravity_type"}; break;
    case C::DynamicAmr: deps = {"lrefinemin", "lrefinemax"}; break;
    case C::CurvatureAmr: deps = {"lrefinemin", "lrefinemax", "refine_var"}; break;
    case C::JeansAmr: deps = {"refine_var"}; break;
    case C::Restart: deps = {"restart"}; break;
    }
    if (d.requirement == config::RequirementKind::Optional && applicability) {
        if (d.key == "cuda_device") deps = {"compute_backend"};
        else if (d.key == "eos_helm_table_path") deps = {"eos_type"};
        else if (d.key == "gravity_max_cycles") deps = {"gravity_type"};
        else if (d.key == "diff_max_stages") deps = {"use_diffusion"};
        else if (d.key == "EntropyFixCoefficient") deps = {"solver", "EntropyFix"};
        else if (d.key == "linear_solver" || d.key.starts_with("ode_")) {
            deps = {"use_burn"};
            if (d.key == "ode_max_newton_iter" || d.key == "ode_dt_safe_fac")
                deps.push_back("ode_solver");
        }
    }
    return condition_schema(condition_id(d, applicability), deps,
        applicability ? "Core-owned consumer condition; inspection evaluates supplied input."
                      : "Core-owned presence requirement; inspection evaluates supplied input.");
}
Json case_units(const std::string& unit, const std::string& type) {
    return Json::object({{"unit", unit.empty() ? Json() : Json(unit)},
        {"status", unit == "1" ? "dimensionless" : !unit.empty() ? "known"
                   : type == "string" || type == "bool" ? "not-applicable" : "not-specified"}});
}
Json record_json(const std::string& key, const config::InputRecord& record,
                 const Json& case_id, const std::string& type, const std::string& group,
                 const std::string& usage, const Json& units,
                 const std::string& requirement_id, const std::string& applicability_id,
                 const config::ConditionResult& applicable) {
    const char* state = record.state == config::InputState::Missing ? "missing"
        : record.state == config::InputState::Present ? "present"
        : record.state == config::InputState::Invalid ? "invalid" : "duplicate";
    Json source;
    if (record.source) {
        switch (*record.source) {
        case config::InputValueSource::Input: source = "input"; break;
        case config::InputValueSource::CaseDefined: source = "case-defined"; break;
        case config::InputValueSource::Derived: source = "derived"; break;
        case config::InputValueSource::DocumentedDefault: source = "documented-default"; break;
        }
    }
    Json raw;
    if (record.locations.size() == 1 && record.locations.front().raw_value)
        raw = *record.locations.front().raw_value;
    Json evidence;
    if (record.source && record.source_evidence)
        evidence = Json::object({{"owner", record.source_evidence->owner},
            {"dependencies", strings(record.source_evidence->dependencies)}});
    return Json::object({{"key", key}, {"caseId", case_id}, {"type", type},
        {"group", group}, {"usage", usage}, {"inputState", state},
        {"rawValue", raw}, {"locations", locations(record.locations)},
        {"parsedValue", scalar(record.parsed)}, {"resolvedValue", scalar(record.resolved)},
        {"valueSource", source}, {"sourceEvidence", evidence},
        {"valueStage", "configuration-resolution-before-setup"},
        {"requirement", condition_state(requirement_id, record.requirement, true)},
        {"applicability", condition_state(applicability_id, applicable)}, {"units", units},
        {"path", group == "Case" || group == "Composition" ? Json() : path_role(key)}});
}
Json coverage(bool cases, bool conditions, bool diagnostics = true) {
    return Json::object({{"standardParametersComplete", true},
        {"auxiliaryParametersComplete", true}, {"caseParametersComplete", cases},
        {"conditionsComplete", conditions}, {"diagnosticsComplete", diagnostics}});
}
Json case_schema(const config::CaseParameter& d, const std::string& case_id) {
    auto choices = Json::array();
    for (const auto& value : d.options)
        choices.push(Json::object({{"value", value}, {"acceptedNames", Json::array({value})},
            {"displayName", value}}));
    const auto predicate = condition_schema("case:" + case_id + ":" + d.key,
        d.requirement.missing_dependencies, "Registered model declaration; no Setup execution.");
    return Json::object({{"key", d.key}, {"caseId", case_id}, {"type", d.type},
        {"group", "Case"}, {"usage", d.usage}, {"units", case_units(d.unit, d.type)},
        {"requirement", Json::object({{"kind", d.requirement.value == true ? "required" : "conditional"},
            {"condition", predicate}})}, {"applicability", predicate},
        {"allowedDefault", Json()}, {"templateRecommendations", Json::array()},
        {"constraints", Json::object({{"complete", false}})}, {"path", Json()},
        {"options", d.options.empty() ? Json() : Json::object({
            {"caseSensitive", !d.options_ignore_case}, {"unknownBehavior", "error"}, {"choices", choices}})}});
}
// A display snapshot needs only resolved topology, not a default-filled SimConfig.
Json InspectionCoordinates(const config::StandardInputResolution& input) {
    const auto* geometry = config::input_detail::get<std::string>(input, "geometry");
    const auto* x1 = config::input_detail::get<int>(input, "nblockx1");
    const auto* x2 = config::input_detail::get<int>(input, "nblockx2");
    const auto* x3 = config::input_detail::get<int>(input, "nblockx3");
    if (!geometry || !x1 || !x2 || !x3 || *x1 < 1 || *x2 < 0 || *x3 < 0
        || (*x3 > 0 && *x2 == 0)) return Json();
    GridConfig grid;
    grid.geometry = *geometry;
    grid.nblockx1 = *x1; grid.nblockx2 = *x2; grid.nblockx3 = *x3;
    grid.dim = *x3 > 0 ? 3 : *x2 > 0 ? 2 : 1;
    return CoordinateMetadata(grid, "cgs");
}
} // namespace

/** Describe the versioned configuration boundary, not simulation readiness. */
Json ConfigurationExtensions() {
    const auto active = std::count_if(config::standard_parameters.begin(), config::standard_parameters.end(),
        [](const auto& d) { return d.requirement != config::RequirementKind::Retired; });
    return Json::object({{"version", contract::configuration_version}, {"schemaCommand", "--config-schema"},
        {"inspectCommand", "--inspect-config"}, {"coverage", "declared-configuration-before-setup"},
        {"presentationVersion", "1"}, {"standardParameterCount", std::int64_t(active)},
        {"customParameterCoverage", "registered-case-declarations"}});
}
Json ConfigurationSchema() {
    auto parameters = Json::array();
    auto retired = Json::array();
    for (const auto key : config::retired_input_keys) retired.push(std::string(key));
    for (const auto& d : config::standard_parameters) {
        const std::string key(d.key);
        if (d.requirement == config::RequirementKind::Retired) { retired.push(key); continue; }
        Json allowed;
        if (const auto* value = config::AllowedDefault(d))
            allowed = Json::object({{"value", scalar(config::input_detail::copy_default(*value))},
                {"source", "documented-default"}, {"evidence", "ConfigurationContractPlan:allowed-default:" + key}});
        parameters.push(Json::object({{"key", key}, {"caseId", Json()}, {"type", std::string(d.type)},
            {"group", std::string(d.group)}, {"usage", "simulation"}, {"presentation", ParameterPresentation(d)},
            {"allowedDefault", allowed}, {"templateRecommendations", Json::array()},
            {"requirement", Json::object({{"kind", d.requirement == config::RequirementKind::Required ? "required"
                : d.requirement == config::RequirementKind::Conditional ? "conditional" : "optional"},
                {"condition", standard_condition(d, false)}})},
            {"applicability", standard_condition(d, true)}, {"constraints", constraints(d)},
            {"options", options(key)}, {"path", path_role(key)}, {"units", unit_info(key)}}));
    }
    ConfigParser empty;
    const auto inputs = config::ResolveStandardInput(empty, {});
    auto cases = Json::array();
    bool complete = true;
    const auto& registry = ProblemRegistry::Get();
    for (const auto& name : registry.Names()) {
        const auto declaration = registry.DescribeConfiguration(name, inputs);
        const auto* registration = registry.Registration(name);
        auto items = Json::array();
        for (const auto& d : declaration.parameters) items.push(case_schema(d, name));
        complete &= declaration.complete;
        cases.push(Json::object({{"caseId", name}, {"source", registration->source_file},
            {"sourceSha256", registration->source_sha256}, {"parameters", items},
            {"declarationsComplete", declaration.complete},
            {"composition", Json::object({{"status", declaration.composition ? "selected-network-dependent" : "not-consumed"},
                {"inspectionRequired", bool(declaration.composition)}})}}));
    }
    auto coordinates = Json::array();
    for (const auto geometry : {"cartesian", "cylindrical", "spherical"}) {
        for (int dim = 1; dim <= 3; ++dim) {
            GridConfig grid; grid.geometry = geometry; grid.dim = dim;
            grid.nblockx2 = dim >= 2 ? 1 : 0; grid.nblockx3 = dim == 3 ? 1 : 0;
            coordinates.push(CoordinateMetadata(grid, "cgs"));
        }
    }
    auto fields = Json::array();
    for (const auto key : {"DENS", "TEMP", "PRES", "ENER", "EINT", "VELX", "VELY", "VELZ", "GPOT", "GACX", "GACY", "GACZ"})
        fields.push(Json::object({{"key", key}, {"cgs", FieldUnit(key, "cgs")}, {"code", FieldUnit(key, "code")}}));
    auto auxiliary = case_schema({"log_dir", "string", ""}, "");
    auxiliary["caseId"] = Json(); auxiliary["group"] = "Runtime";
    auxiliary["requirement"] = Json::object({{"kind", "optional"},
        {"condition", condition_schema("optional", {}, "May derive from resolved out_dir.")}});
    auxiliary["applicability"] = condition_schema("always", {}, "Main logging directory.");
    auxiliary["path"] = Json::object({{"role", "output-directory"}, {"relativeTo", "process-working-directory"},
        {"existenceChecked", false}, {"checkOwner", "local-host"}, {"targetMayBeNew", true}});
    return Json::object({{"schemaVersion", contract::schema_version}, {"version", contract::configuration_version},
        {"kind", "configuration-schema"}, {"status", "ok"}, {"parameters", parameters},
        {"auxiliaryParameters", Json::array({auxiliary})}, {"caseDeclarations", cases},
        {"caseDeclarationsComplete", complete}, {"retiredKeys", retired},
        {"standardParametersComplete", true}, {"constraintsComplete", false},
        {"coordinateSystems", coordinates}, {"fieldUnits", fields}, {"unitSystem", "cgs"},
        {"pathChecks", "local-host; no filesystem access in schema or inspection"}});
}

/** Analyze exact input bytes without executing Setup or constructing runtime defaults. */
PreviewResponse InspectConfiguration(const PreviewRequest& request) {
    ConfigParser parser;
    std::istringstream stream(request.config_text);
    parser.Read(stream, "stdin");
    const auto analysis = config::AnalyzeConfigurationInput(parser, request.case_id);
    auto parameters = Json::array();
    std::map<std::string, Json> units;
    std::map<std::string, std::string> types, groups, predicates;
    bool conditions_known = analysis.requirements_known();
    const auto add = [&](const std::string& key, const config::InputRecord& record,
                         const Json& case_id, const std::string& type, const std::string& group,
                         const std::string& usage, const Json& unit, const std::string& predicate,
                         const std::string& applicable_id, const config::ConditionResult& applicable) {
        parameters.push(record_json(key, record, case_id, type, group, usage, unit,
                                    predicate, applicable_id, applicable));
        units[key] = unit; types[key] = type; groups[key] = group; predicates[key] = predicate;
        conditions_known &= applicable.value.has_value();
    };
    for (const auto& [key, record] : analysis.standard.parameters) {
        const auto& d = *record.definition;
        add(key, record, Json(), std::string(d.type), std::string(d.group), "simulation", unit_info(key),
            condition_id(d), condition_id(d, true),
            config::InputApplicability(d, analysis.standard, analysis.declaration.consumers));
    }
    for (const auto& d : analysis.declaration.parameters) {
        const auto id = "case:" + request.case_id + ":" + d.key;
        add(d.key, analysis.model.parameters.at(d.key), request.case_id, d.type, "Case", d.usage,
            case_units(d.unit, d.type), id, id, d.requirement);
    }
    if (analysis.model.composition)
        for (const auto& [key, record] : analysis.model.composition->parameters)
            add(key, record, request.case_id, "float", "Composition", "simulation",
                case_units("1", "float"), "sparse-composition-member", "composition-consumer",
                {analysis.declaration.composition->consumes_input, {}});
    for (const auto& [key, record] : analysis.auxiliary)
        add(key, record, Json(), "string", "Runtime", "simulation", case_units("", "string"),
            "optional", "always", {true, {}});
    auto diagnostics = Json::array();
    bool missing = false, invalid = false;
    for (const auto& d : analysis.diagnostics) {
        missing |= d.code == "MISSING_PARAMETER";
        invalid |= d.code != "MISSING_PARAMETER";
        diagnostics.push(Json::object({{"code", d.code}, {"severity", "error"},
            {"parameterKey", d.key.empty() ? Json() : Json(d.key)},
            {"module", groups.contains(d.key) ? Json(groups.at(d.key)) : Json()},
            {"conditionId", predicates.contains(d.key) ? Json(predicates.at(d.key)) : Json()},
            {"message", d.message}, {"expected", Json::object({
                {"type", types.contains(d.key) ? Json(types.at(d.key)) : Json()},
                {"units", units.contains(d.key) ? units.at(d.key) : Json()}})},
            {"locations", locations(d.locations)}, {"relatedKeys", strings(d.related_keys)}}));
    }
    // A dependent missing requirement is not an error on the dependent value.
    // Preserve its unknown condition and identify what must be resolved first.
    const auto unresolved = [&](const auto& records) {
        for (const auto& [key, record] : records) {
            if (record.requirement.value) continue;
            diagnostics.push(Json::object({{"code", "UNRESOLVED_DEPENDENCY"}, {"severity", "error"},
                {"parameterKey", key}, {"module", groups.at(key)}, {"conditionId", predicates.at(key)},
                {"message", "Requirement cannot be evaluated until its dependencies are resolved."},
                {"expected", Json::object({{"type", types.at(key)}, {"units", units.at(key)}})},
                {"locations", locations(record.locations)},
                {"relatedKeys", strings(record.requirement.missing_dependencies)}}));
        }
    };
    unresolved(analysis.standard.parameters);
    unresolved(analysis.model.parameters);
    const bool complete = !missing && !invalid && conditions_known;
    const bool declarations = analysis.model.declarations_complete && analysis.ownership.complete;
    auto result = Json::object({{"schemaVersion", contract::schema_version},
        {"version", contract::configuration_version}, {"kind", "configuration-inspection"},
        {"status", complete ? "ok" : "error"},
        {"identity", Json::object({{"requestId", request.request_id}, {"caseId", request.case_id},
            {"configRevision", core::string_sha256(request.config_text)}})},
        {"coverage", coverage(declarations, conditions_known, analysis.ownership.complete)},
        {"completeness", Json::object({{"state", invalid ? "invalid" : missing ? "incomplete"
            : complete ? "complete" : "undetermined"}, {"scope", "declared-configuration-before-setup"}})},
        {"execution", Json::object({{"simulationReadiness", "not_checked"}, {"setup", "not_executed"},
            {"eos", "not_loaded"}, {"caseRegistration", "checked"}, {"caseDeclarations",
                ProblemRegistry::Get().Registration(request.case_id) ? "checked" : "not_checked"},
            {"filesystem", "not_accessed"}, {"cuda", "not_initialized"},
            {"validationStage", "conditional-resolution"}})},
        {"parameters", parameters}, {"diagnostics", diagnostics}, {"unitSystem", "cgs"},
        {"coordinates", InspectionCoordinates(analysis.standard)}});
    const auto* eos = config::input_detail::get<std::string>(analysis.standard, "eos_type");
    const auto* diffusion = config::input_detail::get<bool>(analysis.standard, "use_diffusion");
    result["diffusion"] = eos && diffusion ? DiffusionMetadata(*eos, *diffusion) : Json();
    result["amrIndicators"] = Json();
    if (analysis.refinement_selection) {
        const int x2 = *config::input_detail::get<int>(analysis.standard, "nblockx2");
        const int x3 = *config::input_detail::get<int>(analysis.standard, "nblockx3");
        const bool burn = *config::input_detail::get<bool>(analysis.standard, "use_burn");
        const auto* backend = config::input_detail::get<std::string>(analysis.standard,"compute_backend");
        const auto* geometry = config::input_detail::get<std::string>(analysis.standard,"geometry");
        result["amrIndicators"] = RefinementMetadata(
            *analysis.refinement_selection, x3 > 0 ? 3 : x2 > 0 ? 2 : 1, burn,
            config::input_detail::choice(analysis.standard,"gravity_type",{"self"}).value.value_or(false),
            backend && geometry && config::SupportsJeansBackend(*backend,*geometry));
    }
    return SerializePreviewResponse(result, complete ? 0 : 3);
}
} // namespace arch::api
