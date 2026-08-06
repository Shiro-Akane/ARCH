/**
 * @file Driver.h
 * @brief Main time-integration loop (Driver) for the simulation.
 * * Implements the "Method of Lines" approach, decoupling the spatial discretization
 * * (SolverPolicy) from the time stepping logic.
 */

#pragma once

#include <iostream>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <fstream>
#include <stdexcept>

// Utilities and physics/numerics definitions
#include "DriverUtils.h"
#include "DriverBurn.h"
#include "DriverControl.h"
#include "../numerics/burnsolver/Networks.h"
#include "../numerics/diffusion/DiffDispatch.h"
#include "../io/IO.h"

#ifdef _OPENMP
#include <omp.h>
#endif

/**
 * @brief Executes the main simulation loop.
 * @tparam SolverPolicy The numerical scheme (e.g., Lax-Friedrichs, HLLC).
 * @tparam EosPolicy The equation of state (e.g., Ideal Gas).
 * @tparam GravityPolicy The gravity policy.
 * @tparam BurnerPolicy The burning policy.
 */
template <typename TimeIntegratorPolicy, typename EosPolicy, typename GravityPolicy, typename BurnerPolicy>
void run_simulation(FluidState &state, const EosPolicy &eos,
                    GravityPolicy &gravity, BurnerPolicy &burn,
                    const Grid &grid, const SimConfig &config,
                    const SpeciesManager &specs,
                    const RunState &start_state)
{
    // 1. Initialize simulation controller (handles IO timing, dt logic, and step counting)
    SimulationController ctrl(config, start_state);

    // 2. State Buffers (Double Buffering)
    FluidState u_current = state;              ///< State at time n (Current).
    FluidState u_next(grid, specs.count());    ///< State at time n+1 (Next).
    FluidState u_scratch(grid, specs.count()); ///< RK Scratch state

    const NumericsConfig &num_cfg = config.numerics;
    BCHandler bc_handler{config};

    std::cout << ">>> Simulation Started | Solver: " << TimeIntegratorPolicy::name()
              << " | Entropy Fix Coeff: " << num_cfg.entropy_fix_coeff;
#ifdef _OPENMP
    std::cout << " | OpenMP: ON (max threads=" << omp_get_max_threads() << ")";
#else
    std::cout << " | OpenMP: OFF";
#endif
    std::cout << std::endl;

    bool has_burn = config.physics.burn.use_burn;
    bool has_diff = config.physics.diffusion.use_diffusion;

    double dt_burn_global = 1e99;
    double cfl = config.numerics.cfl;

    // Output initial state if we are at step 0
    if (ctrl.should_print_header())
    {
        write_plt(u_current, eos, grid, ctrl.plt_file_index++, ctrl.t_current, config, specs);
        write_chk(u_current, grid, ctrl.chk_file_index++, ctrl.plt_file_index, ctrl.step_count, ctrl.t_current, config);
        ctrl.print_header(has_burn, has_diff);
    }

    // =========================================================
    // Main Time Loop (Method of Lines)
    // =========================================================
    while (!ctrl.is_finished())
    {
        // -----------------------------------------------------
        // Step A: IO Routine
        // -----------------------------------------------------
        bool do_plt, do_chk;
        ctrl.check_io(do_plt, do_chk);
        
        if (do_plt) 
            write_plt(u_current, eos, grid, ctrl.plt_file_index++, ctrl.t_current, config, specs);
        if (do_chk) 
            write_chk(u_current, grid, ctrl.chk_file_index++, ctrl.plt_file_index, ctrl.step_count, ctrl.t_current, config);

        // -----------------------------------------------------
        // Step B: Calculate Time Step (CFL Condition)
        // -----------------------------------------------------
        double dt_hydro = adaptive_dt(u_current, eos, grid, cfl);
        
        double dt_computed = ctrl.calculate_next_dt(dt_hydro, dt_burn_global);
        dt_burn_global = 1e99; // Reset for internal computation
        
        double dt_diff_limit = DiffFlux::adaptive_dt_diff(u_current, eos, grid, config, 0.5);
        double dt = ctrl.sync_dt(dt_computed);

        // -----------------------------------------------------
        // Step C: Numerical Update (Operator Splitting)
        // -----------------------------------------------------
        
        // C1. Burn Step (1/2 dt)
        bc_handler.apply(u_current, grid);
        execute_burn_step(u_current, 0.5 * dt, eos, burn, grid, config, dt_burn_global);

        // C2. Hydrodynamics Step (dt)
        bc_handler.apply(u_current, grid);
        TimeIntegratorPolicy::solve(u_current, u_next, u_scratch, eos, grid, dt, bc_handler, gravity, num_cfg);

        // C3. Diffusion Step (dt)
        if (has_diff) {
            bc_handler.apply(u_next, grid);
            Numerics::Diffusion::dispatch_diffusion(config, [&](auto& integrator) {
                integrator.integrate(u_next, eos, grid, config, dt, dt_diff_limit, bc_handler);
            });
        }

        // C4. Burn Step (1/2 dt)
        bc_handler.apply(u_next, grid);
        execute_burn_step(u_next, 0.5 * dt, eos, burn, grid, config, dt_burn_global);

        // C5. Ping-Pong Buffering (Swap pointers/references)
        std::swap(u_current, u_next);

        // -----------------------------------------------------
        // Step D: Advance Counters
        // -----------------------------------------------------
        ctrl.advance(dt);
        ctrl.print_step(dt, dt_hydro, has_burn ? dt / 2.0 : 0.0, dt_diff_limit, has_burn, has_diff);
    }

    // =========================================================
    // Final Output (Force output at t_max)
    // =========================================================
    if (std::abs(ctrl.t_current - ctrl.t_max) < 1e-9)
    {
        std::cout << ">>> Target Time Reached. Forcing final output..." << std::endl;
        write_plt(u_current, eos, grid, ctrl.plt_file_index++, ctrl.t_current, config, specs);
        write_chk(u_current, grid, ctrl.chk_file_index++, ctrl.plt_file_index, ctrl.step_count, ctrl.t_current, config);
    }

    std::cout << ">>> Simulation Done. Total Steps: " << ctrl.step_count
              << " | Final Time: " << ctrl.t_current << std::endl;
}
