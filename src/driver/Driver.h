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
#include <fstream>
#include <stdexcept>
#include "DriverUtils.h"
#include "../numerics/burnsolver/Networks.h"

#ifdef _OPENMP
#include <omp.h>
#endif

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

    // Pass NumericsConfig directly instead of just entropy_fix_coeff
    const NumericsConfig &num_cfg = config.numerics;

    std::cout << ">>> Simulation Started | Solver: " << TimeIntegratorPolicy::name()
              << " | Entropy Fix Coeff: " << num_cfg.entropy_fix_coeff;
#ifdef _OPENMP
    std::cout << " | OpenMP: ON (max threads=" << omp_get_max_threads() << ")";
#else
    std::cout << " | OpenMP: OFF";
#endif
    std::cout << std::endl;

    // update Bounday during Scratch state
    BCHandler bc_handler{config};

    // Allocate memory for the next state buffer
    u_next.Resize(grid, state.GetNumSpecies());

    // 提取组分数量
    const int n_spec = state.GetNumSpecies();

    double dt_burn_global = 1e99;
    double dt_old = config.GetCustomParam("dt_init", 1e-16);

    // =========================================================
    // 局部辅助 Lambda 函数：执行网格遍历燃烧
    // =========================================================
    auto do_burn_step = [&](FluidState &current_state, double burn_dt)
    {
        if (!config.physics.burn.use_burn)
            return; // 如果没开燃烧，直接跳过

        double local_dt_burn_min = 1e99;

        int invalid_composition_count = 0;
        int first_invalid_cell = -1;
        double first_invalid_sum = 0.0;
        double first_invalid_min = 0.0;
        double first_invalid_max = 0.0;

        int total_cells = grid.GetTotalSize();
        // Each cell owns its ODE state, network evaluation and LU factorization.
        // Dynamic scheduling is important because stiff substep counts vary strongly
        // across the reaction front; nested teams inside a 22x22 LU are counterproductive.
#pragma omp parallel for schedule(dynamic, 1) reduction(min:local_dt_burn_min)
        for (int i = 0; i < total_cells; ++i)
        {
            double rho = current_state.rho[i];

            // 跳过低密度真空区（保护机制）
            if (rho < config.physics.burn.nuclearDensMin)
                continue;

            // 1. 提取当前单元的组分到 Y_ODE
            double Y_ODE[BurnLimits::MAX_ODE_NEQ]{};
            current_state.get_species_to_buffer(i, Y_ODE);

            // A network state is empty only when the complete composition is
            // invalid.  Testing Y_ODE[0] and Y_ODE[1] is incorrect: H1/He3
            // are normally zero in aprox19/21 helium/carbon fuel, and He4/C12
            // can both be depleted in an evolved alpha-chain state.
            double composition_sum = 0.0;
            double composition_min = Y_ODE[0];
            double composition_max = Y_ODE[0];
            bool composition_is_finite = true;
            bool composition_has_negative = false;
            for (int k = 0; k < n_spec; ++k)
            {
                const double xk = Y_ODE[k];
                composition_is_finite = composition_is_finite && std::isfinite(xk);
                composition_has_negative = composition_has_negative
                                         || xk < -10.0 * config.physics.burn.smallx;
                composition_sum += xk;
                composition_min = std::min(composition_min, xk);
                composition_max = std::max(composition_max, xk);
            }

            const bool composition_is_valid = composition_is_finite
                                           && !composition_has_negative
                                           && std::isfinite(composition_sum)
                                           && composition_sum > 1.0e-13
                                           && std::abs(composition_sum - 1.0) <= 1.0e-6;
            if (!composition_is_valid)
            {
#pragma omp critical(burn_invalid_composition)
                {
                    ++invalid_composition_count;
                    if (first_invalid_cell < 0)
                    {
                        first_invalid_cell = i;
                        first_invalid_sum = composition_sum;
                        first_invalid_min = composition_min;
                        first_invalid_max = composition_max;
                    }
                }
                continue;
            }

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

            // 6. 核能限制器 (Enuc Limiter)
            // 仅当 enucDtFactor > 0 时才启用限制器。
            if (config.physics.burn.enucDtFactor > 0.0) {
                double delta_e = std::abs(e_int_new - e_int);
                if (burn_dt > 0.0) {
                    double enuc_rate = delta_e / burn_dt;
                    
                    // 【防除 0 保护】模仿 FLASH: 计算倒数 (enuc / eint)
                    double energyRatioInv = enuc_rate / std::max(e_int_new, 1e-20);
                    
                    // 仅当能量变化率显著时，才将其纳入时间步限制
                    if (energyRatioInv > 1e-30) {
                        // 相当于 dt = enucDtFactor * (eint / enuc)
                        double dt_enuc_limit = config.physics.burn.enucDtFactor / energyRatioInv;
                        local_dt_burn_min = std::min(local_dt_burn_min, dt_enuc_limit);
                    }
                }
            }
        }

        if (invalid_composition_count > 0)
        {
            std::cerr << "[Fatal Error] Invalid complete composition before burn: "
                      << invalid_composition_count << " cell(s); first cell="
                      << first_invalid_cell << ", sum(X)=" << first_invalid_sum
                      << ", min(X)=" << first_invalid_min
                      << ", max(X)=" << first_invalid_max
                      << ", network=" << config.physics.burn.network_name
                      << std::endl;
            throw std::runtime_error("Invalid complete composition before burn");
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

    std::string log_filename = config.io.out_dir + "/" + config.io.base_name + "_log.dat";
    std::ofstream log_file(log_filename, (step_count == 0) ? std::ios::trunc : std::ios::app);
    if (!log_file.is_open()) {
        std::cerr << "[Warning] Could not open log file: " << log_filename << std::endl;
    }

    // 打印表头
    if (step_count == 0) {
        auto print_header = [&](std::ostream& os) {
            os << std::left << std::setw(8) << "Step"
               << std::left << std::setw(15) << "Time"
               << std::left << std::setw(15) << "dt"
               << std::left << std::setw(15) << "dt_hydro";
            if (has_burn)
                os << std::left << std::setw(15) << "dt_burn";
            os << std::endl;
            os << std::string(has_burn ? 68 : 53, '-') << std::endl;
        };
        print_header(std::cout);
        if (log_file.is_open()) print_header(log_file);
    }

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
        // Computed stable dt based on wave speeds
        double dt_computed = adaptive_dt(u_current, eos, grid, cfl);
        
        // Restrict timestep growth to avoid hydro instabilities from sudden energy release
        if (step_count > 0) {
            double dt_grow = config.GetCustomParam("tstep_change_factor", 1.2);
            dt_computed = std::min(dt_computed, dt_old * dt_grow);
        }

        if (config.physics.burn.use_burn)
        {
            if (step_count == 0) {
                // For the very first step, force a tiny dt (e.g., dt_init)
                double dt_init = config.GetCustomParam("dt_init", 1e-16);
                dt_computed = std::min(dt_computed, dt_init);
            } else {
                dt_computed = std::min(dt_computed, dt_burn_global);
            }
        }
        
        dt_old = dt_computed;

        // 重置 global burn limit 供这一步内部重新计算
        dt_burn_global = 1e99;

        // Safety check for numerical degeneracy
        double dt_min = config.GetCustomParam("dt_min", 1e-20);
        if (dt_computed < dt_min)
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
        TimeIntegratorPolicy::solve(u_current, u_next, u_scratch, eos, grid, dt, bc_handler, gravity, num_cfg);

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

        auto print_step = [&](std::ostream& os) {
            os << std::left << std::setw(8) << step_count
               << std::scientific << std::setprecision(5)
               << std::left << std::setw(15) << t_current
               << std::left << std::setw(15) << dt
               << std::left << std::setw(15) << dt_computed;
            if (has_burn)
                os << std::left << std::setw(15) << dt / 2.0;
            os << std::endl;
        };
        print_step(std::cout);
        if (log_file.is_open()) print_step(log_file);
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
