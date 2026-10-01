/**
 * @file StandardParameters.h
 * @brief Keep requirements and approved defaults tied to one canonical key list.
 *
 * Workflow:
 * 1. Read validated configuration or a registered problem request.
 * 2. Keep requirements and approved defaults tied to one canonical key list.
 * 3. Return a single resolved value or state with explicit failure on invalid input.
 */

#pragma once

// Shared standard-input declarations. Required/conditional/retired entries
// carry no runtime default; template recommendations are a separate contract.
#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

#include "data/GlobalDefs.h"
#include "io/ConfigParser.h"
#include "physics/constant/PhysicalConstants.h"

namespace arch::config {
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
    std::optional<DefaultValue> declared_default; // Only the approved optional set.
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
    return definition.requirement == RequirementKind::Optional && definition.declared_default
        ? &*definition.declared_default : nullptr;
}
inline const ParameterDefinition& Definition(std::string_view key) {
    for (const auto& definition : standard_parameters)
        if (definition.key == key) return definition;
    throw std::logic_error("Unknown standard parameter: " + std::string(key));
}
} // namespace arch::config
