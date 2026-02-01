/**
 * @file Deriver.h
 * @brief Main time-integration loop (Driver) for the simulation.
 * * Implements the "Method of Lines" approach, decoupling the spatial discretization
 * * (SolverPolicy) from the time stepping logic.
 */

#pragma once

#include <iostream>
#include <algorithm>

#include "DriverUtils.h"

#include "../io/IO.h"

/**
 * @brief Executes the main simulation loop.
 * @tparam SolverPolicy The numerical scheme (e.g., Lax-Friedrichs, HLLC).
 * @tparam EosPolicy The equation of state (e.g., Ideal Gas).
 */
template <typename SolverPolicy, typename EosPolicy>
void run_simulation(FluidState &state, const EosPolicy &eos,
                    const Grid &grid, const SimConfig &config,
                    const SpeciesManager &specs)
{

    // =========================================================
    // 1. Time Integration Control Parameters
    // =========================================================
    double t_current = 0.0;              ///< Current simulation physical time.
    const double t_max = config.io.tmax; ///< Target physical termination time.
    double dt = 0.0;                     ///< Current time-step size.
    double cfl = config.numerics.cfl;    ///< CFL stability factor.
    int step_count = 0;                  ///< Number of steps taken (iterations).

    // =========================================================
    // 2. I/O Scheduling Parameters
    // =========================================================
    double next_io_time = 0.0;                   ///< Physical time for the next output.
    double io_interval = config.io.plt_interval; ///< Time interval between outputs.
    int file_index = 0;                          ///< Sequential index for output filenames.

    // =========================================================
    // 3. State Management (Double Buffering)
    // =========================================================
    FluidState u_current = state; ///< State at time n (Current).
    FluidState u_next;            ///< State at time n+1 (Next).

    // Allocate memory for the next state buffer
    u_next.Resize(grid, state.GetNumSpecies());

    std::cout << ">>> Simulation Started | Solver: " << SolverPolicy::name() << std::endl;

    // =========================================================
    // Main Time Loop
    // =========================================================
    while (t_current < t_max)
    {

        // -----------------------------------------------------
        // Step A: I/O Routine (Check if we hit a plot frame)
        // -----------------------------------------------------
        // Use tolerance (1e-9) to handle floating point drift
        if (t_current >= next_io_time - 1e-9)
        {
            save_data(u_current, eos, grid, file_index, config, specs);

            file_index++;
            next_io_time += io_interval;
        }

        // -----------------------------------------------------
        // Step B: Calculate Time Step (CFL Condition)
        // -----------------------------------------------------
        // Compute stable dt based on wave speeds
        double dt_computed = adaptive_dt(u_current, eos, grid, cfl);

        // Safety check for numerical degeneracy
        if (dt_computed < 1e-13)
        {
            std::cerr << "[Error] dt too small (" << dt_computed << "). Simulation aborted." << std::endl;
            break;
        }
        // -----------------------------------------------------
        // Step C: Adjust dt to hit sync points (IO or End)
        // -----------------------------------------------------
        double dt = dt_computed;

        // 1. Don't overshoot the next I/O time
        if (t_current + dt > next_io_time)
        {
            dt = next_io_time - t_current;
        }

        // 2. Don't overshoot the simulation end time
        if (t_current + dt > t_max)
        {
            dt = t_max - t_current;
        }

        // -----------------------------------------------------
        // Step D: Numerical Update
        // -----------------------------------------------------
        // 1. Fill Ghost Zones (Periodic/Outflow/Reflective)
        apply_boundary_conditions(u_current, grid);

        // 2. Evolve System: U(n+1) = U(n) + dt * Flux(U(n))
        SolverPolicy::solver(u_current, u_next, eos, grid, dt);

        // 3. Ping-Pong Buffering (Swap pointers/references)
        std::swap(u_current, u_next);

        // -----------------------------------------------------
        // Step E: Advance Counters
        // -----------------------------------------------------
        t_current += dt;
        step_count++;
    }

    // =========================================================
    // Final Output (Force output at t_max)
    // =========================================================
    if (t_current > (next_io_time - io_interval) + 1e-9)
    {
        std::cout << "Final Step Reached. Forcing output..." << std::endl;
        save_data(u_current, eos, grid, file_index, config, specs);
    }

    std::cout << "Simulation Done. Total Steps: " << step_count << std::endl;
}