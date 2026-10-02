/**
 * @file ConfigValidation.h
 * @brief Check cross-field physical and numerical invariants after typed parsing.
 *
 * Workflow:
 * 1. Read validated configuration or a registered problem request.
 * 2. Check cross-field physical and numerical invariants after typed parsing.
 * 3. Return a single resolved value or state with explicit failure on invalid input.
 */

#pragma once

#include <cmath>

#include "amr/topology/Morton.h"
#include "data/GlobalDefs.h"
#include "driver/dispatch/PolicyDescriptor.h"
#include "io/ConfigParser.h"
#include "core/config/ScalarControlValidation.h"
#include "core/config/ControlRelations.h"

namespace arch::config {
// Shared by text parsing, direct C++ setup and runtime dispatch. Presentation
// priority is a GUI concern; validation follows physical ownership only.
inline void ValidateControls(const SimConfig& c, int species_count = 0)
{
    const auto require = [](bool valid, const char* key, const char* message) {
        if (!valid) throw ConfigValueError(key, "INVALID_RANGE", message);
    };
    const auto scalar = [&](double value, const char* key) {
        if (const auto* error = ScalarControlError(key, value))
            throw ConfigValueError(key, "INVALID_RANGE", error);
    };
    const auto& n = c.numerics;
    scalar(n.sml_rho, "sml_rho");
    scalar(n.min_eint, "min_eint");
    scalar(n.max_eint, "max_eint");
    require(relations::AtLeast(n.max_eint, n.min_eint), "max_eint", "Must be at least min_eint.");
    scalar(n.cfl, "cfl");
    scalar(n.entropy_fix_coeff, "EntropyFixCoefficient");
    scalar(n.dt_init, "dt_init");
    scalar(n.dt_min, "dt_min");
    require(relations::TimeCap(n.dt_max, n.dt_min),
            "dt_max", "Use -1 for no cap, or a finite cap at least dt_min.");
    require(relations::HllSpeed(n.solver_name, n.hll_roe_wave_speed), "hll_wave_speed",
            "A nondefault signal-speed estimator requires HLL or HLLC; other solvers have their own spectra.");
    scalar(c.physics.eos_coulomb_mult, "eos_coulomb_mult");
    require(relations::Coulomb(c.physics.eos_coulomb_mult, c.physics.eos_type), "eos_coulomb_mult",
            "A nondefault Coulomb factor requires eos_type=helmholtz.");
    require(relations::AtLeast(n.dt_init, n.dt_min), "dt_init", "Must be at least dt_min.");
    scalar(n.tstep_change_factor, "tstep_change_factor");
    scalar(c.physics.gamma, "gamma");
    const auto& b = c.physics.burn;
    scalar(b.nuclearTempMin, "nuclearTempMin");
    scalar(b.nuclearDensMin, "nuclearDensMin");
    scalar(b.smallt, "smallt");
    scalar(b.smallx, "smallx");
    require(b.smallx < 1.0 && species_count * b.smallx < 1.0,
            "smallx", "Require 0 < smallx and species_count * smallx < 1.");
    scalar(b.enucDtFactor, "enucDtFactor");
    scalar(b.nseTempThreshold, "nseTempThreshold");
    scalar(b.nseDensThreshold, "nseDensThreshold");
    const auto& o = b.odeconfig;
    scalar(o.rtol, "ode_rtol");
    scalar(o.atol, "ode_atol");
    scalar(o.max_newton_iter, "ode_max_newton_iter");
    scalar(o.max_substeps, "ode_max_substeps");
    scalar(o.dt_safe_factor, "ode_dt_safe_fac");
    scalar(o.dt_fac_min, "ode_dt_fac_min");
    scalar(o.dt_fac_max, "ode_dt_fac_max");
    scalar(o.initial_dt_frac, "ode_initial_dt_frac");
    const auto& d = c.physics.diffusion;
    scalar(d.diff_cfl, "diff_cfl");
    scalar(d.max_stages, "diff_max_stages");
    scalar(d.nu_visc, "nu_visc");
    scalar(d.alpha_therm, "alpha_therm");
    scalar(d.D_spec, "D_spec");
    const auto& g = c.physics.gravity;
    scalar(g.relative_tolerance, "gravity_rtol");
    scalar(g.absolute_tolerance, "gravity_atol");
    scalar(g.max_cycles, "gravity_max_cycles");
    require(g.boundary == "periodic" || g.boundary == "isolated", "gravity_boundary", "Expected periodic or isolated gravity boundary.");
    if (g.type == "self") {
        relations::GravityTopology topology;
        topology.geometry = c.grid.geometry;
        topology.boundary = g.boundary;
        topology.dimension = c.grid.dim;
        topology.lower = {c.grid.x1_min, c.grid.x2_min, c.grid.x3_min};
        topology.upper = {c.grid.x1_max, c.grid.x2_max, c.grid.x3_max};
        topology.faces = {c.grid.x1l_boundary_type, c.grid.x1r_boundary_type,
                          c.grid.x2l_boundary_type, c.grid.x2r_boundary_type,
                          c.grid.x3l_boundary_type, c.grid.x3r_boundary_type};
        relations::CheckGravityTopology(topology,
            [&](const char* key, const char* message, const auto&) { require(false, key, message); });
    }
    require(std::isfinite(g.g_x), "gravity_g_x", "Acceleration must be finite.");
    require(std::isfinite(g.g_y), "gravity_g_y", "Acceleration must be finite.");
    require(std::isfinite(g.g_z), "gravity_g_z", "Acceleration must be finite.");
    require(c.grid.nblockx1 > 0, "nblockx1", "First axis requires positive blocks.");
    require(c.grid.nblockx2 >= 0, "nblockx2", "Block count cannot be negative.");
    require(relations::AxisTopology(c.grid.nblockx2, c.grid.nblockx3),
            "nblockx3", "Third axis requires an active second axis.");
    scalar(c.grid.amr_max_blocks, "max_blocks");
    const double lower[]{c.grid.x1_min, c.grid.x2_min, c.grid.x3_min};
    const double upper[]{c.grid.x1_max, c.grid.x2_max, c.grid.x3_max};
    const char* keys[]{"x1_max", "x2_max", "x3_max"};
    for (int axis = 0; axis < c.grid.dim; ++axis)
        require(relations::ActiveExtent(lower[axis], upper[axis]), keys[axis], "Active axis must have finite positive extent.");
    require(relations::RefinementLevels(c.amr.lrefinemin, c.amr.lrefinemax),
            "lrefinemax", "Require ordered refinement levels within the Morton range.");
    scalar(c.amr.regrid_interval, "regrid_interval");
    require(relations::CurvatureThresholds(c.amr.refine_threshold, c.amr.derefine_threshold),
            "refine_threshold", "Require 0 <= derefine_threshold < refine_threshold <= 1.");
    scalar(c.io.tmax, "tmax");
    scalar(c.io.max_steps, "max_steps");
    scalar(c.io.plt_dt, "plt_dt"); scalar(c.io.chk_dt, "chk_dt");
    scalar(c.io.plt_dstep, "plt_dstep"); scalar(c.io.chk_dstep, "chk_dstep");
    scalar(c.execution.cuda_device, "cuda_device");
}
// Restart identity for state recovery and the physical/temporal limits that
// determine accepted trajectories. The format revision fixes algorithm policy.
inline constexpr std::size_t StateControlCount = 18;
inline constexpr double StateControlRevision = 2.0;
inline std::vector<double> StateControlIdentity(const SimConfig& c)
{
    const auto& n=c.numerics; const auto& b=c.physics.burn;
    return {StateControlRevision,n.sml_rho,n.min_eint,n.max_eint,n.cfl,n.dt_init,n.dt_min,n.tstep_change_factor,
        b.smallt,b.smallx,b.nuclearTempMin,b.nuclearDensMin,b.enucDtFactor,
        b.odeconfig.rtol,b.odeconfig.atol,n.dt_max,c.physics.eos_coulomb_mult,
        double(n.hll_roe_wave_speed)};
}
} // namespace arch::config
