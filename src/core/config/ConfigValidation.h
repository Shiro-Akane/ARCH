#pragma once

#include <cmath>
#include "data/GlobalDefs.h"
#include "io/ConfigParser.h"
#include "amr/topology/Morton.h"

namespace arch::config {
// Shared by text parsing, direct C++ setup and runtime dispatch. Presentation
// priority is a GUI concern; validation follows physical ownership only.
inline void ValidateControls(const SimConfig& c, int species_count = 0)
{
    const auto require = [](bool valid, const char* key, const char* message) {
        if (!valid) throw ConfigValueError(key, "INVALID_RANGE", message);
    };
    const auto positive = [&](double value, const char* key) {
        require(std::isfinite(value) && value > 0.0, key, "Requires a finite positive value.");
    };
    const auto nonnegative = [&](double value, const char* key) {
        require(std::isfinite(value) && value >= 0.0, key, "Requires a finite nonnegative value.");
    };
    const auto& n = c.numerics;
    positive(n.sml_rho, "sml_rho");
    positive(n.min_eint, "min_eint");
    positive(n.max_eint, "max_eint");
    require(n.max_eint >= n.min_eint, "max_eint", "Must be at least min_eint.");
    positive(n.cfl, "cfl");
    require(n.cfl <= 1.0, "cfl", "Must not exceed one.");
    nonnegative(n.entropy_fix_coeff, "EntropyFixCoefficient");
    positive(n.dt_init, "dt_init");
    positive(n.dt_min, "dt_min");
    require(n.dt_init >= n.dt_min, "dt_init", "Must be at least dt_min.");
    require(std::isfinite(n.tstep_change_factor) && n.tstep_change_factor >= 1.0,
            "tstep_change_factor", "Requires a finite growth factor of at least one.");
    require(std::isfinite(c.physics.gamma) && c.physics.gamma > 1.0,
            "gamma", "Ideal-gas gamma must exceed one.");
    const auto& b = c.physics.burn;
    nonnegative(b.nuclearTempMin, "nuclearTempMin");
    nonnegative(b.nuclearDensMin, "nuclearDensMin");
    positive(b.smallt, "smallt");
    positive(b.smallx, "smallx");
    require(b.smallx < 1.0 && species_count * b.smallx < 1.0,
            "smallx", "Require 0 < smallx and species_count * smallx < 1.");
    positive(b.enucDtFactor, "enucDtFactor");
    positive(b.nseTempThreshold, "nseTempThreshold");
    nonnegative(b.nseDensThreshold, "nseDensThreshold");
    const auto& o = b.odeconfig;
    positive(o.rtol, "ode_rtol");
    positive(o.atol, "ode_atol");
    require(o.rtol < 1.0, "ode_rtol", "Relative tolerance must be smaller than one.");
    require(o.max_newton_iter > 0, "ode_max_newton_iter", "Iteration count must be positive.");
    require(o.max_substeps > 0, "ode_max_substeps", "Substep count must be positive.");
    positive(o.dt_safe_factor, "ode_dt_safe_fac");
    require(o.dt_safe_factor <= 1.0, "ode_dt_safe_fac", "Safety factor must not exceed one.");
    positive(o.dt_fac_min, "ode_dt_fac_min");
    require(o.dt_fac_min <= 1.0, "ode_dt_fac_min", "Reduction factor must not exceed one.");
    require(std::isfinite(o.dt_fac_max) && o.dt_fac_max >= 1.0,
            "ode_dt_fac_max", "Growth factor must be finite and at least one.");
    positive(o.initial_dt_frac, "ode_initial_dt_frac");
    require(o.initial_dt_frac <= 1.0, "ode_initial_dt_frac", "Initial substep fraction must not exceed one.");
    const auto& d = c.physics.diffusion;
    positive(d.diff_cfl, "diff_cfl");
    require(d.diff_cfl <= 1.0, "diff_cfl", "Must not exceed one.");
    require(d.max_stages >= 2, "diff_max_stages", "RKL requires at least two stages.");
    nonnegative(d.nu_visc, "nu_visc");
    nonnegative(d.alpha_therm, "alpha_therm");
    nonnegative(d.D_spec, "D_spec");
    const auto& g = c.physics.gravity;
    positive(g.G_const, "gravity_G");
    require(std::isfinite(g.g_x), "gravity_g_x", "Acceleration must be finite.");
    require(std::isfinite(g.g_y), "gravity_g_y", "Acceleration must be finite.");
    require(std::isfinite(g.g_z), "gravity_g_z", "Acceleration must be finite.");
    require(c.grid.nblockx1 > 0, "nblockx1", "First axis requires positive blocks.");
    require(c.grid.nblockx2 >= 0, "nblockx2", "Block count cannot be negative.");
    require(c.grid.nblockx3 >= 0 && (c.grid.nblockx3 == 0 || c.grid.nblockx2 > 0),
            "nblockx3", "Third axis requires an active second axis.");
    require(c.grid.amr_max_blocks > 0, "max_blocks", "Block capacity must be positive.");
    const double lower[]{c.grid.x1_min, c.grid.x2_min, c.grid.x3_min};
    const double upper[]{c.grid.x1_max, c.grid.x2_max, c.grid.x3_max};
    const char* keys[]{"x1_max", "x2_max", "x3_max"};
    for (int axis = 0; axis < c.grid.dim; ++axis)
        require(std::isfinite(lower[axis]) && std::isfinite(upper[axis] - lower[axis])
                && upper[axis] > lower[axis], keys[axis], "Active axis must have finite positive extent.");
    require(c.amr.lrefinemin >= 0 && c.amr.lrefinemax >= c.amr.lrefinemin
            && c.amr.lrefinemax <= amr::kMaxRefinementLevel,
            "lrefinemax", "Require ordered refinement levels within the Morton range.");
    require(c.amr.regrid_interval > 0, "regrid_interval", "Regrid interval must be positive.");
    require(std::isfinite(c.amr.refine_threshold) && std::isfinite(c.amr.derefine_threshold)
            && c.amr.refine_threshold <= 1.0 && c.amr.derefine_threshold >= 0.0
            && c.amr.derefine_threshold < c.amr.refine_threshold,
            "refine_threshold", "Require 0 <= derefine_threshold < refine_threshold <= 1.");
    nonnegative(c.io.tmax, "tmax");
    require(c.io.max_steps == -1 || c.io.max_steps >= 0, "max_steps", "Use -1 or a nonnegative step limit.");
    const auto cadence = [&](double value, const char* key) {
        require(std::isfinite(value) && (value == -1.0 || value > 0.0), key,
                "Use -1 to disable output, or a positive cadence.");
    };
    cadence(c.io.plt_dt, "plt_dt"); cadence(c.io.chk_dt, "chk_dt");
    cadence(c.io.plt_dstep, "plt_dstep"); cadence(c.io.chk_dstep, "chk_dstep");
    require(c.execution.cuda_device >= 0, "cuda_device", "Device index cannot be negative.");
}
// Restart identity for state recovery and the physical/temporal limits that
// determine accepted trajectories. The format revision fixes algorithm policy.
inline constexpr std::size_t StateControlCount = 15;
inline constexpr double StateControlRevision = 1.0;
inline std::vector<double> StateControlIdentity(const SimConfig& c)
{
    const auto& n=c.numerics; const auto& b=c.physics.burn;
    return {StateControlRevision,n.sml_rho,n.min_eint,n.max_eint,n.cfl,n.dt_init,n.dt_min,n.tstep_change_factor,
        b.smallt,b.smallx,b.nuclearTempMin,b.nuclearDensMin,b.enucDtFactor,
        b.odeconfig.rtol,b.odeconfig.atol};
}
} // namespace arch::config
