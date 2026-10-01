/**
 * @file StandardParameters.h
 * @brief Keep parser defaults and schema descriptions tied to one canonical key list.
 *
 * Workflow:
 * 1. Read validated configuration or a registered problem request.
 * 2. Keep parser defaults and schema descriptions tied to one canonical key list.
 * 3. Return a single resolved value or state with explicit failure on invalid input.
 */

#pragma once

// Standard .par defaults shared by RuntimeParams and the local configuration API.
// Definitions describe input defaults, not results of policy resolution or Setup.
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

#include "data/GlobalDefs.h"
#include "io/ConfigParser.h"
#include "physics/constant/PhysicalConstants.h"

namespace arch::config {
inline constexpr std::string_view time_integrator_default = "RK2";
using DefaultValue = std::variant<int, double, bool, std::string_view>;
enum class RequirementKind { Required, Conditional, Optional, Retired };
enum class InputCondition {
    Always, Evolution, Axis1, Axis2, Axis3, Limiter, Roe, Hll,
    Ideal, Tabular, Helm, Network, Burn, TemperatureFloor, CompositionFloor,
    Nse, Diffusion, Thermal, Viscous, Species, SelfGravity, ExternalGravity,
    DynamicAmr, CurvatureAmr, Restart
};
struct ParameterDefinition {
    std::string_view key, type, group;
    DefaultValue fallback; // Transitional v2 loader storage; never a v3 permission.
    RequirementKind requirement;
    InputCondition condition;
};
inline const std::array<ParameterDefinition, 95> standard_parameters{{
#define ARCH_STANDARD_PARAMETER(...) {__VA_ARGS__},
#include "core/config/StandardParameterEntries.inc"
#undef ARCH_STANDARD_PARAMETER
}};
// Only this named allow-list can supply a missing value during v3 resolution.
inline const DefaultValue* AllowedDefault(const ParameterDefinition& definition) {
    return definition.requirement == RequirementKind::Optional ? &definition.fallback : nullptr;
}
inline const ParameterDefinition& Definition(std::string_view key) {
    for (const auto& definition : standard_parameters)
        if (definition.key == key) return definition;
    throw std::logic_error("Unknown standard parameter: " + std::string(key));
}
inline int DefaultInt(std::string_view key) { return std::get<int>(Definition(key).fallback); }
inline double DefaultDouble(std::string_view key) { return std::get<double>(Definition(key).fallback); }
inline bool DefaultBool(std::string_view key) {
    const auto& fallback = Definition(key).fallback;
    // use_nse has a string input contract (true/false/auto), with a true default.
    if (const auto* value = std::get_if<bool>(&fallback)) return *value;
    return std::get<std::string_view>(fallback) == "true";
}
inline std::string DefaultString(std::string_view key) {
    return std::string(std::get<std::string_view>(Definition(key).fallback));
}
// Validate even explicitly supplied inactive settings; never truncate bad tokens.
inline void ValidateStandardTokens(const ConfigParser& parser) {
    for (const auto retired : retired_input_keys) {
        const std::string key(retired);
        if (parser.HasKey(key)) throw ConfigValueError(key, "RETIRED_PARAMETER",
            "This Core parameter has been retired; remove it from the input.");
    }
    for (const auto& d : standard_parameters) {
        const std::string key(d.key);
        if (!parser.HasKey(key)) continue;
        const auto raw = parser.GetString(key, "");
        if (d.type == "int") ConfigParser::ParseInteger(key, raw);
        else if (d.type == "float") ConfigParser::ParseNumber(key, raw);
        else if (d.type == "bool") parser.GetBool(key, false);
        else if (d.type == "expression") ConfigParser::ParseExpression(key, raw);
    }
}
} // namespace arch::config
