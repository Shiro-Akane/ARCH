/**
 * @file InputResolution.h
 * @brief Resolve explicit standard input and requirement dependencies without resources.
 *
 * This stage does not construct SimConfig, execute Setup, or certify ranges and
 * case declarations. Runtime construction and API inspection consume its records.
 */
#pragma once

#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "core/config/StandardParameters.h"
#include "core/config/ScalarControlValidation.h"
#include "core/config/ControlRelations.h"
#include "driver/dispatch/PolicyDescriptor.h"

namespace arch::config {
using InputValue = std::variant<int, double, bool, std::string>;
enum class InputState { Missing, Present, Invalid, Duplicate };
enum class InputValueSource { Input, CaseDefined, Derived, DocumentedDefault };
enum class ConfigurationPurpose { Evolution, InitialState };

// Supplied by registered case/closure declarations, never by a browser guess.
struct InputContext {
    ConfigurationPurpose purpose = ConfigurationPurpose::Evolution;
    std::optional<bool> needs_network;
    std::optional<bool> needs_temperature_floor;
    std::optional<bool> needs_composition_floor;
    std::optional<bool> constant_thermal, constant_viscous, constant_species;
};
struct ConditionResult {
    std::optional<bool> value;
    std::vector<std::string> missing_dependencies;
};
struct InputSourceEvidence {
    bool operator==(const InputSourceEvidence&) const = default;
    std::string owner;
    std::vector<std::string> dependencies;
};
// A registered model can supply an absent standard input before Setup.
// These are model definitions/derivations, not another runtime default catalog.
struct ModelInputValue {
    bool operator==(const ModelInputValue&) const = default;
    std::string key;
    InputValue value;
    InputValueSource source;
    InputSourceEvidence evidence;
};
struct InputRecord {
    const ParameterDefinition* definition = nullptr;
    InputState state = InputState::Missing;
    std::optional<InputValue> parsed;
    std::optional<InputValue> resolved;
    std::optional<InputValueSource> source;
    std::optional<InputSourceEvidence> source_evidence;
    ConditionResult requirement;
    std::vector<ConfigSourceLocation> locations;
};
struct StandardInputResolution {
    std::map<std::string, InputRecord> parameters;
    std::vector<ConfigInputDiagnostic> diagnostics;

    // Only typed input and mandatory presence; never full simulation readiness.
    bool requirements_satisfied() const {
        if (!diagnostics.empty()) return false;
        for (const auto& [key, record] : parameters)
            if (!record.requirement.value.has_value()) return false;
        return true;
    }
};

namespace input_detail {
inline InputValue copy_default(const DefaultValue& value) {
    return std::visit([](const auto& v) -> InputValue {
        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, std::string_view>)
            return std::string(v);
        else return v;
    }, value);
}
inline ConditionResult known(bool value) { return {value, {}}; }
inline ConditionResult unknown(std::string key) { return {std::nullopt, {std::move(key)}}; }
inline ConditionResult combine(ConditionResult a, ConditionResult b, bool conjunction) {
    const bool decisive = !conjunction;
    if ((a.value && *a.value == decisive) || (b.value && *b.value == decisive))
        return known(decisive);
    if (a.value && b.value) return known(conjunction);
    for (const auto& key : b.missing_dependencies)
        if (std::find(a.missing_dependencies.begin(), a.missing_dependencies.end(), key)
            == a.missing_dependencies.end()) a.missing_dependencies.push_back(key);
    a.value.reset();
    return a;
}
template<class T>
const T* get(const StandardInputResolution& result, const char* key) {
    const auto it = result.parameters.find(key);
    if (it == result.parameters.end() || !it->second.resolved) return nullptr;
    return std::get_if<T>(&*it->second.resolved);
}
inline ConditionResult flag(const StandardInputResolution& result, const char* key) {
    if (const auto* value = get<bool>(result, key)) return known(*value);
    return unknown(key);
}
inline ConditionResult choice(const StandardInputResolution& result, const char* key,
                              std::initializer_list<std::string_view> accepted) {
    const auto* value = get<std::string>(result, key);
    if (!value) return unknown(key);
    for (const auto token : accepted)
        if (dispatch::ascii_iequals(*value, token)) return known(true);
    return known(false);
}
inline ConditionResult case_need(std::optional<bool> value, const char* owner) {
    return value ? known(*value) : unknown(owner);
}
inline ConditionResult condition(InputCondition id, const StandardInputResolution& values,
                                 const InputContext& context) {
    using C = InputCondition;
    const auto burn = [&] { return flag(values, "use_burn"); };
    const auto diffusion = [&] { return flag(values, "use_diffusion"); };
    switch (id) {
    case C::Always: return known(true);
    case C::Evolution: return known(context.purpose == ConfigurationPurpose::Evolution);
    case C::Axis1: case C::Axis2: case C::Axis3: {
        const char* key = id == C::Axis1 ? "nblockx1" : id == C::Axis2 ? "nblockx2" : "nblockx3";
        const auto* blocks = get<int>(values, key);
        return blocks ? known(*blocks > 0) : unknown(key);
    }
    case C::Limiter: {
        const auto* text = get<std::string>(values, "reconstruct");
        if (!text) return unknown("reconstruct");
        const auto selected = dispatch::parse_registered_policy<dispatch::ReconstructionPolicies>(*text);
        if (!selected.ok) return unknown("reconstruct");
        return known(selected.value == dispatch::ReconstructionId::Muscl);
    }
    case C::Roe: return choice(values, "solver", {"roe"});
    case C::Hll: return choice(values, "solver", {"hll", "hllc"});
    case C::Ideal: return choice(values, "eos_type", {"ideal"});
    case C::Tabular: return choice(values, "eos_type", {"tabular"});
    case C::Helm: return choice(values, "eos_type", {"helmholtz"});
    case C::Network:
        return combine(burn(), case_need(context.needs_network, "case:network-consumer"), false);
    case C::Burn: return burn();
    case C::TemperatureFloor:
        return combine(burn(), case_need(context.needs_temperature_floor, "case:temperature-floor-consumer"), false);
    case C::CompositionFloor:
        return combine(burn(), case_need(context.needs_composition_floor, "case:composition-floor-consumer"), false);
    case C::Nse: {
        ConditionResult nse = unknown("use_nse");
        if (const auto* request = get<std::string>(values, "use_nse")) {
            if (dispatch::ascii_iequals(*request, "auto")) {
                if (const auto* network = get<std::string>(values, "network_name")) {
                    const auto selected = dispatch::parse_registered_policy<dispatch::NetworkPolicies>(*network);
                    nse = selected.ok ? known(dispatch::network_supports_nse(selected.value)) : unknown("network_name");
                } else nse = unknown("network_name");
            } else nse = known(dispatch::ascii_iequals(*request, "true"));
        }
        return combine(burn(), nse, true);
    }
    case C::Diffusion: return diffusion();
    case C::Thermal: case C::Viscous: case C::Species: {
        const char* key = id == C::Thermal ? "use_thermal_diff"
                        : id == C::Viscous ? "use_viscous_diff" : "use_species_diff";
        // Helmholtz forbids overrides; IdealGas has no stellar electron state.
        // Tabular completion can supply stellar transport, so use its declaration.
        auto material = unknown("eos_type");
        if (const auto* eos = get<std::string>(values, "eos_type")) {
            if (dispatch::ascii_iequals(*eos, "ideal")) material = known(true);
            else if (dispatch::ascii_iequals(*eos, "helmholtz")) material = known(false);
            else material = case_need(id == C::Thermal ? context.constant_thermal
                                    : id == C::Viscous ? context.constant_viscous
                                    : context.constant_species, "material:constant-transport");
        }
        return combine(combine(diffusion(), flag(values, key), true), material, true);
    }
    case C::SelfGravity: return choice(values, "gravity_type", {"self"});
    case C::ExternalGravity: return choice(values, "gravity_type", {"external"});
    case C::DynamicAmr: case C::CurvatureAmr: {
        const auto* low = get<int>(values, "lrefinemin");
        const auto* high = get<int>(values, "lrefinemax");
        if (!low || !high) {
            auto result = unknown(!low ? "lrefinemin" : "lrefinemax");
            if (!low && !high) result.missing_dependencies.push_back("lrefinemax");
            return result;
        }
        if (*high == *low) return known(false);
        if (*high < *low) return unknown("lrefinemax");
        if (id == C::DynamicAmr) return known(true);
        const auto* text = get<std::string>(values, "refine_var");
        if (!text) return unknown("refine_var");
        // Named species validity belongs to the registered species declaration.
        // JENS is the only non-curvature indicator and is implemented separately.
        std::string tokens = *text;
        std::replace(tokens.begin(), tokens.end(), '+', ',');
        std::istringstream stream(tokens);
        std::string token;
        while (std::getline(stream, token, ',')) {
            const auto first = token.find_first_not_of(" \t");
            if (first == std::string::npos) continue;
            token = token.substr(first, token.find_last_not_of(" \t") - first + 1);
            if (!dispatch::ascii_iequals(token, "JENS")) return known(true);
        }
        return known(false);
    }
    case C::JeansAmr: {
        const auto* text=get<std::string>(values,"refine_var");
        if (!text) {
            // refine_var is conditional on dynamic AMR; an absent selector
            // in a fixed-level configuration does not request JENS.
            const auto dynamic=condition(C::DynamicAmr,values,context);
            return dynamic.value && !*dynamic.value ? known(false) : unknown("refine_var");
        }
        std::string tokens=*text;
        std::replace(tokens.begin(),tokens.end(),'+',',');
        std::istringstream stream(tokens);std::string token;
        while (std::getline(stream,token,',')) {
            const auto first=token.find_first_not_of(" \t");
            if (first==std::string::npos) continue;
            token=token.substr(first,token.find_last_not_of(" \t")-first+1);
            if (dispatch::ascii_iequals(token,"JENS")) return known(true);
        }
        return known(false);
    }
    case C::Restart: return flag(values, "restart");
    }
    throw std::logic_error("Unregistered input condition");
}

inline bool valid_option(const std::string& key, const std::string& text) {
    using namespace dispatch;
    if (key == "solver") return parse_registered_policy<FluxPolicies>(text).ok;
    if (key == "reconstruct") return parse_registered_policy<ReconstructionPolicies>(text).ok;
    if (key == "limiter") return parse_registered_policy<LimiterPolicies>(text).ok;
    if (key == "time_integrator") return parse_registered_policy<TimeIntegratorPolicies>(text).ok;
    if (key == "network_name") return parse_registered_policy<NetworkPolicies>(text).ok;
    if (key == "ode_solver") return parse_registered_policy<OdeSolverPolicies>(text).ok;
    if (key == "linear_solver") return parse_linear_solver_request(text).ok;
    if (key == "diff_integrator") return parse_registered_policy<DiffusionIntegratorPolicies>(text).ok;
    if (key == "compute_backend") return parse_compute_backend(text).ok;
    if (key == "geometry") return parse_geometry(text).ok;
    if (key == "gravity_type") return parse_gravity(text).ok;
    if (key.ends_with("_boundary_type")) return parse_boundary(text).ok;
    if (key == "eos_type") return registration_matches<Tabular3DPolicy>(text)
        || parse_registered_policy<EosPolicies>(text).ok;
    if (key == "gravity_boundary") return ascii_iequals(text, "periodic") || ascii_iequals(text, "isolated");
    if (key == "use_nse") return ascii_iequals(text, "true") || ascii_iequals(text, "false") || ascii_iequals(text, "auto");
    if (key == "hll_wave_speed") return ascii_iequals(text, "roe") || ascii_iequals(text, "davis");
    return true;
}
// Read the same valid snapshot for every relation. Invalidate participants
// afterwards so one failed relation does not suppress another independent error.
inline void validate_relations(StandardInputResolution& result) {
    std::vector<ConfigInputDiagnostic> failures;
    const auto fail = [&](const std::string& key, const char* message,
                          std::vector<std::string> dependencies) {
        failures.push_back({key, "INVALID_RANGE", message,
            result.parameters.at(key).locations, std::move(dependencies)});
    };
    const auto pair = [&](const char* key, const char* other, auto valid, const char* message) {
        const auto* value = get<double>(result, key);
        const auto* dependency = get<double>(result, other);
        if (value && dependency && !valid(*value, *dependency))
            fail(key, message, {other});
    };
    pair("max_eint", "min_eint", relations::AtLeast, "Must be at least min_eint.");
    pair("dt_init", "dt_min", relations::AtLeast, "Must be at least dt_min.");
    pair("dt_max", "dt_min", relations::TimeCap, "Use -1 for no cap, or a finite cap at least dt_min.");
    pair("refine_threshold", "derefine_threshold", relations::CurvatureThresholds,
         "Require 0 <= derefine_threshold < refine_threshold <= 1.");
    const auto* solver = get<std::string>(result, "solver");
    const auto* speed = get<std::string>(result, "hll_wave_speed");
    if (solver && speed && !relations::HllSpeed(*solver, dispatch::ascii_iequals(*speed, "roe")))
        fail("hll_wave_speed", "A nondefault signal-speed estimator requires HLL or HLLC.", {"solver"});
    const auto* eos = get<std::string>(result, "eos_type");
    const auto* coulomb = get<double>(result, "eos_coulomb_mult");
    if (eos && coulomb && !relations::Coulomb(*coulomb, *eos))
        fail("eos_coulomb_mult", "A nondefault Coulomb factor requires eos_type=helmholtz.", {"eos_type"});
    const auto* second = get<int>(result, "nblockx2");
    const auto* third = get<int>(result, "nblockx3");
    if (second && third && !relations::AxisTopology(*second, *third))
        fail("nblockx3", "Third axis requires an active second axis.", {"nblockx2"});
    const auto* lower = get<int>(result, "lrefinemin");
    const auto* upper = get<int>(result, "lrefinemax");
    if (lower && upper && !relations::RefinementLevels(*lower, *upper))
        fail("lrefinemax", "Require ordered refinement levels within the Morton range.", {"lrefinemin"});
    for (int axis = 1; axis <= 3; ++axis) {
        const auto name = "x" + std::to_string(axis);
        const auto count_key = "nblock" + name;
        const auto min_key = name + "_min", max_key = name + "_max";
        const auto* count = get<int>(result, count_key.c_str());
        const auto* min = get<double>(result, min_key.c_str());
        const auto* max = get<double>(result, max_key.c_str());
        if (count && *count > 0 && min && max && !relations::ActiveExtent(*min, *max))
            fail(max_key, "Active axis must have finite positive extent.", {count_key, min_key});
    }
    if (choice(result, "gravity_type", {"self"}).value == true) {
        relations::GravityTopology topology;
        const auto text = [&](const char* key) -> std::optional<std::string> {
            const auto* raw = get<std::string>(result, key);
            if (!raw) return {};
            std::string value = *raw;
            std::transform(value.begin(), value.end(), value.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        };
        topology.geometry = text("geometry");
        topology.boundary = text("gravity_boundary");
        const auto* first = get<int>(result, "nblockx1");
        if (first && second && third && relations::AxisTopology(*second, *third))
            topology.dimension = *second == 0 ? 1 : *third == 0 ? 2 : 3;
        for (int axis = 0; axis < 3; ++axis) {
            const auto name = "x" + std::to_string(axis + 1);
            if (const auto* v = get<double>(result, (name + "_min").c_str())) topology.lower[axis] = *v;
            if (const auto* v = get<double>(result, (name + "_max").c_str())) topology.upper[axis] = *v;
            topology.faces[2 * axis] = text((name + "l_boundary_type").c_str());
            topology.faces[2 * axis + 1] = text((name + "r_boundary_type").c_str());
        }
        relations::CheckGravityTopology(topology, fail);
    }
    for (auto& error : failures) {
        auto& record = result.parameters.at(error.key);
        record.state = InputState::Invalid;
        record.resolved.reset();
        record.source.reset();
        record.source_evidence.reset();
        result.diagnostics.push_back(std::move(error));
    }
}
} // namespace input_detail

inline StandardInputResolution ResolveStandardInput(const ConfigParser& parser,
                                                     const InputContext& context,
                                                     const std::vector<ModelInputValue>& model_values = {}) {
    StandardInputResolution result;
    result.diagnostics = parser.Diagnostics();
    for (const auto& definition : standard_parameters) {
        const std::string key(definition.key);
        if (definition.requirement == RequirementKind::Retired) {
            if (parser.HasKey(key))
                result.diagnostics.push_back({key, "RETIRED_PARAMETER",
                    "This input is retired; remove it explicitly.", parser.Locations(key)});
            continue;
        }
        InputRecord record;
        record.definition = &definition;
        record.locations = parser.Locations(key);
        if (record.locations.size() > 1) record.state = InputState::Duplicate;
        else if (parser.HasKey(key)) {
            record.state = InputState::Present;
            try {
                const auto raw = parser.GetString(key, "");
                if (definition.type == "int") record.parsed = ConfigParser::ParseInteger(key, raw);
                else if (definition.type == "float") record.parsed = ConfigParser::ParseNumber(key, raw);
                else if (definition.type == "expression") record.parsed = ConfigParser::ParseExpression(key, raw);
                else if (definition.type == "bool") record.parsed = parser.GetBool(key, false);
                else record.parsed = raw;
                if (definition.type == "string" && !input_detail::valid_option(key, raw))
                    throw ConfigValueError(key, "INVALID_OPTION", "Unknown registered option.");
                if ((key == "nblockx1" || key == "nblockx2" || key == "nblockx3")
                    && std::get<int>(*record.parsed) < (key == "nblockx1" ? 1 : 0))
                    throw ConfigValueError(key, "INVALID_RANGE", "Invalid active-axis block count.");
                record.resolved = record.parsed;
                record.source = InputValueSource::Input;
            } catch (const ConfigValueError& error) {
                record.state = InputState::Invalid;
                if (error.code != "INVALID_RANGE") record.parsed.reset();
                result.diagnostics.push_back({key, error.code, error.what(), record.locations});
            }
        } else if (const auto* value = AllowedDefault(definition)) {
            record.resolved = input_detail::copy_default(*value);
            record.source = InputValueSource::DocumentedDefault;
            record.source_evidence = InputSourceEvidence{
                "ConfigurationContractPlan:allowed-default:" + key, {}};
        }
        result.parameters.emplace(key, std::move(record));
    }
    std::set<std::string> provided;
    for (const auto& model : model_values) {
        const auto found = result.parameters.find(model.key);
        const auto reject = [&](const std::string& reason) {
            result.diagnostics.push_back({model.key, "INVALID_MODEL_PROVISION", reason, {}});
        };
        if (!provided.insert(model.key).second) {
            reject("More than one model provision names this input.");
            continue;
        }
        if (found == result.parameters.end()) {
            reject("Model provision must name an active standard parameter.");
            continue;
        }
        const auto& type = found->second.definition->type;
        const bool type_ok = (type == "int" && std::holds_alternative<int>(model.value))
            || ((type == "float" || type == "expression") && std::holds_alternative<double>(model.value))
            || (type == "bool" && std::holds_alternative<bool>(model.value))
            || (type == "string" && std::holds_alternative<std::string>(model.value));
        if (!type_ok || model.evidence.owner.empty()
            || (model.source != InputValueSource::CaseDefined && model.source != InputValueSource::Derived)
            || (model.source == InputValueSource::Derived && model.evidence.dependencies.empty())) {
            reject("Model provision requires the registered type and named case-defined/derived evidence.");
            continue;
        }
        // Explicit input remains authoritative, including explicit invalid input.
        // A declaration must never turn a bad token into a model-supplied value.
        if (parser.HasKey(model.key)) continue;
        auto& record = found->second;
        record.resolved = model.value;
        record.source = model.source;
        record.source_evidence = model.evidence;
        const auto* word = std::get_if<std::string>(&model.value);
        const auto* number = std::get_if<double>(&model.value);
        const auto* integer = std::get_if<int>(&model.value);
        if ((word && !input_detail::valid_option(model.key, *word))
            || (number && !std::isfinite(*number))
            || (integer && (model.key == "nblockx1" || model.key == "nblockx2" || model.key == "nblockx3")
                && *integer < (model.key == "nblockx1" ? 1 : 0))) {
            record.state = InputState::Invalid;
            record.resolved.reset();
            record.source.reset();
            reject("Model-provided value violates its option, finite value or topology contract.");
        }
    }
    for (auto& [key, record] : result.parameters) {
        if (!record.resolved) continue;
        const auto number = std::visit([](const auto& value) -> std::optional<double> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, int> || std::is_same_v<T, double>) return value;
            else return {};
        }, *record.resolved);
        if (!number) continue;
        if (const auto* error = ScalarControlError(key, *number)) {
            record.state = InputState::Invalid;
            record.resolved.reset();
            record.source.reset();
            result.diagnostics.push_back({key, "INVALID_RANGE", error, record.locations});
        }
    }
    input_detail::validate_relations(result);
    for (const auto retired : retired_input_keys) {
        const std::string key(retired);
        if (parser.HasKey(key))
            result.diagnostics.push_back({key, "RETIRED_PARAMETER",
                "This input is retired; remove it explicitly.", parser.Locations(key)});
    }
    if (input_detail::flag(result, "use_diffusion").value == true
        && input_detail::choice(result, "eos_type", {"helmholtz"}).value == true) {
        for (const auto key : {"alpha_therm", "nu_visc", "D_spec"})
            if (parser.HasKey(key)) {
                auto& record = result.parameters.at(key);
                record.state = InputState::Invalid;
                record.resolved.reset();
                record.source.reset();
                result.diagnostics.push_back({key, "INAPPLICABLE_PARAMETER",
                    "Helmholtz transport forbids explicit constant coefficients.", record.locations});
            }
    }
    // Reject unresolved or cyclic derivation evidence after scalar/relational
    // checks. Dependents cannot keep a value whose prerequisite was invalidated.
    std::map<std::string, int> visited;
    const auto dependencies_ready = [&](auto&& self, const std::string& key) -> bool {
        const auto found = result.parameters.find(key);
        if (found == result.parameters.end() || !found->second.resolved) return false;
        auto& state = visited[key];
        if (state == 1) return false;
        if (state >= 2) return state == 2;
        state = 1;
        const auto& record = found->second;
        bool valid = true;
        if (record.source_evidence
            && (record.source == InputValueSource::Derived || record.source == InputValueSource::CaseDefined)) {
            for (const auto& dependency : record.source_evidence->dependencies)
                if (!self(self, dependency)) valid = false;
        }
        state = valid ? 2 : 3;
        return valid;
    };
    std::vector<std::string> invalid_dependencies;
    for (const auto& [key, record] : result.parameters)
        if ((record.source == InputValueSource::Derived || record.source == InputValueSource::CaseDefined)
            && !dependencies_ready(dependencies_ready, key)) invalid_dependencies.push_back(key);
    for (const auto& key : invalid_dependencies) {
        auto& record = result.parameters.at(key);
        record.state = InputState::Invalid;
        record.resolved.reset();
        record.source.reset();
        result.diagnostics.push_back({key, "UNRESOLVED_MODEL_DEPENDENCY",
            "Model provision has missing, invalid or cyclic input dependencies.", {}});
    }
    for (auto& [key, record] : result.parameters) {
        const auto& definition = *record.definition;
        record.requirement = definition.requirement == RequirementKind::Optional
            ? input_detail::known(false) : input_detail::condition(definition.condition, result, context);
        if (record.requirement.value == true && record.state == InputState::Missing && !record.resolved)
            result.diagnostics.push_back({key, "MISSING_PARAMETER",
                "Required input has no declared value.", {}});
    }
    return result;
}
// Applicability describes a consumer, independently of permission to omit input.
// Unknown controlling values remain unknown; defaults never stand in for a switch.
inline ConditionResult InputApplicability(const ParameterDefinition& definition,
                                          const StandardInputResolution& values,
                                          const InputContext& context) {
    using namespace input_detail;
    const auto key = definition.key;
    if (definition.requirement != RequirementKind::Optional)
        return condition(definition.condition, values, context);
    if (key == "cuda_device") return choice(values, "compute_backend", {"cuda", "auto"});
    if (key == "eos_helm_table_path") return choice(values, "eos_type", {"tabular"});
    if (key == "gravity_max_cycles") return choice(values, "gravity_type", {"self"});
    if (key == "diff_max_stages") return flag(values, "use_diffusion");
    if (key == "EntropyFixCoefficient")
        return combine(choice(values, "solver", {"roe", "sw"}), flag(values, "EntropyFix"), true);
    if (key == "linear_solver" || key.starts_with("ode_")) {
        auto active = flag(values, "use_burn");
        if (key == "ode_max_newton_iter")
            active = combine(active, choice(values, "ode_solver", {"BE_NR"}), true);
        if (key == "ode_dt_safe_fac") {
            auto bd = choice(values, "ode_solver", {"BD"});
            if (bd.value) bd.value = !*bd.value;
            active = combine(active, bd, true);
        }
        return active;
    }
    return known(true);
}
} // namespace arch::config
