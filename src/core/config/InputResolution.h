/**
 * @file InputResolution.h
 * @brief Resolve explicit standard input and requirement dependencies without resources.
 *
 * This stage does not construct SimConfig, execute Setup, or certify ranges and
 * case declarations. Runtime construction and API inspection consume its records.
 */
#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "core/config/StandardParameters.h"
#include "driver/dispatch/PolicyDescriptor.h"

namespace arch::config {
using InputValue = std::variant<int, double, bool, std::string>;
enum class InputState { Missing, Present, Invalid, Duplicate };
enum class InputValueSource { Input, DocumentedDefault };
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
struct InputRecord {
    const ParameterDefinition* definition = nullptr;
    InputState state = InputState::Missing;
    std::optional<InputValue> parsed;
    std::optional<InputValue> resolved;
    std::optional<InputValueSource> source;
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
} // namespace input_detail

inline StandardInputResolution ResolveStandardInput(const ConfigParser& parser,
                                                     const InputContext& context) {
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
        }
        result.parameters.emplace(key, std::move(record));
    }
    for (const auto key : {"enforce_mass_conservation", "burn_verbose_level",
                           "ode_use_numerical_jac", "ode_freeze_jacobian", "timeintegrator"}) {
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
    for (auto& [key, record] : result.parameters) {
        const auto& definition = *record.definition;
        record.requirement = definition.requirement == RequirementKind::Optional
            ? input_detail::known(false) : input_detail::condition(definition.condition, result, context);
        if (record.requirement.value == true && record.state == InputState::Missing)
            result.diagnostics.push_back({key, "MISSING_PARAMETER",
                "Required input has no declared value.", {}});
    }
    return result;
}
} // namespace arch::config
