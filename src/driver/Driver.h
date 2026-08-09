/**
 * @file Driver.h
 * @brief Main time-integration loop (Driver) for the simulation.
 * * Implements the "Method of Lines" approach, decoupling the spatial discretization
 * * (SolverPolicy) from the time stepping logic.
 */

/**
 * Workflow:
 * 1. Select the configured policy and determine a stable macro step.
 * 2. Apply hydro, diffusion, gravity, and burn operators in the documented order.
 * 3. Synchronize AMR leaves and emit diagnostics before continuing the evolution.
 */

#pragma once

#include <iostream>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

// Utilities and physics/numerics definitions
#include "DriverUtils.h"
#include "DriverBurn.h"
#include "DriverControl.h"
#include "../numerics/burnsolver/Networks.h"
#include "../numerics/diffusion/DiffDispatch.h"
#include "../numerics/diffusion/DiffFunction.h"
#include "../io/IO.h"

#include "../amr/AMRControl.h"
#include "../numerics/burnsolver/BurnerHandle.h"
#include "../physics/gravity/IGravityPolicy.h"
#include "../numerics/integrator/IHydroSolver.h"

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
template <typename EosPolicy>
void run_simulation(amr::AMRControl &amr_ctrl, const EosPolicy &eos,
                    const Physical::Gravity::IGravityPolicy* gravity,
                    const BurnerHandle<EosPolicy> &burn,
                    const Numerics::IHydroSolver* hydro,
                    void (*integrator_solve)(amr::AMRControl&, double, BCHandler&, const Physical::Gravity::IGravityPolicy*, const Numerics::IHydroSolver*, const NumericsConfig&),
                    const std::string& integrator_name,
                    const SimConfig &config,
                    const SpeciesManager &specs,
                    const RunState &start_state)
{
    // 1. Initialize simulation controller (handles IO timing, dt logic, and step counting)
    SimulationController ctrl(config, start_state);

    // GhostExchange and FluxRegister are now in amr_ctrl

    const NumericsConfig &num_cfg = config.numerics;
    BCHandler bc_handler{config};

    // AMR owns no EOS type.  Bind the selected policy once as a batch callback
    // so pressure, temperature, and entropy-proxy indicators use the same
    // thermodynamics as the flux, burn, and diffusion operators.
    amr_ctrl.tree->SetThermodynamicEvaluator([&eos](const FluidState& state,
                                                     std::vector<double>* pressure,
                                                     std::vector<double>* temperature,
                                                     std::vector<double>* gamma1) {
        const int total_size = static_cast<int>(state.rho.size());
        const int n_species = state.GetNumSpecies();
        if (pressure) pressure->assign(total_size, 0.0);
        if (temperature) temperature->assign(total_size, 0.0);
        if (gamma1) gamma1->assign(total_size, std::numeric_limits<double>::quiet_NaN());
        std::vector<double> Xi(n_species, 0.0);
        for (int index = 0; index < total_size; ++index) {
            for (int species = 0; species < n_species; ++species)
                Xi[species] = state.X(species, index);
            const FluidVector U = state.get(index);
            const double pressure_value = (pressure || gamma1) ? eos.get_pressure(U, Xi.data()) : 0.0;
            if (pressure) (*pressure)[index] = pressure_value;
            if (temperature && U.rho > 0.0) {
                const double kinetic = 0.5 * (U.mom_u * U.mom_u + U.mom_v * U.mom_v +
                                               U.mom_w * U.mom_w) / U.rho;
                (*temperature)[index] = eos.get_temperature(U.rho, (U.eng - kinetic) / U.rho, Xi.data());
            }
            if (gamma1 && U.rho > 0.0 && pressure_value > 0.0) {
                const double sound_speed = eos.get_sound_speed(U, pressure_value, Xi.data());
                if (std::isfinite(sound_speed) && sound_speed > 0.0) {
                    (*gamma1)[index] = U.rho * sound_speed * sound_speed / pressure_value;
                }
            }
        }
    });

    const auto p_func = [](const FluidVector& U, const double* Xi, const void* context) -> double {
        return static_cast<const EosPolicy*>(context)->get_pressure(U, Xi);
    };
    const auto t_func = [](const FluidVector& U, const double* Xi, const void* context) -> double {
        if (U.rho <= 0.0) return 0.0;
        const double kinetic = 0.5 * (U.mom_u * U.mom_u + U.mom_v * U.mom_v +
                                      U.mom_w * U.mom_w) / U.rho;
        return static_cast<const EosPolicy*>(context)->get_temperature(U.rho, (U.eng - kinetic) / U.rho, Xi);
    };
    const auto gamma1_func = [](const FluidVector& U, const double* Xi, const void* context) -> double {
        if (U.rho <= 0.0) return std::numeric_limits<double>::quiet_NaN();
        const EosPolicy* active_eos = static_cast<const EosPolicy*>(context);
        const double pressure = active_eos->get_pressure(U, Xi);
        const double sound_speed = active_eos->get_sound_speed(U, pressure, Xi);
        if (!std::isfinite(pressure) || pressure <= 0.0 || !std::isfinite(sound_speed) || sound_speed <= 0.0) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        return U.rho * sound_speed * sound_speed / pressure;
    };
    // Derivative plot fields use centered stencils. Synchronize halos at every
    // plot event so VORT and DIVV never sample an outdated neighboring block.
    const auto synchronize_plot_ghosts = [&] {
#pragma omp parallel for schedule(dynamic, 1)
        for (size_t i = 0; i < amr_ctrl.tree->GetActiveBlocks().size(); ++i) {
            amr::Block& block = amr_ctrl.pool->GetBlock(amr_ctrl.tree->GetActiveBlocks()[i]);
            bc_handler.apply(block.fluid_state, block.grid);
        }
        amr_ctrl.ghost_exchange.ExecuteExchange(amr_ctrl.pool, amr_ctrl.tree,
                                                config.grid.dim, &amr::Block::fluid_state);
    };
    // The initialization dispatcher cannot safely instantiate a runtime EOS.
    // Complete deferred thermodynamic regrids with valid ghost zones and
    // conservative prolongation before the first output or hydro step.
    const int deferred_initial_passes = amr_ctrl.tree->ConsumeDeferredInitialRefinement();
    if (deferred_initial_passes > 0) {
        std::cout << "[Dispatch] Performing initial AMR refinement loop..." << std::endl;
        for (int pass = 0; pass < deferred_initial_passes; ++pass) {
#pragma omp parallel for schedule(dynamic, 1)
            for (size_t i = 0; i < amr_ctrl.tree->GetActiveBlocks().size(); ++i) {
                amr::Block& block = amr_ctrl.pool->GetBlock(amr_ctrl.tree->GetActiveBlocks()[i]);
                bc_handler.apply(block.fluid_state, block.grid);
            }
            amr_ctrl.ghost_exchange.ExecuteExchange(amr_ctrl.pool, amr_ctrl.tree,
                                                    config.grid.dim, &amr::Block::fluid_state);
            if (!amr_ctrl.tree->Regrid(config)) break;
            std::cout << "           -> Refining initial condition (Pass " << pass + 1 << ")..." << std::endl;
        }
#pragma omp parallel for schedule(dynamic, 1)
        for (size_t i = 0; i < amr_ctrl.tree->GetActiveBlocks().size(); ++i) {
            amr::Block& block = amr_ctrl.pool->GetBlock(amr_ctrl.tree->GetActiveBlocks()[i]);
            bc_handler.apply(block.fluid_state, block.grid);
        }
        amr_ctrl.ghost_exchange.ExecuteExchange(amr_ctrl.pool, amr_ctrl.tree,
                                                config.grid.dim, &amr::Block::fluid_state);
    }

    std::cout << ">>> Simulation Started | Solver: " << integrator_name
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
    bool reported_composite_diffusion = false;

    // Output initial state if we are at step 0
    if (ctrl.should_print_header())
    {
        synchronize_plot_ghosts();
        write_plt(amr_ctrl, p_func, t_func, gamma1_func, &eos, ctrl.plt_file_index++, ctrl.t_current, config, specs);
        write_chk(amr_ctrl, ctrl.chk_file_index++, ctrl.plt_file_index, ctrl.step_count, ctrl.t_current, config);
        ctrl.print_header(has_burn, has_diff);
    }

    // =========================================================
    // Main Time Loop (Method of Lines)
    // =========================================================
    while (!ctrl.is_finished())
    {
        // -----------------------------------------------------
        // Step A: IO Routine & AMR Regrid
        // -----------------------------------------------------
        if (ctrl.step_count % config.amr.regrid_interval == 0) {
            // Apply physical boundary conditions first.  Exchange then replaces
            // the ghost layers of internal AMR faces with neighboring data.
            #pragma omp parallel for schedule(dynamic, 1)
            for (size_t i = 0; i < amr_ctrl.tree->GetActiveBlocks().size(); ++i) {
                amr::Block& b = amr_ctrl.pool->GetBlock(amr_ctrl.tree->GetActiveBlocks()[i]);
                bc_handler.apply(b.fluid_state, b.grid);
            }
            amr_ctrl.ghost_exchange.ExecuteExchange(amr_ctrl.pool, amr_ctrl.tree, config.grid.dim, &amr::Block::fluid_state);
            const bool mesh_changed = amr_ctrl.tree->Regrid(config);

            // Regrid creates (or restricts into) blocks whose ghost zones have
            // not participated in the pre-regrid exchange. RK stage 1 reads
            // those zones immediately, so synchronize the new hierarchy before
            // any reconstruction can use a reset halo value.
            if (mesh_changed) {
                #pragma omp parallel for schedule(dynamic, 1)
                for (size_t i = 0; i < amr_ctrl.tree->GetActiveBlocks().size(); ++i) {
                    amr::Block& b = amr_ctrl.pool->GetBlock(amr_ctrl.tree->GetActiveBlocks()[i]);
                    bc_handler.apply(b.fluid_state, b.grid);
                }
                amr_ctrl.ghost_exchange.ExecuteExchange(amr_ctrl.pool, amr_ctrl.tree, config.grid.dim, &amr::Block::fluid_state);
            }
        }

        bool do_plt, do_chk;
        ctrl.check_io(do_plt, do_chk);

        if (do_plt) {
            synchronize_plot_ghosts();
            write_plt(amr_ctrl, p_func, t_func, gamma1_func, &eos, ctrl.plt_file_index++, ctrl.t_current, config, specs);
        }
        if (do_chk) {
            write_chk(amr_ctrl, ctrl.chk_file_index++, ctrl.plt_file_index, ctrl.step_count, ctrl.t_current, config);
        }

        // -----------------------------------------------------
        // Step B: Calculate Time Step (CFL Condition)
        // -----------------------------------------------------
        double dt_hydro = 1e99;
        const auto& active_blocks = amr_ctrl.tree->GetActiveBlocks();
        for (int block_id : active_blocks) {
            amr::Block& b = amr_ctrl.pool->GetBlock(block_id);
            double dt_b = adaptive_dt(b.fluid_state, eos, b.grid, cfl);
            dt_hydro = std::min(dt_hydro, dt_b);
        }

        // The diffusion operator reports a forward-Euler stability step.  STS
        // removes that O(dx^2) restriction from the macro step, except when
        // the configured RKL stage cap is genuinely exhausted.
        double dt_diff_fe = 1e99;
        double dt_diff_sts_limit = 1e99;
        if (has_diff) {
            for (const int block_id : active_blocks) {
                const amr::Block& block = amr_ctrl.pool->GetBlock(block_id);
                dt_diff_fe = std::min(dt_diff_fe,
                    DiffFlux::adaptive_dt_diff(block.fluid_state, eos, block.grid, config, 1.0));
            }
            const std::string& diff_integrator = config.physics.diffusion.integrator;
            const bool rkl1 = diff_integrator == "RKL1" || diff_integrator == "rkl1";
            const bool rkl2 = diff_integrator == "RKL2" || diff_integrator == "rkl2";
            if (!rkl1 && !rkl2) {
                throw std::runtime_error("Unknown diffusion integrator: " + diff_integrator);
            }
            const DiffFunction::RKLOrder rkl_order = rkl1
                ? DiffFunction::RKLOrder::First : DiffFunction::RKLOrder::Second;
            const int usable_stages = DiffFunction::usable_max_stages(
                rkl_order, config.physics.diffusion.max_stages);
            dt_diff_sts_limit = DiffFunction::stable_step(
                rkl_order, dt_diff_fe, config.physics.diffusion.diff_cfl, usable_stages);
        }

        double dt_computed = ctrl.calculate_next_dt(std::min(dt_hydro, dt_diff_sts_limit), dt_burn_global);
        dt_burn_global = 1e99; // Reset for internal computation
        double dt = ctrl.sync_dt(dt_computed);

        // -----------------------------------------------------
        // Step C: Numerical Update (Operator Splitting)
        // -----------------------------------------------------

        // C1. Burn Step (1/2 dt)
        if (has_burn) {
            std::vector<double> dt_burn_by_block(active_blocks.size(), 1e99);
            #pragma omp parallel for schedule(dynamic, 1)
            for (size_t i = 0; i < active_blocks.size(); ++i) {
                amr::Block& b = amr_ctrl.pool->GetBlock(active_blocks[i]);
                bc_handler.apply(b.fluid_state, b.grid);
                execute_burn_step(b.fluid_state, 0.5 * dt, eos, burn, b.grid, config, dt_burn_by_block[i]);
            }
            for (double dt_burn_block : dt_burn_by_block) {
                dt_burn_global = std::min(dt_burn_global, dt_burn_block);
            }
        }

        // C2. Hydrodynamics Step (dt)
        #pragma omp parallel for schedule(dynamic, 1)
        for (size_t i = 0; i < active_blocks.size(); ++i) {
            amr::Block& b = amr_ctrl.pool->GetBlock(active_blocks[i]);
            bc_handler.apply(b.fluid_state, b.grid);
        }
        // C2. Hydro Step (dt)
        amr_ctrl.ghost_exchange.ExecuteExchange(amr_ctrl.pool, amr_ctrl.tree, config.grid.dim, &amr::Block::fluid_state);

        integrator_solve(amr_ctrl, dt, bc_handler, gravity, hydro, num_cfg);

        // C3. Diffusion Step (dt)
        if (has_diff) {
            if (active_blocks.size() > 1) {
                const std::string& diff_integrator = config.physics.diffusion.integrator;
                const bool rkl1 = diff_integrator == "RKL1" || diff_integrator == "rkl1";
                const bool rkl2 = diff_integrator == "RKL2" || diff_integrator == "rkl2";
                if (!rkl1 && !rkl2) {
                    throw std::runtime_error("Unknown diffusion integrator: " + diff_integrator);
                }
                if (!reported_composite_diffusion) {
                    std::cout << "[Diffusion] multi-block AMR uses composite "
                              << (rkl1 ? "RKL1" : "RKL2")
                              << " STS with stage ghost synchronization and reflux." << std::endl;
                    reported_composite_diffusion = true;
                }
                if (rkl1) {
                    Numerics::Diffusion::advance_amr_rkl1(amr_ctrl, dt, dt_diff_fe, bc_handler, eos, config);
                } else {
                    Numerics::Diffusion::advance_amr_rkl2(amr_ctrl, dt, dt_diff_fe, bc_handler, eos, config);
                }
            } else {
                Numerics::Diffusion::dispatch_diffusion(config, [&](auto& integrator) {
                    amr::Block& block = amr_ctrl.pool->GetBlock(active_blocks.front());
                    integrator.integrate(block.fluid_state, eos, block.grid, config, dt, dt_diff_fe, bc_handler);
                });
            }
        }
        // C4. Burn Step (1/2 dt)
        if (has_burn) {
            std::vector<double> dt_burn_by_block(active_blocks.size(), 1e99);
            #pragma omp parallel for schedule(dynamic, 1)
            for (size_t i = 0; i < active_blocks.size(); ++i) {
                amr::Block& b = amr_ctrl.pool->GetBlock(active_blocks[i]);
                bc_handler.apply(b.fluid_state, b.grid);
                execute_burn_step(b.fluid_state, 0.5 * dt, eos, burn, b.grid, config, dt_burn_by_block[i]);
            }
            for (double dt_burn_block : dt_burn_by_block) {
                dt_burn_global = std::min(dt_burn_global, dt_burn_block);
            }
        }

        // -----------------------------------------------------
        // Step D: Advance Counters
        // -----------------------------------------------------
        ctrl.advance(dt);
        ctrl.print_step(dt, dt_hydro, has_burn ? dt / 2.0 : 0.0, dt_diff_fe, has_burn, has_diff);
    }

    // =========================================================
    // Final Output (Force output at t_max)
    // =========================================================
    if (std::abs(ctrl.t_current - ctrl.t_max) < 1e-9)
    {
        std::cout << ">>> Target Time Reached. Forcing final output..." << std::endl;
        synchronize_plot_ghosts();
        write_plt(amr_ctrl, p_func, t_func, gamma1_func, &eos, ctrl.plt_file_index++, ctrl.t_current, config, specs);
        write_chk(amr_ctrl, ctrl.chk_file_index++, ctrl.plt_file_index, ctrl.step_count, ctrl.t_current, config);
    }

    std::cout << ">>> Simulation Done. Total Steps: " << ctrl.step_count
              << " | Final Time: " << ctrl.t_current << std::endl;
}
