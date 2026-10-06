/**
 * @file ScalarControlValidation.h
 * @brief Shared single-control domain checks for partial input and runtime.
 *
 * Missing values never enter this function. Cross-control, case, material and
 * resource checks remain separate; these predicates do not certify readiness.
 */
#pragma once
#include <cmath>
#include <initializer_list>
#include <string_view>

namespace arch::config {
inline const char* ScalarControlError(std::string_view key, double value) {
    const auto in = [key](std::initializer_list<std::string_view> keys) {
        for (const auto candidate : keys) if (key == candidate) return true;
        return false;
    };
    if (in({"sml_rho", "min_eint", "max_eint", "dt_init", "dt_min",
            "smallt", "enucDtFactor", "nseTempThreshold", "ode_atol"}))
        return std::isfinite(value) && value > 0.0 ? nullptr : "Requires a finite positive value.";
    if (in({"EntropyFixCoefficient", "nuclearTempMin", "nuclearDensMin",
            "nseDensThreshold", "nu_visc", "alpha_therm", "D_spec", "gravity_atol", "tmax"}))
        return std::isfinite(value) && value >= 0.0 ? nullptr : "Requires a finite nonnegative value.";
    if (in({"cfl", "diff_cfl", "ode_dt_safe_fac", "ode_dt_fac_min", "ode_initial_dt_frac"}))
        return std::isfinite(value) && value > 0.0 && value <= 1.0
            ? nullptr : "Requires a finite value in (0,1].";
    if (in({"smallx", "ode_rtol", "gravity_rtol"}))
        return std::isfinite(value) && value > 0.0 && value < 1.0
            ? nullptr : "Requires a finite value in (0,1).";
    if (in({"tstep_change_factor", "ode_dt_fac_max"}))
        return std::isfinite(value) && value >= 1.0
            ? nullptr : "Requires a finite value of at least one.";
    if (key == "jeans_cells")
        return std::isfinite(value) && value >= 4.0 ? nullptr : "Requires a finite value of at least four.";
    if (key == "gamma")
        return std::isfinite(value) && value > 1.0 ? nullptr : "Ideal-gas gamma must exceed one.";
    if (key == "eos_coulomb_mult")
        return std::isfinite(value) && value >= 0.0 && value <= 1.0
            ? nullptr : "Requires a finite value in [0,1].";
    if (in({"ode_max_newton_iter", "ode_max_substeps", "gravity_max_cycles", "max_blocks", "regrid_interval"}))
        return std::isfinite(value) && value > 0.0 ? nullptr : "Count must be positive.";
    if (key == "diff_max_stages")
        return std::isfinite(value) && value >= 2.0 ? nullptr : "RKL requires at least two stages.";
    if (key == "max_steps")
        return value == -1.0 || (std::isfinite(value) && value >= 0.0)
            ? nullptr : "Use -1 or a nonnegative step limit.";
    if (in({"plt_dt", "chk_dt", "plt_dstep", "chk_dstep"}))
        return value == -1.0 || (std::isfinite(value) && value > 0.0)
            ? nullptr : "Use -1 to disable output, or a positive cadence.";
    if (key == "cuda_device")
        return std::isfinite(value) && value >= 0.0 ? nullptr : "Device index cannot be negative.";
    return nullptr;
}
} // namespace arch::config
