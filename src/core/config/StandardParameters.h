#pragma once

// Standard .par defaults shared by RuntimeParams and the local configuration API.
// Definitions describe input defaults, not results of policy resolution or Setup.
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include "physics/constant/PhysicalConstants.h"
#include "io/ConfigParser.h"
#include "data/GlobalDefs.h"

namespace arch::config {
inline constexpr std::string_view time_integrator_default = "RK2";
using DefaultValue = std::variant<int, double, bool, std::string_view>;
struct ParameterDefinition {
    std::string_view key, type, group;
    DefaultValue fallback;
};
inline const std::array<ParameterDefinition, 92> standard_parameters{{
    {"geometry", "string", "Grid", std::string_view("cartesian")},
    {"nblockx1", "int", "Grid", GridConfig{}.nblockx1},
    {"nblockx2", "int", "Grid", GridConfig{}.nblockx2},
    {"nblockx3", "int", "Grid", GridConfig{}.nblockx3},
    {"max_blocks", "int", "Grid", GridConfig{}.amr_max_blocks},
    {"x1_min", "expression", "Grid", std::string_view("0.0")},
    {"x1_max", "expression", "Grid", std::string_view("1.0")},
    {"x2_min", "expression", "Grid", std::string_view("0.0")},
    {"x2_max", "expression", "Grid", std::string_view("1.0")},
    {"x3_min", "expression", "Grid", std::string_view("0.0")},
    {"x3_max", "expression", "Grid", std::string_view("1.0")},
    {"x1l_boundary_type", "string", "Grid", std::string_view("outflow")},
    {"x1r_boundary_type", "string", "Grid", std::string_view("outflow")},
    {"x2l_boundary_type", "string", "Grid", std::string_view("outflow")},
    {"x2r_boundary_type", "string", "Grid", std::string_view("outflow")},
    {"x3l_boundary_type", "string", "Grid", std::string_view("outflow")},
    {"x3r_boundary_type", "string", "Grid", std::string_view("outflow")},
    {"solver", "string", "Runtime", std::string_view("SW")},
    {"dt_init", "float", "Runtime", NumericsConfig{}.dt_init},
    {"dt_min", "float", "Runtime", NumericsConfig{}.dt_min},
    {"tstep_change_factor", "float", "Runtime", NumericsConfig{}.tstep_change_factor},
    {"cfl", "float", "Runtime", NumericsConfig{}.cfl},
    {"limiter", "string", "Runtime", std::string_view("minmod")},
    {"reconstruct", "string", "Runtime", std::string_view("pcm")},
    {"EntropyFix", "bool", "Runtime", true},
    {"EntropyFixCoefficient", "float", "Runtime", NumericsConfig{}.entropy_fix_coeff},
    {"sml_rho", "float", "Runtime", NumericsConfig{}.sml_rho},
    {"min_eint", "float", "Runtime", NumericsConfig{}.min_eint},
    {"max_eint", "float", "Runtime", NumericsConfig{}.max_eint},
    {"compute_backend", "string", "Runtime", std::string_view("cpu")},
    {"cuda_device", "int", "Runtime", ExecutionConfig{}.cuda_device},
    {"eos_type", "string", "EOS", std::string_view("ideal")},
    {"eos_table_path", "string", "EOS", std::string_view("")},
    {"eos_helm_table_path", "string", "EOS", std::string_view("")},
    {"gamma", "float", "EOS", PhysicsConfig{}.gamma},
    {"use_burn", "bool", "Network", PhysicsConfig{}.burn.use_burn},
    {"network_name", "string", "Network", std::string_view("aprox19")},
    {"nuclearTempMin", "float", "Network", PhysicsConfig{}.burn.nuclearTempMin},
    {"nuclearDensMin", "float", "Network", PhysicsConfig{}.burn.nuclearDensMin},
    {"smallt", "float", "Network", PhysicsConfig{}.burn.smallt},
    {"smallx", "float", "Network", PhysicsConfig{}.burn.smallx},
    {"enucDtFactor", "float", "Network", PhysicsConfig{}.burn.enucDtFactor},
    {"use_nse", "string", "Network", std::string_view("true")},
    {"nseTempThreshold", "float", "Network", PhysicsConfig{}.burn.nseTempThreshold},
    {"nseDensThreshold", "float", "Network", PhysicsConfig{}.burn.nseDensThreshold},
    {"ode_solver", "string", "Network", std::string_view("BE_NR")},
    {"linear_solver", "string", "Network", std::string_view("Auto")},
    {"ode_rtol", "float", "Network", PhysicsConfig{}.burn.odeconfig.rtol},
    {"ode_atol", "float", "Network", PhysicsConfig{}.burn.odeconfig.atol},
    {"ode_max_newton_iter", "int", "Network", PhysicsConfig{}.burn.odeconfig.max_newton_iter},
    {"ode_max_substeps", "int", "Network", PhysicsConfig{}.burn.odeconfig.max_substeps},
    {"ode_dt_safe_fac", "float", "Network", PhysicsConfig{}.burn.odeconfig.dt_safe_factor},
    {"ode_dt_fac_max", "float", "Network", PhysicsConfig{}.burn.odeconfig.dt_fac_max},
    {"ode_dt_fac_min", "float", "Network", PhysicsConfig{}.burn.odeconfig.dt_fac_min},
    {"ode_initial_dt_frac", "float", "Network", PhysicsConfig{}.burn.odeconfig.initial_dt_frac},
    {"use_diffusion", "bool", "Diffusion", PhysicsConfig{}.diffusion.use_diffusion},
    {"diff_integrator", "string", "Diffusion", std::string_view("RKL2")},
    {"diff_cfl", "float", "Diffusion", PhysicsConfig{}.diffusion.diff_cfl},
    {"diff_max_stages", "int", "Diffusion", PhysicsConfig{}.diffusion.max_stages},
    {"use_thermal_diff", "bool", "Diffusion", PhysicsConfig{}.diffusion.use_thermal_diffusion},
    {"use_viscous_diff", "bool", "Diffusion", PhysicsConfig{}.diffusion.use_viscous_diffusion},
    {"use_species_diff", "bool", "Diffusion", PhysicsConfig{}.diffusion.use_species_diffusion},
    {"nu_visc", "float", "Diffusion", PhysicsConfig{}.diffusion.nu_visc},
    {"alpha_therm", "float", "Diffusion", PhysicsConfig{}.diffusion.alpha_therm},
    {"D_spec", "float", "Diffusion", PhysicsConfig{}.diffusion.D_spec},
    {"gravity_boundary", "string", "Gravity", std::string_view("periodic")},
    {"gravity_rtol", "float", "Gravity", 1e-10},
    {"gravity_atol", "float", "Gravity", 0.0},
    {"gravity_max_cycles", "int", "Gravity", 200},
    {"gravity_type", "string", "Gravity", std::string_view("none")},
    {"gravity_g_x", "expression", "Gravity", std::string_view("0.0")},
    {"gravity_g_y", "expression", "Gravity", std::string_view("0.0")},
    {"gravity_g_z", "expression", "Gravity", std::string_view("0.0")},
    {"gravity_G", "expression", "Gravity", arch::constants::gravity::cgs::gravitational_constant},
    {"lrefinemin", "int", "Grid", AmrConfig{}.lrefinemin},
    {"lrefinemax", "int", "Grid", AmrConfig{}.lrefinemax},
    {"regrid_interval", "int", "Grid", AmrConfig{}.regrid_interval},
    {"refine_var", "string", "Grid", std::string_view("DENS")},
    {"refine_threshold", "float", "Grid", AmrConfig{}.refine_threshold},
    {"derefine_threshold", "float", "Grid", AmrConfig{}.derefine_threshold},
    {"tmax", "float", "Runtime", IOConfig{}.tmax},
    {"max_steps", "int", "Runtime", IOConfig{}.max_steps},
    {"out_dir", "string", "Runtime", std::string_view("data")},
    {"base_name", "string", "Runtime", std::string_view("arch")},
    {"plt_dt", "float", "Runtime", IOConfig{}.plt_dt},
    {"plt_dstep", "int", "Runtime", IOConfig{}.plt_dstep},
    {"chk_dt", "float", "Runtime", IOConfig{}.chk_dt},
    {"chk_dstep", "int", "Runtime", IOConfig{}.chk_dstep},
    {"restart", "bool", "Runtime", IOConfig{}.restart},
    {"restart_file", "string", "Runtime", std::string_view("")},
    {"plt_variables", "string", "Runtime", std::string_view("ALL")},
    {"time_integrator", "string", "Runtime", time_integrator_default},
}};
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
