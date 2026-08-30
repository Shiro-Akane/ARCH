/**
 * @file DriverControl.h
 * @brief Handles simulation state tracking, I/O scheduling, and timestep alignment.
 *
 * Workflow:
 * 1. Initializes timing and index tracking based on whether it is a restart.
 * 2. Checks conditions for writing plot/checkpoint files (I/O).
 * 3. Restricts dt growth based on user-defined limits and burn safety factors.
 * 4. Ensures the final timestep perfectly aligns with the targeted maximum simulation time.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>

#include "../core/RuntimeParams.h"

struct SimulationController
{
    const SimConfig &config;
    int step_count;
    double t_current;
    const double t_max;

    int plt_file_index;
    int chk_file_index;

    double next_plt_time;
    double next_chk_time;

    double dt_old;
    bool suppress_step_io_once;

    SimulationController(const SimConfig &cfg, const RunState &start_state)
        : config(cfg),
          step_count(start_state.step),
          t_current(start_state.time),
          t_max(cfg.io.tmax),
          plt_file_index(start_state.plt_idx),
          chk_file_index(start_state.chk_idx),
          next_plt_time(next_scheduled_time(start_state.time, cfg.io.plt_dt)),
          next_chk_time(next_scheduled_time(start_state.time, cfg.io.chk_dt)),
          dt_old(start_state.has_timestep_state
                     ? start_state.dt_old
                     : (cfg.io.restart
                            ? 1.0e99
                            : cfg.GetCustomParam("dt_init", 1e-16))),
          suppress_step_io_once(cfg.io.restart)
    {
        if (config.io.restart)
        {
            chk_file_index += 1;
        }

    }

    static double time_tolerance(double lhs, double rhs, double interval = 0.0)
    {
        const double scale = std::max(
            {std::abs(lhs), std::abs(rhs), std::abs(interval),
             std::numeric_limits<double>::min()});
        return 64.0 * std::numeric_limits<double>::epsilon() * scale;
    }

    static double next_scheduled_time(double current_time, double interval)
    {
        if (!(interval > 0.0)) return 1.0e99;
        double next =
            (std::floor(current_time / interval) + 1.0) * interval;
        if (next <= current_time +
                        time_tolerance(current_time, next, interval)) {
            next += interval;
        }
        return next;
    }

    bool reached_target_time() const
    {
        const double target_tolerance = 1.0e-12 * std::max(
            {std::abs(t_current), std::abs(t_max),
             std::numeric_limits<double>::min()});
        return t_current >= t_max ||
               t_max - t_current <= target_tolerance;
    }

    bool is_finished() const
    {
        if (config.io.max_steps > 0 && step_count >= config.io.max_steps)
        {
            std::cout << "[Terminate] Max simulation steps reached." << std::endl;
            return true;
        }
        // Repeated output-time alignment can accumulate a few ulps below
        // t_max. Treat a relative 1e-12 remainder as complete instead of
        // forcing an artificial tiny step that creates a duplicate final H5
        // and amplifies the ENUC roundoff diagnostic by division by tiny dt.
        return reached_target_time();
    }

    void advance(double dt)
    {
        t_current += dt;
        if (reached_target_time())
            t_current = t_max;
        step_count++;
    }

    bool should_write_initial_output() const
    {
        return step_count == 0 && !config.io.restart;
    }

    void print_header(bool has_burn, bool has_diffusion) const
    {
        std::cout << std::left << std::setw(8) << "Step"
                  << std::left << std::setw(15) << "Time"
                  << std::left << std::setw(15) << "dt"
                  << std::left << std::setw(15) << "dt_hydro";
        if (has_burn)
            std::cout << std::left << std::setw(15) << "dt_burn";
        if (has_diffusion)
            std::cout << std::left << std::setw(15) << "dt_diff";
        std::cout << std::endl;
        std::cout << "-----------------------------------------------------";
        if (has_burn) std::cout << "---------------";
        if (has_diffusion) std::cout << "---------------";
        std::cout << std::endl;
    }

    void print_step(double dt, double dt_hydro, double dt_burn, double dt_diff_limit, bool has_burn, bool has_diffusion) const
    {
        std::cout << std::left << std::setw(8) << step_count
                  << std::scientific << std::setprecision(5)
                  << std::left << std::setw(15) << t_current
                  << std::left << std::setw(15) << dt
                  << std::left << std::setw(15) << dt_hydro;
        if (has_burn)
            std::cout << std::left << std::setw(15) << dt_burn;
        if (has_diffusion)
            std::cout << std::left << std::setw(15) << dt_diff_limit;
        std::cout << std::endl;
    }

    void check_io(bool &do_plt, bool &do_chk)
    {
        do_plt = false;
        do_chk = false;
        if (config.io.plt_dt > 0 &&
            t_current + time_tolerance(
                t_current, next_plt_time, config.io.plt_dt) >= next_plt_time)
        {
            do_plt = true;
            next_plt_time = next_scheduled_time(
                t_current, config.io.plt_dt);
        }
        if (config.io.chk_dt > 0 &&
            t_current + time_tolerance(
                t_current, next_chk_time, config.io.chk_dt) >= next_chk_time)
        {
            do_chk = true;
            next_chk_time = next_scheduled_time(
                t_current, config.io.chk_dt);
        }

        if (step_count > 0 && !suppress_step_io_once)
        {
            if (config.io.plt_dstep > 0 && step_count % config.io.plt_dstep == 0)
                do_plt = true;
            if (config.io.chk_dstep > 0 && step_count % config.io.chk_dstep == 0)
                do_chk = true;
        }
        suppress_step_io_once = false;
    }

    double calculate_next_dt(double dt_hydro, double dt_burn_global)
    {
        double dt_computed = dt_hydro;

        if (step_count > 0)
        {
            double dt_grow = config.GetCustomParam("tstep_change_factor", 1.2);
            dt_computed = std::min(dt_computed, dt_old * dt_grow);
        }

        if (config.physics.burn.use_burn)
        {
            if (step_count == 0)
            {
                double dt_init = config.GetCustomParam("dt_init", 1e-16);
                dt_computed = std::min(dt_computed, dt_init);
            }
            else
            {
                dt_computed = std::min(dt_computed, dt_burn_global);
            }
        }

        dt_old = dt_computed;

        double dt_min = config.GetCustomParam("dt_min", 1e-20);
        if (dt_computed < dt_min)
        {
            throw std::runtime_error("dt too small. Simulation aborted.");
        }

        return dt_computed;
    }

    double sync_dt(double dt_computed) const
    {
        double dt = dt_computed;
        if (config.io.plt_dt > 0 && t_current + dt > next_plt_time)
        {
            dt = next_plt_time - t_current;
        }
        if (config.io.chk_dt > 0 && t_current + dt > next_chk_time)
        {
            dt = next_chk_time - t_current;
        }
        if (t_current + dt > t_max)
        {
            dt = t_max - t_current;
        }
        return dt;
    }
};
