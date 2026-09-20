#pragma once

// Standard .par defaults shared by RuntimeParams and the local configuration API.
// Definitions describe input defaults, not results of policy resolution or Setup.
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include "../physics/constant/PhysicalConstants.h"
#include "../io/ConfigParser.h"

namespace arch::config {
inline constexpr std::string_view time_integrator_default = "RK2";
using DefaultValue = std::variant<int, double, bool, std::string_view>;
struct ParameterDefinition {
    std::string_view key, type, group;
    DefaultValue fallback;
};
inline constexpr std::array<ParameterDefinition, 90> standard_parameters{{
    {"geometry", "string", "Grid", std::string_view("cartesian")},
    {"nblockx1", "int", "Grid", 1},
    {"nblockx2", "int", "Grid", 1},
    {"nblockx3", "int", "Grid", 1},
    {"max_blocks", "int", "Grid", 2000},
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
    {"cfl", "float", "Runtime", 0.8},
    {"limiter", "string", "Runtime", std::string_view("minmod")},
    {"reconstruct", "string", "Runtime", std::string_view("pcm")},
    {"timeintegrator", "string", "Runtime", time_integrator_default},
    {"EntropyFix", "bool", "Runtime", true},
    {"EntropyFixCoefficient", "float", "Runtime", 0.1},
    {"sml_rho", "float", "Runtime", 1e-12},
    {"min_eint", "float", "Runtime", 1e-10},
    {"max_eint", "float", "Runtime", 1e21},
    {"compute_backend", "string", "Runtime", std::string_view("cpu")},
    {"cuda_device", "int", "Runtime", 0},
    {"eos_type", "string", "EOS", std::string_view("ideal")},
    {"eos_table_path", "string", "EOS", std::string_view("")},
    {"eos_helm_table_path", "string", "EOS", std::string_view("")},
    {"gamma", "float", "EOS", 1.4},
    {"use_burn", "bool", "Network", false},
    {"network_name", "string", "Network", std::string_view("aprox19")},
    {"nuclearTempMin", "float", "Network", 1e9},
    {"nuclearDensMin", "float", "Network", 1e-10},
    {"smallt", "float", "Network", 1e5},
    {"smallx", "float", "Network", 1e-20},
    {"enucDtFactor", "float", "Network", 1e30},
    {"use_nse", "string", "Network", std::string_view("true")},
    {"nseTempThreshold", "float", "Network", 4.5e9},
    {"nseDensThreshold", "float", "Network", 1.0e6},
    {"enforce_mass_conservation", "bool", "Network", true},
    {"burn_verbose_level", "int", "Network", 0},
    {"ode_solver", "string", "Network", std::string_view("BE_NR")},
    {"linear_solver", "string", "Network", std::string_view("Auto")},
    {"ode_rtol", "float", "Network", 1e-4},
    {"ode_atol", "float", "Network", 1e-8},
    {"ode_max_newton_iter", "int", "Network", 50},
    {"ode_max_substeps", "int", "Network", 10000},
    {"ode_dt_safe_fac", "float", "Network", 0.9},
    {"ode_dt_fac_max", "float", "Network", 2.0},
    {"ode_dt_fac_min", "float", "Network", 0.1},
    {"ode_initial_dt_frac", "float", "Network", 1e-3},
    {"ode_use_numerical_jac", "bool", "Network", false},
    {"ode_freeze_jacobian", "bool", "Network", false},
    {"use_diffusion", "bool", "Diffusion", false},
    {"diff_integrator", "string", "Diffusion", std::string_view("RKL2")},
    {"diff_cfl", "float", "Diffusion", 0.8},
    {"diff_max_stages", "int", "Diffusion", 256},
    {"use_thermal_diff", "bool", "Diffusion", false},
    {"use_viscous_diff", "bool", "Diffusion", false},
    {"use_species_diff", "bool", "Diffusion", false},
    {"nu_visc", "float", "Diffusion", 0.0},
    {"alpha_therm", "float", "Diffusion", 0.0},
    {"D_spec", "float", "Diffusion", 0.0},
    {"gravity_type", "string", "Gravity", std::string_view("none")},
    {"gravity_g_x", "expression", "Gravity", std::string_view("0.0")},
    {"gravity_g_y", "expression", "Gravity", std::string_view("0.0")},
    {"gravity_g_z", "expression", "Gravity", std::string_view("0.0")},
    {"gravity_G", "expression", "Gravity", arch::constants::gravity::cgs::gravitational_constant},
    {"lrefinemin", "int", "Grid", 0},
    {"lrefinemax", "int", "Grid", 0},
    {"regrid_interval", "int", "Grid", 2},
    {"refine_var", "string", "Grid", std::string_view("DENS")},
    {"refine_threshold", "float", "Grid", 0.8},
    {"derefine_threshold", "float", "Grid", 0.2},
    {"tmax", "float", "Runtime", 0.1},
    {"max_steps", "int", "Runtime", -1},
    {"out_dir", "string", "Runtime", std::string_view("data")},
    {"base_name", "string", "Runtime", std::string_view("arch")},
    {"plt_dt", "float", "Runtime", -1.0},
    {"plt_dstep", "int", "Runtime", -1},
    {"chk_dt", "float", "Runtime", -1.0},
    {"chk_dstep", "int", "Runtime", -1},
    {"restart", "bool", "Runtime", false},
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
