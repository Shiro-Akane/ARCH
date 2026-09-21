#pragma once
#include "Json.h"
#include "../core/StandardParameters.h"
#include <map>

namespace arch::api {
// Presentation strings only. Defaults, parsing and numerical rules stay in Core.
inline detail::Json ParameterPresentation(const config::ParameterDefinition& definition) {
    static const std::map<std::string, std::pair<const char*, const char*>> text{
        {"geometry", {"Coordinate system", "Coordinate system used to interpret the three logical axes."}},
        {"max_blocks", {"Block pool capacity", "Capacity of the simulation block pool. Nonpositive values use 10000; this is not an off switch."}},
        {"solver", {"Flux solver", "Numerical flux used to advance the fluid state."}},
        {"cfl", {"CFL factor", "Safety factor for the fluid time step."}},
        {"limiter", {"Slope limiter", "Limits reconstructed slopes near sharp changes."}},
        {"reconstruct", {"Reconstruction", "Reconstructs the state at cell faces."}},
        {"time_integrator", {"Time integrator", "Time stepping method for fluid evolution."}},
        {"timeintegrator", {"Time integrator", "Legacy alias of time_integrator; the canonical key takes precedence."}},
        {"EntropyFix", {"Entropy correction", "Enables the solver entropy correction where supported."}},
        {"EntropyFixCoefficient", {"Entropy correction strength", "Strength of the correction when EntropyFix is enabled."}},
        {"sml_rho", {"Density floor", "Minimum permitted density used by numerical state repairs."}},
        {"min_eint", {"Internal energy floor", "Lower bound on specific internal energy."}},
        {"max_eint", {"Internal energy ceiling", "Upper bound on specific internal energy."}},
        {"compute_backend", {"Compute backend", "Requested backend for simulation. Initial previews always use CPU."}},
        {"cuda_device", {"CUDA device", "Device index for a CUDA simulation; configuration inspection does not probe devices."}},
        {"eos_type", {"Equation of state", "Converts between density, pressure, temperature and internal energy."}},
        {"eos_table_path", {"Main EOS table", "Main table used by Helmholtz or Tabular. Relative paths use the worker working directory."}},
        {"eos_helm_table_path", {"Supplementary EOS table", "Optional electron and positron table for Tabular tables that need this component. This is not an alias of the main table."}},
        {"gamma", {"Heat capacity ratio", "Global ideal-gas gamma; a case can supply species-specific values."}},
        {"use_burn", {"Enable reactions", "Enables reaction evolution; case composition setup can still need the selected network when disabled."}},
        {"network_name", {"Reaction network", "Registered network used for species and reaction data."}},
        {"nuclearTempMin", {"Reaction temperature threshold", "Minimum temperature at which reaction evolution is attempted."}},
        {"nuclearDensMin", {"Reaction density threshold", "Minimum density at which reaction evolution is attempted."}},
        {"smallt", {"Temperature floor", "Temperature floor used by the reaction solver."}},
        {"smallx", {"Abundance floor", "Small abundance cutoff used by the reaction solver."}},
        {"enucDtFactor", {"Energy time step factor", "Controls the reaction energy based time step restriction."}},
        {"use_nse", {"Equilibrium mode", "Enables, disables or automatically selects the nuclear equilibrium shortcut."}},
        {"nseTempThreshold", {"Equilibrium temperature threshold", "Temperature threshold for the equilibrium shortcut."}},
        {"nseDensThreshold", {"Equilibrium density threshold", "Density threshold for the equilibrium shortcut."}},
        {"enforce_mass_conservation", {"Conserve composition mass", "Enables mass conservation handling in reaction integration."}},
        {"burn_verbose_level", {"Reaction logging level", "Detail level of reaction solver diagnostics."}},
        {"ode_solver", {"Reaction integrator", "ODE integration method for reactions."}},
        {"linear_solver", {"Linear solver", "Linear system solver used by implicit reaction integration."}},
        {"ode_rtol", {"Relative tolerance", "Relative component error tolerance for reaction integration."}},
        {"ode_atol", {"Absolute tolerance", "Absolute component error tolerance for the mixed temperature and abundance state; there is no single scalar unit."}},
        {"ode_max_newton_iter", {"Maximum Newton iterations", "Iteration limit for an implicit reaction substep."}},
        {"ode_max_substeps", {"Maximum reaction substeps", "Maximum substeps allowed in one reaction advance."}},
        {"ode_dt_safe_fac", {"Reaction step safety factor", "Safety factor used when choosing the next reaction substep."}},
        {"ode_dt_fac_max", {"Maximum step growth", "Upper multiplier for reaction substep adaptation."}},
        {"ode_dt_fac_min", {"Minimum step shrink factor", "Lower multiplier for reaction substep adaptation."}},
        {"ode_initial_dt_frac", {"Initial substep fraction", "Fraction of the requested reaction interval used for the first substep."}},
        {"ode_use_numerical_jac", {"Numerical Jacobian", "Uses numerical derivatives where supported by the selected integrator."}},
        {"ode_freeze_jacobian", {"Reuse Jacobian", "Reuses the Jacobian within iterations where supported."}},
        {"use_diffusion", {"Enable diffusion", "Enables diffusion evolution and reveals diffusion settings."}},
        {"diff_integrator", {"Diffusion integrator", "Time integration method used for diffusion."}},
        {"diff_cfl", {"Diffusion CFL factor", "Safety factor used by diffusion time step estimates."}},
        {"diff_max_stages", {"Maximum diffusion stages", "Stage limit for diffusion integration."}},
        {"use_thermal_diff", {"Thermal diffusion", "Enables heat transport. Constant thermal diffusivity is entered only on the constant coefficient path."}},
        {"use_viscous_diff", {"Viscous diffusion", "Enables momentum diffusion. The stellar model currently supplies no viscosity."}},
        {"use_species_diff", {"Species diffusion", "Enables composition diffusion. The stellar model currently supplies no species diffusivity."}},
        {"nu_visc", {"Kinematic viscosity", "Constant kinematic viscosity on the constant coefficient path."}},
        {"alpha_therm", {"Thermal diffusivity", "Constant thermal diffusivity, not thermal conductivity."}},
        {"D_spec", {"Species diffusivity", "Constant composition diffusivity on the constant coefficient path."}},
        {"gravity_type", {"Gravity model", "Selects no gravity or an external acceleration. Self gravity is not implemented."}},
        {"gravity_G", {"Gravitational constant", "Gravitational constant reserved for self gravity; that module is not implemented."}},
        {"lrefinemin", {"Minimum refinement level", "Prevents coarsening below this level; it does not force uniform initial refinement."}},
        {"lrefinemax", {"Maximum refinement level", "Highest allowed refinement level; each active direction is halved per level."}},
        {"regrid_interval", {"Regrid interval", "Number of simulation steps between regrids. Must be positive; it is not an initial preview iteration count."}},
        {"refine_var", {"Refinement fields", "Fields used by the refinement indicators, separated by commas or plus signs; species names are resolved after Setup."}},
        {"refine_threshold", {"Refinement threshold", "Refines blocks when their indicator exceeds this threshold."}},
        {"derefine_threshold", {"Coarsening threshold", "Coarsens eligible block families when their indicators fall below this threshold."}},
        {"tmax", {"End time", "Requested simulation end time."}},
        {"max_steps", {"Step limit", "Stops after this many steps when positive; nonpositive values disable this limit."}},
        {"out_dir", {"Output directory", "Directory for simulation output. Preview does not create it."}},
        {"base_name", {"Output base name", "Prefix for simulation output files."}},
        {"plt_dt", {"Plot time interval", "Writes plot output at this positive time interval; nonpositive disables this trigger."}},
        {"plt_dstep", {"Plot step interval", "Writes plot output every this many steps when positive; nonpositive disables this trigger."}},
        {"chk_dt", {"Checkpoint time interval", "Writes recovery checkpoints at this positive time interval; nonpositive disables this trigger."}},
        {"chk_dstep", {"Checkpoint step interval", "Writes recovery checkpoints every this many steps when positive; nonpositive disables this trigger."}},
        {"restart", {"Resume from checkpoint", "Loads a saved simulation state instead of creating initial conditions."}},
        {"restart_file", {"Checkpoint file", "Input checkpoint used when restart is enabled."}},
        {"plt_variables", {"Plot fields", "Selects the fields written to plot output; ALL selects the default complete set."}},
        {"nblockx1", {"Axis 1 blocks", "Root block count. Axis 1 must be positive; axes 2 and 3 use zero to disable the axis. Any positive integer enables it."}},
        {"x1_min", {"Axis 1 minimum", "Physical domain bound for this logical axis. Angular coordinates use radians; lengths use cm."}},
        {"x1_max", {"Axis 1 maximum", "Physical domain bound for this logical axis. Angular coordinates use radians; lengths use cm."}},
        {"x1l_boundary_type", {"Axis 1 lower boundary", "Boundary behavior at the lower end of this active axis."}},
        {"x1r_boundary_type", {"Axis 1 upper boundary", "Boundary behavior at the upper end of this active axis."}},
        {"nblockx2", {"Axis 2 blocks", "Root block count. Axis 1 must be positive; axes 2 and 3 use zero to disable the axis. Any positive integer enables it."}},
        {"x2_min", {"Axis 2 minimum", "Physical domain bound for this logical axis. Angular coordinates use radians; lengths use cm."}},
        {"x2_max", {"Axis 2 maximum", "Physical domain bound for this logical axis. Angular coordinates use radians; lengths use cm."}},
        {"x2l_boundary_type", {"Axis 2 lower boundary", "Boundary behavior at the lower end of this active axis."}},
        {"x2r_boundary_type", {"Axis 2 upper boundary", "Boundary behavior at the upper end of this active axis."}},
        {"nblockx3", {"Axis 3 blocks", "Root block count. Axis 1 must be positive; axes 2 and 3 use zero to disable the axis. Any positive integer enables it."}},
        {"x3_min", {"Axis 3 minimum", "Physical domain bound for this logical axis. Angular coordinates use radians; lengths use cm."}},
        {"x3_max", {"Axis 3 maximum", "Physical domain bound for this logical axis. Angular coordinates use radians; lengths use cm."}},
        {"x3l_boundary_type", {"Axis 3 lower boundary", "Boundary behavior at the lower end of this active axis."}},
        {"x3r_boundary_type", {"Axis 3 upper boundary", "Boundary behavior at the upper end of this active axis."}},
        {"gravity_g_x", {"External x acceleration", "Constant acceleration component used by external gravity."}},
        {"gravity_g_y", {"External y acceleration", "Constant acceleration component used by external gravity."}},
        {"gravity_g_z", {"External z acceleration", "Constant acceleration component used by external gravity."}},
    };
    const std::string key(definition.key);
    const auto& entry = text.at(key);
    const bool amr = key == "max_blocks" || key.starts_with("lrefine") || key == "regrid_interval"
        || key == "refine_var" || key == "refine_threshold" || key == "derefine_threshold";
    auto result = detail::Json::object({{"displayName", entry.first}, {"description", entry.second},
        {"subgroup", amr ? "AMR" : std::string(definition.group)}});
    if (key == "max_steps" || key == "plt_dt" || key == "plt_dstep" || key == "chk_dt" || key == "chk_dstep")
        result["toggle"] = detail::Json::object({{"enabledWhen", "value > 0"}, {"offValue", -1},
            {"preserveUneditedInput", true}, {"enabledValueRequired", true}});
    if (key == "alpha_therm" || key == "nu_visc" || key == "D_spec")
        result["enabledBy"] = key == "alpha_therm" ? "use_thermal_diff" : key == "nu_visc" ? "use_viscous_diff" : "use_species_diff";
    return result;
}
inline std::string OptionDisplayName(std::string_view name) {
    if (name == "bd") return "Bader-Deuflhard (BD)";
    if (name == "be_nr") return "Backward Euler (Newton)";
    if (name == "rk2") return "RK2";
    if (name == "rk3") return "RK3";
    if (name == "ros4") return "ROS4";
    return std::string(name);
}
} // namespace arch::api
