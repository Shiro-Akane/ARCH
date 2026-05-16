/**
 * @file Deriver.h
 * @brief Main time-integration loop (Driver) for the simulation.
 * * Implements the "Method of Lines" approach, decoupling the spatial discretization
 * * (SolverPolicy) from the time stepping logic.
 */

#pragma once

#include <iostream>
#include <algorithm>
#include <iomanip>

#include "DriverUtils.h"

#include "../io/IO.h"

/**
 * @brief Local Helper Class to wrap Boundary Condition logic.
 * * This allows the RK solver to call bc.apply() inside its stages
 * * without needing to know about SimConfig details.
 */
struct BCHandler
{
    const SimConfig &config;

    // 这是 SolverRK2 调用的接口
    void apply(FluidState &state, const Grid &grid) const
    {
        apply_boundary_conditions(state, grid, config);
    }
};

/**
 * @brief Executes the main simulation loop.
 * @tparam SolverPolicy The numerical scheme (e.g., Lax-Friedrichs, HLLC).
 * @tparam EosPolicy The equation of state (e.g., Ideal Gas).
 * @tparam GravityPolicy The gravity policy.
 */
template <typename TimeIntegratorPolicy, typename EosPolicy, typename GravityPolicy>
void run_simulation(FluidState &state, const EosPolicy &eos,
                    GravityPolicy &gravity,
                    const Grid &grid, const SimConfig &config,
                    const SpeciesManager &specs,
                    const RunState &start_state)
{

    // =========================================================
    // 1. Time Integration Control Parameters
    // =========================================================
    double t_current = start_state.time; ///< Current simulation physical time.
    const double t_max = config.io.tmax; ///< Target physical termination time.
    double dt = 0.0;                     ///< Current time-step size.
    double cfl = config.numerics.cfl;    ///< CFL stability factor.
    int step_count = start_state.step;   ///< Number of steps taken (iterations).

    // =========================================================
    // 2. I/O Scheduling Parameters
    // =========================================================

    int plt_file_index = start_state.plt_idx; // Sequential index for plot files (e.g., plt_0001.h5).
    int chk_file_index = start_state.chk_idx; // Sequential index for checkpoint files (e.g., chk_0001.h5).

    if (config.io.restart) // 如果是重启，防止覆盖重启文件导致的反复覆盖
    {
        plt_file_index += 1;
        chk_file_index += 1;
    }

    // 如果是重启，t_current 可能=0.1，plt_dt=0.05，我们要让它下次在 0.15 触发
    // 如果是全新开始(t_current=0)，则强制为 0.0 以便输出初始场
    double next_plt_time = 1e99; // Next physical time to write a plot file.
    if (config.io.plt_dt > 0)
    {
        if (t_current == 0.0 && step_count == 0)
        {
            next_plt_time = 0.0; // 初始场强制输出
        }
        else
        {
            // 计算大于当前时间的下一个输出时间点
            next_plt_time = std::floor(t_current / config.io.plt_dt + 1e-6) * config.io.plt_dt + config.io.plt_dt;
        }
    }

    double next_chk_time = 1e99; // Next physical time to write a checkpoint file.
    if (config.io.chk_dt > 0)
    {
        if (t_current == 0.0 && step_count == 0)
        {
            next_chk_time = 0.0;
        }
        else
        {
            next_chk_time = std::floor(t_current / config.io.chk_dt + 1e-6) * config.io.chk_dt + config.io.chk_dt;
        }
    }

    // =========================================================
    // 3. State Management (Double Buffering)
    // =========================================================
    FluidState u_current = state;              ///< State at time n (Current).
    FluidState u_next(grid, specs.count());    ///< State at time n+1 (Next).
    FluidState u_scratch(grid, specs.count()); ///< RK Scratch state

    // Entropy_fix_coeff for SW and Roe flux scheme
    double entropy_fix_coeff = config.numerics.entropy_fix_coeff;

    std::cout << ">>> Simulation Started | Solver: " << TimeIntegratorPolicy::name()
              << " | Entropy Fix Coeff: " << entropy_fix_coeff << std::endl;

    // update Bounday during Scratch state
    BCHandler bc_handler{config};

    // Allocate memory for the next state buffer
    u_next.Resize(grid, state.GetNumSpecies());

    std::cout << ">>> Simulation Started | Solver: " << TimeIntegratorPolicy::name() << std::endl;

    // =========================================================
    // Main Time Loop
    // =========================================================
    while (t_current < t_max)
    {

        // -----------------------------------------------------
        // Step A: I/O Routine (Check if we hit a plot frame)
        // -----------------------------------------------------
        // Use tolerance (1e-9) to handle floating point drift

        bool do_plt = false; // Check if it's time for a plot file
        bool do_chk = false; // Check if it's time for a checkpoint  file

        const double eps = 1e-10; // 1e-10 is a small tolerance to prevent floating-point precision issues

        if (config.io.plt_dt > 0 && t_current >= next_plt_time - eps)
        {
            do_plt = true;
            next_plt_time += config.io.plt_dt;
        }
        if (config.io.chk_dt > 0 && t_current >= next_chk_time - eps)
        {
            do_chk = true;
            next_chk_time += config.io.chk_dt;
        }

        // 强制输出（如果接近 t_max）
        if (step_count > 0)
        {
            if (config.io.plt_dstep > 0 && step_count % config.io.plt_dstep == 0)
                do_plt = true;
            if (config.io.chk_dstep > 0 && step_count % config.io.chk_dstep == 0)
                do_chk = true;
        }

        if (do_plt) // Plot output
            write_plt(u_current, eos, grid, plt_file_index++, t_current, config, specs);
        if (do_chk) // Checkpoint output
            write_chk(u_current, grid, chk_file_index++, plt_file_index, step_count, t_current, config);

        if (config.io.max_steps > 0 && step_count >= config.io.max_steps) // Max steps check
        {
            std::cout << "[Terminate] Max simulation steps reached." << std::endl;
            break;
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

        if (config.io.plt_dt > 0 && t_current + dt > next_plt_time) // 如果下一个时间步会错过下一个 plt 输出点，调整 dt 以精确命中
        {
            dt = std::max(1e-14, next_plt_time - t_current);
        }
        if (config.io.chk_dt > 0 && t_current + dt > next_chk_time) // 如果下一个时间步会错过下一个 chk 输出点，调整 dt 以精确命中
        {
            dt = std::max(1e-14, next_chk_time - t_current);
        }
        if (t_current + dt > t_max) // 如果下一个时间步会超过 t_max，调整 dt 以精确命中 t_max
        {
            dt = std::max(1e-14, t_max - t_current);
        }

        // -----------------------------------------------------
        // Step D: Numerical Update
        // -----------------------------------------------------
        // 1. Fill Ghost Zones (Periodic/Outflow/Reflective)
        bc_handler.apply(u_current, grid);

        // 2. Evolve System: U(n+1) = U(n) + dt * Flux(U(n))
        TimeIntegratorPolicy::solve(u_current, u_next, u_scratch, eos, grid, dt, bc_handler, gravity, entropy_fix_coeff);

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
    if (std::abs(t_current - t_max) < 1e-9)
    {
        std::cout << ">>> Target Time Reached. Forcing final output..." << std::endl;
        write_plt(u_current, eos, grid, plt_file_index++, t_current, config, specs);
        write_chk(u_current, grid, chk_file_index++, plt_file_index, step_count, t_current, config);
    }

    std::cout << ">>> Simulation Done. Total Steps: " << step_count
              << " | Final Time: " << t_current << std::endl;
}