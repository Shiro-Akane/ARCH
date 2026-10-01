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
    {"geometry", "string", "Grid", std::string_view("cartesian"), RequirementKind::Required, InputCondition::Always},
    {"nblockx1", "int", "Grid", GridConfig{}.nblockx1, RequirementKind::Required, InputCondition::Always},
    {"nblockx2", "int", "Grid", GridConfig{}.nblockx2, RequirementKind::Required, InputCondition::Always},
    {"nblockx3", "int", "Grid", GridConfig{}.nblockx3, RequirementKind::Required, InputCondition::Always},
    {"max_blocks", "int", "Grid", GridConfig{}.amr_max_blocks, RequirementKind::Optional, InputCondition::Always},
    {"x1_min", "expression", "Grid", std::string_view("0.0"), RequirementKind::Conditional, InputCondition::Axis1},
    {"x1_max", "expression", "Grid", std::string_view("1.0"), RequirementKind::Conditional, InputCondition::Axis1},
    {"x2_min", "expression", "Grid", std::string_view("0.0"), RequirementKind::Conditional, InputCondition::Axis2},
    {"x2_max", "expression", "Grid", std::string_view("1.0"), RequirementKind::Conditional, InputCondition::Axis2},
    {"x3_min", "expression", "Grid", std::string_view("0.0"), RequirementKind::Conditional, InputCondition::Axis3},
    {"x3_max", "expression", "Grid", std::string_view("1.0"), RequirementKind::Conditional, InputCondition::Axis3},
    {"x1l_boundary_type", "string", "Grid", std::string_view("outflow"), RequirementKind::Conditional, InputCondition::Axis1},
    {"x1r_boundary_type", "string", "Grid", std::string_view("outflow"), RequirementKind::Conditional, InputCondition::Axis1},
    {"x2l_boundary_type", "string", "Grid", std::string_view("outflow"), RequirementKind::Conditional, InputCondition::Axis2},
    {"x2r_boundary_type", "string", "Grid", std::string_view("outflow"), RequirementKind::Conditional, InputCondition::Axis2},
    {"x3l_boundary_type", "string", "Grid", std::string_view("outflow"), RequirementKind::Conditional, InputCondition::Axis3},
    {"x3r_boundary_type", "string", "Grid", std::string_view("outflow"), RequirementKind::Conditional, InputCondition::Axis3},
    {"solver", "string", "Runtime", std::string_view("SW"), RequirementKind::Required, InputCondition::Always},
    {"dt_init", "float", "Runtime", NumericsConfig{}.dt_init, RequirementKind::Conditional, InputCondition::Burn},
    {"dt_max", "float", "Runtime", NumericsConfig{}.dt_max, RequirementKind::Optional, InputCondition::Always},
    {"hll_wave_speed", "string", "Runtime", std::string_view("roe"), RequirementKind::Conditional, InputCondition::Hll},
    {"dt_min", "float", "Runtime", NumericsConfig{}.dt_min, RequirementKind::Optional, InputCondition::Always},
    {"tstep_change_factor", "float", "Runtime", NumericsConfig{}.tstep_change_factor, RequirementKind::Optional, InputCondition::Always},
    {"cfl", "float", "Runtime", NumericsConfig{}.cfl, RequirementKind::Required, InputCondition::Always},
    {"limiter", "string", "Runtime", std::string_view("minmod"), RequirementKind::Conditional, InputCondition::Limiter},
    {"reconstruct", "string", "Runtime", std::string_view("pcm"), RequirementKind::Required, InputCondition::Always},
    {"EntropyFix", "bool", "Runtime", true, RequirementKind::Conditional, InputCondition::Roe},
    {"EntropyFixCoefficient", "float", "Runtime", NumericsConfig{}.entropy_fix_coeff, RequirementKind::Optional, InputCondition::Always},
    {"sml_rho", "float", "Runtime", NumericsConfig{}.sml_rho, RequirementKind::Required, InputCondition::Always},
    {"min_eint", "float", "Runtime", NumericsConfig{}.min_eint, RequirementKind::Required, InputCondition::Always},
    {"max_eint", "float", "Runtime", NumericsConfig{}.max_eint, RequirementKind::Required, InputCondition::Always},
    {"compute_backend", "string", "Runtime", std::string_view("cpu"), RequirementKind::Required, InputCondition::Always},
    {"cuda_device", "int", "Runtime", ExecutionConfig{}.cuda_device, RequirementKind::Optional, InputCondition::Always},
    {"eos_type", "string", "EOS", std::string_view("ideal"), RequirementKind::Required, InputCondition::Always},
    {"eos_table_path", "string", "EOS", std::string_view(""), RequirementKind::Conditional, InputCondition::Tabular},
    {"eos_helm_table_path", "string", "EOS", std::string_view(""), RequirementKind::Optional, InputCondition::Always},
    {"eos_coulomb_mult", "float", "EOS", PhysicsConfig{}.eos_coulomb_mult, RequirementKind::Conditional, InputCondition::Helm},
    {"gamma", "float", "EOS", PhysicsConfig{}.gamma, RequirementKind::Conditional, InputCondition::Ideal},
    {"use_burn", "bool", "Network", PhysicsConfig{}.burn.use_burn, RequirementKind::Required, InputCondition::Always},
    {"network_name", "string", "Network", std::string_view("aprox19"), RequirementKind::Conditional, InputCondition::Network},
    {"nuclearTempMin", "float", "Network", PhysicsConfig{}.burn.nuclearTempMin, RequirementKind::Conditional, InputCondition::Burn},
    {"nuclearDensMin", "float", "Network", PhysicsConfig{}.burn.nuclearDensMin, RequirementKind::Conditional, InputCondition::Burn},
    {"smallt", "float", "Network", PhysicsConfig{}.burn.smallt, RequirementKind::Conditional, InputCondition::TemperatureFloor},
    {"smallx", "float", "Network", PhysicsConfig{}.burn.smallx, RequirementKind::Conditional, InputCondition::CompositionFloor},
    {"enucDtFactor", "float", "Network", PhysicsConfig{}.burn.enucDtFactor, RequirementKind::Conditional, InputCondition::Burn},
    {"use_nse", "string", "Network", std::string_view("true"), RequirementKind::Conditional, InputCondition::Burn},
    {"nseTempThreshold", "float", "Network", PhysicsConfig{}.burn.nseTempThreshold, RequirementKind::Conditional, InputCondition::Nse},
    {"nseDensThreshold", "float", "Network", PhysicsConfig{}.burn.nseDensThreshold, RequirementKind::Conditional, InputCondition::Nse},
    {"ode_solver", "string", "Network", std::string_view("BE_NR"), RequirementKind::Conditional, InputCondition::Burn},
    {"linear_solver", "string", "Network", std::string_view("Auto"), RequirementKind::Optional, InputCondition::Always},
    {"ode_rtol", "float", "Network", PhysicsConfig{}.burn.odeconfig.rtol, RequirementKind::Conditional, InputCondition::Burn},
    {"ode_atol", "float", "Network", PhysicsConfig{}.burn.odeconfig.atol, RequirementKind::Conditional, InputCondition::Burn},
    {"ode_max_newton_iter", "int", "Network", PhysicsConfig{}.burn.odeconfig.max_newton_iter, RequirementKind::Optional, InputCondition::Always},
    {"ode_max_substeps", "int", "Network", PhysicsConfig{}.burn.odeconfig.max_substeps, RequirementKind::Optional, InputCondition::Always},
    {"ode_dt_safe_fac", "float", "Network", PhysicsConfig{}.burn.odeconfig.dt_safe_factor, RequirementKind::Optional, InputCondition::Always},
    {"ode_dt_fac_max", "float", "Network", PhysicsConfig{}.burn.odeconfig.dt_fac_max, RequirementKind::Optional, InputCondition::Always},
    {"ode_dt_fac_min", "float", "Network", PhysicsConfig{}.burn.odeconfig.dt_fac_min, RequirementKind::Optional, InputCondition::Always},
    {"ode_initial_dt_frac", "float", "Network", PhysicsConfig{}.burn.odeconfig.initial_dt_frac, RequirementKind::Optional, InputCondition::Always},
    {"use_diffusion", "bool", "Diffusion", PhysicsConfig{}.diffusion.use_diffusion, RequirementKind::Required, InputCondition::Always},
    {"diff_integrator", "string", "Diffusion", std::string_view("RKL2"), RequirementKind::Conditional, InputCondition::Diffusion},
    {"diff_cfl", "float", "Diffusion", PhysicsConfig{}.diffusion.diff_cfl, RequirementKind::Conditional, InputCondition::Diffusion},
    {"diff_max_stages", "int", "Diffusion", PhysicsConfig{}.diffusion.max_stages, RequirementKind::Optional, InputCondition::Always},
    {"use_thermal_diff", "bool", "Diffusion", PhysicsConfig{}.diffusion.use_thermal_diffusion, RequirementKind::Conditional, InputCondition::Diffusion},
    {"use_viscous_diff", "bool", "Diffusion", PhysicsConfig{}.diffusion.use_viscous_diffusion, RequirementKind::Conditional, InputCondition::Diffusion},
    {"use_species_diff", "bool", "Diffusion", PhysicsConfig{}.diffusion.use_species_diffusion, RequirementKind::Conditional, InputCondition::Diffusion},
    {"nu_visc", "float", "Diffusion", PhysicsConfig{}.diffusion.nu_visc, RequirementKind::Conditional, InputCondition::Viscous},
    {"alpha_therm", "float", "Diffusion", PhysicsConfig{}.diffusion.alpha_therm, RequirementKind::Conditional, InputCondition::Thermal},
    {"D_spec", "float", "Diffusion", PhysicsConfig{}.diffusion.D_spec, RequirementKind::Conditional, InputCondition::Species},
    {"gravity_boundary", "string", "Gravity", std::string_view("periodic"), RequirementKind::Conditional, InputCondition::SelfGravity},
    {"gravity_rtol", "float", "Gravity", 1e-10, RequirementKind::Conditional, InputCondition::SelfGravity},
    {"gravity_atol", "float", "Gravity", 0.0, RequirementKind::Conditional, InputCondition::SelfGravity},
    {"gravity_max_cycles", "int", "Gravity", 200, RequirementKind::Optional, InputCondition::Always},
    {"gravity_type", "string", "Gravity", std::string_view("none"), RequirementKind::Required, InputCondition::Always},
    {"gravity_g_x", "expression", "Gravity", std::string_view("0.0"), RequirementKind::Conditional, InputCondition::ExternalGravity},
    {"gravity_g_y", "expression", "Gravity", std::string_view("0.0"), RequirementKind::Conditional, InputCondition::ExternalGravity},
    {"gravity_g_z", "expression", "Gravity", std::string_view("0.0"), RequirementKind::Conditional, InputCondition::ExternalGravity},
    {"gravity_G", "expression", "Gravity", arch::constants::gravity::cgs::gravitational_constant, RequirementKind::Retired, InputCondition::Always},
    {"lrefinemin", "int", "Grid", AmrConfig{}.lrefinemin, RequirementKind::Required, InputCondition::Always},
    {"lrefinemax", "int", "Grid", AmrConfig{}.lrefinemax, RequirementKind::Required, InputCondition::Always},
    {"regrid_interval", "int", "Grid", AmrConfig{}.regrid_interval, RequirementKind::Conditional, InputCondition::DynamicAmr},
    {"refine_var", "string", "Grid", std::string_view("DENS"), RequirementKind::Conditional, InputCondition::DynamicAmr},
    {"refine_threshold", "float", "Grid", AmrConfig{}.refine_threshold, RequirementKind::Conditional, InputCondition::CurvatureAmr},
    {"derefine_threshold", "float", "Grid", AmrConfig{}.derefine_threshold, RequirementKind::Conditional, InputCondition::CurvatureAmr},
    {"tmax", "float", "Runtime", IOConfig{}.tmax, RequirementKind::Required, InputCondition::Evolution},
    {"max_steps", "int", "Runtime", IOConfig{}.max_steps, RequirementKind::Optional, InputCondition::Always},
    {"out_dir", "string", "Runtime", std::string_view("data"), RequirementKind::Optional, InputCondition::Always},
    {"base_name", "string", "Runtime", std::string_view("arch"), RequirementKind::Optional, InputCondition::Always},
    {"plt_dt", "float", "Runtime", IOConfig{}.plt_dt, RequirementKind::Optional, InputCondition::Always},
    {"plt_dstep", "int", "Runtime", IOConfig{}.plt_dstep, RequirementKind::Optional, InputCondition::Always},
    {"chk_dt", "float", "Runtime", IOConfig{}.chk_dt, RequirementKind::Optional, InputCondition::Always},
    {"chk_dstep", "int", "Runtime", IOConfig{}.chk_dstep, RequirementKind::Optional, InputCondition::Always},
    {"restart", "bool", "Runtime", IOConfig{}.restart, RequirementKind::Optional, InputCondition::Always},
    {"restart_file", "string", "Runtime", std::string_view(""), RequirementKind::Conditional, InputCondition::Restart},
    {"plt_variables", "string", "Runtime", std::string_view("ALL"), RequirementKind::Optional, InputCondition::Always},
    {"time_integrator", "string", "Runtime", time_integrator_default, RequirementKind::Required, InputCondition::Always},
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
    for (const auto key : {"enforce_mass_conservation", "burn_verbose_level",
                           "ode_use_numerical_jac", "ode_freeze_jacobian", "timeintegrator"}) {
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
