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
#include <cmath>

#include "DriverUtils.h"
#include "../numerics/burnsolver/NetPynucastro.h"

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
 * @tparam BurnerPolicy The burning policy.
 */
template <typename TimeIntegratorPolicy, typename EosPolicy, typename GravityPolicy, typename BurnerPolicy>
void run_simulation(FluidState &state, const EosPolicy &eos,
                    GravityPolicy &gravity, BurnerPolicy &burn,
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
    double next_plt_time = 1e99; // Next physical time to write a plot file.
    if (config.io.plt_dt > 0)
    {
        // 计算大于当前时间的下一个输出时间点
        next_plt_time = std::floor(t_current / config.io.plt_dt + 1e-6) * config.io.plt_dt + config.io.plt_dt;
    }

    double next_chk_time = 1e99; // Next physical time to write a checkpoint file.
    if (config.io.chk_dt > 0)
    {
        next_chk_time = std::floor(t_current / config.io.chk_dt + 1e-6) * config.io.chk_dt + config.io.chk_dt;
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

    // 提取组分数量
    const int n_spec = state.GetNumSpecies();

    double dt_burn_global = 1e99;

    // =========================================================
    // 局部辅助 Lambda 函数：执行网格遍历燃烧
    // =========================================================
    auto do_burn_step = [&](FluidState &current_state, double burn_dt)
    {
        if (!config.physics.burn.use_burn)
            return; // 如果没开燃烧，直接跳过

        double local_dt_burn_min = 1e99;

        int total_cells = grid.GetTotalSize();
// 在 GPU 上，这个 for 循环就是我们要并行化的内核
#pragma omp parallel for reduction(min:local_dt_burn_min)
        for (int i = 0; i < total_cells; ++i)
        {
            double rho = current_state.rho[i];

            // 跳过低密度真空区（保护机制）
            if (rho < config.physics.burn.burn_rho_min)
                continue;

            // 1. 提取当前单元的组分到 Y_ODE
            double Y_ODE[BurnLimits::MAX_ODE_NEQ];
            current_state.get_species_to_buffer(i, Y_ODE);

            // 2. 计算内能并从 EOS 获取当前温度
            double mx = current_state.mom_x[i];
            double my = current_state.mom_y[i];
            double mz = current_state.mom_z[i];
            double e_kin = 0.5 * (mx * mx + my * my + mz * mz) / rho;
            double e_int = (current_state.eng[i] - e_kin) / rho; // 比内能

            // 假设你的 EOS 提供了这个接口：根据 rho, e_int, X_k 求 T
            double T = eos.get_temperature(rho, e_int, Y_ODE);
            if (T < 1e7)
                continue; // 温度太低（< 1e7 K），核反应速率在物理上可忽略，且极低温会引起严重数值溢出
            Y_ODE[n_spec] = T; // 将温度放在数组末尾

            // 3. 呼叫底层的 ODE 求解器执行燃烧
            double dt_rec = burn_dt;
            bool success = burn.integrate(Y_ODE, rho, burn_dt, eos, config.physics.burn, dt_rec);

            if (success) {
                local_dt_burn_min = std::min(local_dt_burn_min, dt_rec);
            }

            if (!success)
            {
                std::cerr << "[Fatal Error] Burn failed at cell " << i << " at time " << t_current << std::endl;
                exit(EXIT_FAILURE);
            }

            // 4. 将燃烧后的新组分和新温度写回流体状态
            current_state.set_species_from_buffer(i, Y_ODE);
            double T_new = Y_ODE[n_spec];

            // 5. 根据新温度和新组分，重新计算内能并更新总能量
            double e_int_new = eos.get_eint_from_T(rho, T_new, Y_ODE);
            current_state.eng[i] = rho * e_int_new + e_kin;
        }
        
        // 汇总全局最新的燃烧建议步长
        dt_burn_global = std::min(dt_burn_global, local_dt_burn_min);
    };

    bool has_burn = config.physics.burn.use_burn;

    // 初始状态强制输出 (Step 0)
    if (step_count == 0) {
        write_plt(u_current, eos, grid, plt_file_index++, t_current, config, specs);
        write_chk(u_current, grid, chk_file_index++, plt_file_index, step_count, t_current, config);
    }

    // 打印表头
    std::cout << std::left << std::setw(8) << "Step"
              << std::left << std::setw(15) << "Time"
              << std::left << std::setw(15) << "dt"
              << std::left << std::setw(15) << "dt_hydro";
    if (has_burn)
        std::cout << std::left << std::setw(15) << "dt_burn";
    std::cout << std::endl;
    std::cout << std::string(has_burn ? 68 : 53, '-') << std::endl;

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

        // 强制输出（按步数）
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

        if (config.physics.burn.use_burn)
        {
            double dt_hydro_fac = config.Get<double>("ode_dt_hydro_fac", 100.0);
            
            if (step_count == 0) {
                // For the very first step, force a tiny dt so the ODE solver can safely evaluate 
                // the initial extreme stiffness and feedback a reasonable dt_burn_global.
                dt_computed = std::min(dt_computed, 1e-13);
            } else {
                double dt_burn_limit = dt_burn_global * dt_hydro_fac;
                dt_computed = std::min(dt_computed, dt_burn_limit);
            }
        }

        // 重置 global burn limit 供这一步内部重新计算
        dt_burn_global = 1e99;

        // Safety check for numerical degeneracy
        if (dt_computed < 1e-25)
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
        // D1. Burn Step (if enabled)
        bc_handler.apply(u_current, grid); // 确保幽灵区也有物理意义
        do_burn_step(u_current, 0.5 * dt);

        // D2. Hydrodynamics Step
        bc_handler.apply(u_current, grid);
        TimeIntegratorPolicy::solve(u_current, u_next, u_scratch, eos, grid, dt, bc_handler, gravity, entropy_fix_coeff);

        // D3. Burn Step (if enabled)
        bc_handler.apply(u_next, grid);
        do_burn_step(u_next, 0.5 * dt);

        // D4. Ping-Pong Buffering (Swap pointers/references)
        std::swap(u_current, u_next);

        // -----------------------------------------------------
        // Step E: Advance Counters
        // -----------------------------------------------------
        t_current += dt;
        step_count++;

        std::cout << std::left << std::setw(8) << step_count
                  << std::scientific << std::setprecision(5)
                  << std::left << std::setw(15) << t_current
                  << std::left << std::setw(15) << dt
                  << std::left << std::setw(15) << dt_computed;
        if (has_burn)
            std::cout << std::left << std::setw(15) << dt / 2.0;
        std::cout << std::endl;
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