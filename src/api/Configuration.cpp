#include "Configuration.h"
#include "ParameterPresentation.h"
#include "LogCapture.h"
#include "Response.h"
#include "../core/RuntimeParams.h"
#include "../core/FileFingerprint.h"
#include "../driver/dispatch/PolicyDescriptor.h"
#include "../amr/Morton.h"
#include <algorithm>
#include <limits>
#include <sstream>

namespace arch::api {
namespace {
using detail::Json;
using config::ParameterDefinition;
Json default_json(const ParameterDefinition& definition) {
    return std::visit([](auto value) -> Json {
        if constexpr (std::is_same_v<decltype(value), std::string_view>) return std::string(value);
        else return value;
    }, definition.fallback);
}
Json diagnostic(const std::string& code, const std::string& key, const std::string& message,
                const char* severity = "error") {
    return Json::object({{"severity", severity}, {"code", code},
        {"parameterKey", key.empty() ? Json() : Json(key)}, {"message", message}});
}
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
            {"unknownBehavior", Behavior == dispatch::UnknownPolicyBehavior::UseDefault ? "core-fallback" : "error"},
            {"availability", "registration-only; build and runtime requirements not checked"}});
    }
};
Json simple_options(std::initializer_list<const char*> names) {
    auto choices = Json::array();
    for (auto name : names) choices.push(Json::object({{"value", name}, {"displayName", OptionDisplayName(name)}, {"acceptedNames", Json::array({name})}}));
    return Json::object({{"caseSensitive", false}, {"choices", choices}, {"unknownBehavior", "error"}});
}
Json options(const std::string& key) {
    if (key == "solver") return Options<dispatch::FluxPolicies>::get();
    if (key == "reconstruct") return Options<dispatch::ReconstructionPolicies>::get();
    if (key == "limiter") return Options<dispatch::LimiterPolicies>::get();
    if (key == "time_integrator" || key == "timeintegrator") return Options<dispatch::TimeIntegratorPolicies>::get();
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
    if (key == "gravity_type") {
        auto result = simple_options({"none", "external", "self"});
        result["unavailableValues"] = Json::array({"self"});
        result["unavailableReason"] = "Self gravity is not implemented.";
        return result;
    }
    if (key == "use_nse") return simple_options({"true", "false", "auto"});
    if (key.ends_with("_boundary_type")) return Json::object({{"caseSensitive", false}, {"unknownBehavior", "error"},
        {"choices", Json::array({
            Json::object({{"value", "outflow"}, {"displayName", "Outflow"}, {"acceptedNames", Json::array({"outflow"})}}),
            Json::object({{"value", "periodic"}, {"displayName", "Periodic"}, {"acceptedNames", Json::array({"periodic"})}}),
            Json::object({{"value", "reflect"}, {"displayName", "Reflecting"}, {"acceptedNames", Json::array({"reflect", "reflecting"})}})})}});
    return Json();
}
Json constraints(const ParameterDefinition& d) {
    const std::string key(d.key);
    auto out = Json::object();
    if (d.type == "int") {
        out["storageMin"] = std::numeric_limits<int>::min();
        out["storageMax"] = std::numeric_limits<int>::max();
        out["syntax"] = "[+-]?[0-9]+";
    }
    if (d.type == "float") out["syntax"] = "finite decimal or scientific notation; full token";
    if (d.type == "expression") out["syntax"] = "number, pi, -pi, number*pi, pi*number, pi/number; finite result";
    if (key == "nblockx1" || key == "regrid_interval") { out["min"] = 1; out["minInclusive"] = true; }
    if (key == "nblockx2" || key == "nblockx3" || key == "lrefinemin" || key == "lrefinemax" || key == "nseDensThreshold") {
        out["min"] = 0; out["minInclusive"] = true;
    }
    if (key == "lrefinemin" || key == "lrefinemax") { out["max"] = amr::kMaxRefinementLevel; out["maxInclusive"] = true; }
    if (key == "refine_threshold" || key == "derefine_threshold") {
        out["min"] = 0; out["max"] = 1; out["minInclusive"] = true; out["maxInclusive"] = true;
    }
    if (key == "sml_rho" || key == "min_eint" || key == "nseTempThreshold") {
        out["min"] = 0; out["minInclusive"] = false;
    }
    out["complete"] = false;
    return out;
}
Json path_role(const std::string& key) {
    if (key != "eos_table_path" && key != "eos_helm_table_path" && key != "restart_file" && key != "out_dir") return Json();
    return Json::object({{"role", key == "out_dir" ? "output-directory" : "input-file"},
        {"relativeTo", "process-working-directory"}, {"existenceChecked", false},
        {"checkOwner", "local-host"}, {"targetMayBeNew", key == "out_dir"}});
}
Json unit_info(const std::string& key, const std::string& system = "cgs") {
    if (key == "nuclearTempMin" || key == "smallt" || key == "nseTempThreshold")
        return Json::object({{"unit", "K"}, {"status", "known"}});
    if (key == "nuclearDensMin" || key == "nseDensThreshold")
        return Json::object({{"unit", "g/cm^3"}, {"status", "known"}});
    if (key == "gravity_G") return Json::object({{"unit", "cm^3/(g*s^2)"}, {"status", "known"}});
    if (key.starts_with("gravity_g_"))
        return Json::object({{"unit", system == "cgs" ? Json("cm/s^2") : system == "code" ? Json("code_acceleration") : Json()}, {"status", system == "unknown" ? "model-dependent" : "known"}});
    if (key == "nu_visc" || key == "alpha_therm" || key == "D_spec")
        return Json::object({{"unit", system == "cgs" ? Json("cm^2/s") : system == "code" ? Json("code_diffusivity") : Json()}, {"status", system == "unknown" ? "model-dependent" : "known"}});
    if (key == "gamma" || key == "smallx" || key == "cfl" || key == "diff_cfl"
        || key == "ode_rtol" || key == "refine_threshold" || key == "derefine_threshold"
        || key == "enucDtFactor" || key == "EntropyFixCoefficient" || key.starts_with("ode_dt_") || key == "ode_initial_dt_frac")
        return Json::object({{"unit", "1"}, {"status", "dimensionless"}});
    std::string field;
    if (key == "sml_rho") field = "DENS";
    if (key == "min_eint" || key == "max_eint") field = "EINT";
    if (!field.empty()) {
        const auto unit = FieldUnit(field, system);
        return Json::object({{"unit", unit.empty() ? Json() : Json(unit)}, {"status", system == "unknown" ? "model-dependent" : "known"}});
    }
    if (key == "tmax" || key == "plt_dt" || key == "chk_dt")
        return Json::object({{"unit", system == "cgs" ? Json("s") : system == "code" ? Json("code_time") : Json()}, {"status", system == "unknown" ? "model-dependent" : "known"}});
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
std::string condition(const ParameterDefinition& d) {
    const auto key = d.key;
    if (key == "timeintegrator") return "Alias of time_integrator; canonical key takes precedence.";
    if (key == "EntropyFixCoefficient") return "EntropyFix=true";
    if (key == "cuda_device") return "compute_backend=cuda or auto; no device probing in configuration inspection";
    if (key == "restart_file") return "restart=true";
    if (key == "eos_table_path") return "eos_type=helmholtz or tabular";
    if (key == "eos_helm_table_path") return "eos_type=tabular; need depends on table policy";
    if (key.starts_with("gravity_g_")) return "gravity_type=external";
    if (key == "gravity_G") return "gravity_type=self; unavailable in this build";
    if (d.group == "Diffusion" && key != "use_diffusion") {
        if (key == "nu_visc" || key == "alpha_therm" || key == "D_spec") return "use_diffusion=true; explicit coefficient forbidden with Helmholtz";
        return "use_diffusion=true";
    }
    if (d.group == "Network" && key != "use_burn" && key != "network_name") return "use_burn=true; thresholds are still parsed and validated when disabled";
    return "See parsed configuration; model Setup and simulation policies may impose further conditions.";
}
bool applicable(const ParameterDefinition& d, const SimConfig& c, const ConfigParser& p) {
    const auto key = d.key;
    if (key == "timeintegrator") return !p.HasKey("time_integrator");
    if (key == "EntropyFixCoefficient") return p.GetBool("EntropyFix", config::DefaultBool("EntropyFix"));
    if (key == "cuda_device") return c.execution.compute_backend != "cpu";
    if (key == "restart_file") return c.io.restart;
    if (key == "eos_table_path") return !dispatch::ascii_iequals(c.physics.eos_type, "ideal");
    if (key == "eos_helm_table_path") return dispatch::ascii_iequals(c.physics.eos_type, "tabular");
    if (key.starts_with("gravity_g_")) return c.physics.gravity.type == "external";
    if (key == "gravity_G") return false;
    if (d.group == "Diffusion" && key != "use_diffusion") {
        if (!c.physics.diffusion.use_diffusion) return false;
        if (key == "alpha_therm" || key == "nu_visc" || key == "D_spec") {
            if (dispatch::ascii_iequals(c.physics.eos_type, "helmholtz")) return false;
            if (key == "alpha_therm") return c.physics.diffusion.use_thermal_diffusion;
            if (key == "nu_visc") return c.physics.diffusion.use_viscous_diffusion;
            return c.physics.diffusion.use_species_diffusion;
        }
        return true;
    }
    if (d.group == "Network" && key != "use_burn" && key != "network_name") return c.physics.burn.use_burn;
    if (key.size() > 2 && key[0] == 'x' && key[1] >= '1' && key[1] <= '3') return key[1]-'0' <= c.grid.dim;
    return true;
}
Json input_value(const ParameterDefinition& d, const ConfigParser& p) {
    const std::string key(d.key);
    if (d.type == "int") return p.GetInt(key, config::DefaultInt(key));
    if (d.type == "float") return p.GetDouble(key, config::DefaultDouble(key));
    if (d.type == "bool") return p.GetBool(key, config::DefaultBool(key));
    if (d.type == "expression") {
        if (!p.HasKey(key) && key == "gravity_G") return config::DefaultDouble(key);
        return ConfigParser::ParseExpression(key, p.GetString(key, key == "gravity_G" ? "" : config::DefaultString(key)));
    }
    return p.GetString(key, config::DefaultString(key));
}
} // namespace

Json ConfigurationExtensions() {
    return Json::object({{"version", contract::configuration_version}, {"schemaCommand", "--config-schema"},
        {"inspectCommand", "--inspect-config"}, {"coverage", "standard-runtime-inputs"},
        {"presentationVersion", "1"}, {"standardParameterCount", std::int64_t(std::size(config::standard_parameters))}, {"customParameterCoverage", false}});
}
Json ConfigurationSchema() {
    auto parameters = Json::array();
    for (const auto& d : config::standard_parameters) {
        const std::string key(d.key);
        auto item = Json::object({{"key", key}, {"type", std::string(d.type)},
            {"group", std::string(d.group)}, {"presentation", ParameterPresentation(d)}, {"defaultValue", default_json(d)},
            {"defaultSource", "shared-runtime-definition"}, {"constraints", constraints(d)},
            {"options", options(key)}, {"path", path_role(key)}, {"units", unit_info(key)},
            {"applicability", condition(d)}});
        if (key == "timeintegrator") item["aliasOf"] = "time_integrator";
        parameters.push(std::move(item));
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
    for (const auto key : {"DENS", "TEMP", "PRES", "ENER", "EINT", "VELX", "VELY", "VELZ"})
        fields.push(Json::object({{"key", key}, {"cgs", FieldUnit(key, "cgs")}, {"code", FieldUnit(key, "code")}}));
    return Json::object({{"schemaVersion", contract::schema_version}, {"version", contract::configuration_version}, {"kind", "configuration-schema"}, {"status", "ok"},
        {"coverage", "standard-runtime-inputs"}, {"standardParametersComplete", true},
        {"customParametersComplete", false}, {"constraintsComplete", false},
        {"parameters", parameters}, {"coordinateSystems", coordinates}, {"fieldUnits", fields}, {"unitSystem", "cgs"}, {"legacyCodeUnits", "deprecated; no current preview selects them"},
        {"crossConstraints", Json::array({"x3 enabled requires x2 enabled", "active axis max > min", "0 <= lrefinemin <= lrefinemax <= 15", "0 <= derefine_threshold < refine_threshold <= 1", "max_eint >= min_eint", "Helmholtz diffusion forbids explicit alpha_therm/nu_visc/D_spec"})},
        {"pathChecks", "local-host; no filesystem access in schema or inspection"}});
}

PreviewResponse InspectConfiguration(const PreviewRequest& request) {
    detail::CaptureLogs logs;
    auto result = Json::object({{"schemaVersion", contract::schema_version}, {"version", contract::configuration_version}, {"kind", "configuration-inspection"},
        {"status", "error"}, {"identity", Json::object({{"requestId", request.request_id}, {"caseId", request.case_id},
            {"configRevision", core::string_sha256(request.config_text)}})},
        {"execution", Json::object({{"simulationReadiness", "not_checked"}, {"setup", "not_executed"},
            {"eos", "not_loaded"}, {"caseRegistration", "not_checked"}, {"filesystem", "not_accessed"}, {"cuda", "not_initialized"}})},
        {"parameters", Json::array()}, {"coordinates", Json()}, {"diagnostics", Json::array()}});
    int code = 3;
    try {
        ConfigParser parser;
        std::istringstream stream(request.config_text);
        parser.Load(stream);
        bool invalid = false;
        for (const auto& d : config::standard_parameters) {
            const std::string key(d.key);
            try {
                const auto value = input_value(d, parser);
                auto item = Json::object({{"key", key}, {"parsedValue", value},
                    {"valueStage", "typed-input-before-setup-and-policy-resolution"},
                    {"valueSource", parser.HasKey(key) ? "explicit" : "default"},
                    {"defaultValue", default_json(d)}, {"rawValue", parser.HasKey(key) ? Json(parser.GetString(key, "")) : Json()}});
                if (key == "time_integrator" && !parser.HasKey(key) && parser.HasKey("timeintegrator")) {
                    item["parsedValue"] = parser.GetString("timeintegrator", "");
                    item["valueSource"] = "alias"; item["sourceKey"] = "timeintegrator";
                }
                result["parameters"].push(std::move(item));
            } catch (const ConfigValueError& e) {
                invalid = true; result["diagnostics"].push(diagnostic(e.code, e.key, e.what()));
            }
        }
        if (invalid) return SerializePreviewResponse(result, code);
        const auto config = RuntimeParams::LoadText(request.config_text);
        const auto check_option = [&](const std::string& key, const auto& parsed) {
            if (!parsed.ok) throw ConfigValueError(key, "INVALID_OPTION", "Unknown registered option.");
            if (parsed.defaulted) result["diagnostics"].push(diagnostic("POLICY_FALLBACK", key,
                "Unknown option; the existing simulation policy parser selects its default. Input has not been rewritten.", "warning"));
        };
        check_option("compute_backend", dispatch::parse_compute_backend(config.execution.compute_backend));
        check_option("gravity_type", dispatch::parse_gravity(config.physics.gravity.type));
        check_option("solver", dispatch::parse_registered_policy<dispatch::FluxPolicies>(config.numerics.solver_name));
        check_option("reconstruct", dispatch::parse_registered_policy<dispatch::ReconstructionPolicies>(config.numerics.reconstruction));
        check_option("limiter", dispatch::parse_registered_policy<dispatch::LimiterPolicies>(config.numerics.limiter));
        check_option("time_integrator", dispatch::parse_registered_policy<dispatch::TimeIntegratorPolicies>(config.numerics.time_integrator));
        if (!dispatch::ascii_iequals(config.physics.eos_type, "ideal")
            && !dispatch::ascii_iequals(config.physics.eos_type, "helmholtz")
            && !dispatch::ascii_iequals(config.physics.eos_type, "tabular")) throw ConfigValueError("eos_type", "INVALID_OPTION", "Use ideal, helmholtz or tabular.");
        if (config.physics.burn.use_burn) {
            check_option("network_name", dispatch::parse_registered_policy<dispatch::NetworkPolicies>(config.physics.burn.network_name));
            check_option("ode_solver", dispatch::parse_registered_policy<dispatch::OdeSolverPolicies>(config.physics.burn.odeconfig.ode_solver));
            check_option("linear_solver", dispatch::parse_linear_solver_request(config.physics.burn.odeconfig.linear_solver));
        }
        if (config.physics.diffusion.use_diffusion)
            check_option("diff_integrator", dispatch::parse_registered_policy<dispatch::DiffusionIntegratorPolicies>(config.physics.diffusion.integrator));
        // These checks describe editable grid configuration, without constructing Grid/AMR.
        if (!dispatch::parse_geometry(config.grid.geometry).ok)
            throw ConfigValueError("geometry", "INVALID_OPTION", "Unknown coordinate geometry.");
        const int blocks[] = {config.grid.nblockx1, config.grid.nblockx2, config.grid.nblockx3};
        const double lo[] = {config.grid.x1_min, config.grid.x2_min, config.grid.x3_min};
        const double hi[] = {config.grid.x1_max, config.grid.x2_max, config.grid.x3_max};
        for (int i = 0; i < 3; ++i) {
            const auto axis = "x" + std::to_string(i+1);
            if (blocks[i] < (i == 0 ? 1 : 0)) throw ConfigValueError("nblock"+axis, "INVALID_RANGE", "Use positive blocks for an active axis, or 0 for an inactive second/third axis.");
            if (i < config.grid.dim) {
                for (const auto suffix : {"l_boundary_type", "r_boundary_type"}) {
                    const auto key = axis + suffix;
                    check_option(key, dispatch::parse_boundary(parser.GetString(key, config::DefaultString(key))));
                }
            }
            if (i < config.grid.dim && (!(hi[i] > lo[i]) || !std::isfinite(hi[i]-lo[i])))
                throw ConfigValueError(axis+"_max", "INVALID_RANGE", "Active axis maximum must exceed its minimum with a finite extent.");
        }
        if (config.amr.lrefinemin < 0 || config.amr.lrefinemax < config.amr.lrefinemin
            || config.amr.lrefinemax > amr::kMaxRefinementLevel)
            throw ConfigValueError("lrefinemax", "INVALID_RANGE", "Require 0 <= lrefinemin <= lrefinemax <= 15.");
        auto parameters = Json::array();
        for (const auto& d : config::standard_parameters) {
            const std::string key(d.key);
            auto value = input_value(d, parser);
            const bool alias = key == "time_integrator" && !parser.HasKey(key) && parser.HasKey("timeintegrator");
            if (alias) value = parser.GetString("timeintegrator", "");
            auto item = Json::object({{"key", key}, {"parsedValue", value},
                {"valueStage", "typed-input-before-setup-and-policy-resolution"},
                {"valueSource", alias ? "alias" : parser.HasKey(key) ? "explicit" : "default"},
                {"sourceKey", alias ? "timeintegrator" : key}, {"defaultValue", default_json(d)},
                {"rawValue", parser.HasKey(key) ? Json(parser.GetString(key, "")) : Json()},
                {"applicable", applicable(d, config, parser)}, {"applicabilityScope", "configured-modules; model usage not traced"},
                {"units", unit_info(key, UnitSystem(config))}, {"path", path_role(key)}});
            parameters.push(std::move(item));
        }
        result["parameters"] = parameters;
        result["coordinates"] = CoordinateMetadata(config.grid, UnitSystem(config));
        result["unitSystem"] = UnitSystem(config);
        result["diffusion"] = DiffusionMetadata(config);
        result["amrIndicators"] = RefinementMetadata(config);
        result["resolved"] = Json::object({{"geometry", config.grid.geometry}, {"dimension", config.grid.dim},
            {"timeIntegrator", config.numerics.time_integrator}, {"eosRequested", config.physics.eos_type},
            {"burnEnabled", config.physics.burn.use_burn}, {"networkRequested", config.physics.burn.network_name},
            {"nseAutoRequested", config.physics.burn.nse_auto}, {"gravityType", config.physics.gravity.type},
            {"diffusionEnabled", config.physics.diffusion.use_diffusion}, {"restart", config.io.restart}});
        auto unknown = Json::array();
        for (const auto& [key, raw] : parser.GetAllParams()) {
            const bool known = std::any_of(config::standard_parameters.begin(), config::standard_parameters.end(),
                [&](const auto& d) { return d.key == key; });
            if (!known) unknown.push(Json::object({{"key", key}, {"rawValue", raw}, {"type", Json()}, {"unit", Json()}, {"status", "uninspected-model-parameter"}}));
        }
        result["customParameters"] = unknown;
        if (config.physics.gravity.type == "self")
            result["diagnostics"].push(diagnostic("UNAVAILABLE_MODULE", "gravity_type", "Self gravity is not implemented; configuration inspection does not establish simulation readiness.", "warning"));
        result["status"] = "ok"; code = 0;
    } catch (const ConfigValueError& e) {
        result["diagnostics"].push(diagnostic(e.code, e.key, e.what()));
    } catch (const std::exception& e) {
        result["diagnostics"].push(diagnostic("INVALID_CONFIGURATION", "", e.what()));
    }
    if (!logs.warning.text.empty()) result["diagnostics"].push(diagnostic("CORE_LOG", "", logs.warning.message(), "warning"));
    return SerializePreviewResponse(result, code);
}
} // namespace arch::api
