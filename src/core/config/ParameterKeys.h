/**
 * @file ParameterKeys.h
 * @brief Classify input ownership without depending on runtime storage types.
 */
#pragma once
#include <array>
#include <string_view>

namespace arch::config {
inline bool IsStandardInputKey(std::string_view key) {
    static constexpr std::string_view keys[] = {
#define ARCH_STANDARD_PARAMETER(key, ...) key,
#include "core/config/StandardParameterEntries.inc"
#undef ARCH_STANDARD_PARAMETER
    };
    for (const auto item : keys) if (item == key) return true;
    return false;
}
// Historical keys are recognized as retired, never as user extensions.
inline constexpr std::array<std::string_view, 5> retired_input_keys{
    "enforce_mass_conservation", "burn_verbose_level", "ode_use_numerical_jac",
    "ode_freeze_jacobian", "timeintegrator"};
} // namespace arch::config
