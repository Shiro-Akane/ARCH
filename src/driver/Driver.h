/**
 * @file Driver.h
 * @brief Main time-integration loop (Driver) for the simulation.
 * Implements the "Method of Lines" approach, decoupling the spatial discretization
 * (SolverPolicy) from the time stepping logic.
 */

/**
 * Workflow:
 * 1. Select the configured policy and determine a stable macro step.
 * 2. Apply hydro, diffusion, gravity, and burn operators in the documented order.
 * 3. Synchronize AMR leaves and emit diagnostics before continuing the evolution.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

// Driver-local orchestration.
#include "DriverBurn.h"
#include "DriverControl.h"
#include "DriverUtils.h"
#include "StageScheduler.h"
#include "TopologyIdentityRegistry.h"

// AMR and I/O services.
#include "../amr/AMRControl.h"
#include "../io/IO.h"

// Numerical and physical policy interfaces.
#include "../numerics/burnsolver/BurnerHandle.h"
#include "../numerics/burnsolver/Networks.h"
#include "../numerics/diffusion/DiffDispatch.h"
#include "../numerics/diffusion/DiffFunction.h"
#include "../numerics/integrator/IHydroSolver.h"
#include "../physics/gravity/IGravityPolicy.h"

#ifdef _OPENMP
#include <omp.h>
#endif

/**
 * @brief Executes the main simulation loop.
 * @tparam SolverPolicy Numerical flux policy, for example HLLC.
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
    // Initialize output scheduling, time-step control, and run counters.
    SimulationController ctrl(config, start_state);

    const NumericsConfig &num_cfg = config.numerics;
    BCHandler bc_handler{config};

    using arch::scheduler::MonotonicSchedulerClock;
    using arch::scheduler::ScopedStageBinding;
    using arch::scheduler::StageExecutionContext;
    using arch::state::ExecutionSide;
    using arch::state::StateResidencyLedger;
    using arch::state::StateSlot;
    using arch::topology::LogicalBlockIdentity;
    using arch::topology::TopologyDomainBounds;
    using arch::topology::TopologyIdentityRegistry;
    using arch::topology::TopologyObservation;

    TopologyIdentityRegistry topology_registry(TopologyDomainBounds{
        config.grid.dim,
        {static_cast<std::uint32_t>(std::max(1, config.grid.nblockx1)),
         static_cast<std::uint32_t>(std::max(1, config.grid.nblockx2)),
         static_cast<std::uint32_t>(std::max(1, config.grid.nblockx3))},
        config.amr.lrefinemax});
    MonotonicSchedulerClock scheduler_clock;
    std::unique_ptr<StateResidencyLedger> residency_ledger;
    std::vector<amr::BlockHandle> stage_handles;

    const auto observe_topology = [&] {
        std::vector<TopologyObservation> observations;
        const auto& active = amr_ctrl.tree->GetActiveBlocks();
        observations.reserve(active.size());
        for (const int pool_index : active) {
            const amr::Block& block = amr_ctrl.pool->GetBlock(pool_index);
            observations.push_back({
                pool_index,
                LogicalBlockIdentity{
                    config.grid.dim, block.level, block.logical_x1,
                    block.logical_x2, block.logical_x3}});
        }
        return observations;
    };

    const auto current_interior_version = [&] {
        if (!residency_ledger || stage_handles.empty())
            throw std::logic_error("state residency is not initialized");
        const arch::state::StateVersion version = residency_ledger->inspect(
            {stage_handles.front(), StateSlot::Current}).interior.version;
        for (const amr::BlockHandle handle : stage_handles) {
            if (residency_ledger->inspect({handle, StateSlot::Current})
                    .interior.version != version) {
                throw std::logic_error(
                    "active Current interiors do not share one version");
            }
        }
        return version;
    };

    const auto publish_current_ghost = [&] {
        StageExecutionContext context{
            ExecutionSide::Host, *residency_ledger, scheduler_clock};
        (void)arch::scheduler::complete_boundary(
            context, stage_handles, StateSlot::Current,
            current_interior_version(),
            [](StateSlot, arch::state::StateVersion,
               arch::state::CompletionToken token) { return token; });
    };

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
    // Apply physical boundaries and exchange every internal AMR face through
    // one path. Regrid, hydro, and derivative plot fields all require the same
    // synchronized fluid halos.
    const auto synchronize_fluid_ghosts = [&] {
#pragma omp parallel for schedule(dynamic, 1)
        for (size_t i = 0; i < amr_ctrl.tree->GetActiveBlocks().size(); ++i) {
            amr::Block& block = amr_ctrl.pool->GetBlock(amr_ctrl.tree->GetActiveBlocks()[i]);
            bc_handler.apply(block.fluid_state, block.grid);
        }
        amr_ctrl.ghost_exchange.ExecuteExchange(amr_ctrl.pool, amr_ctrl.tree,
                                                config.grid.dim, &amr::Block::fluid_state);
        if (residency_ledger && !stage_handles.empty())
            publish_current_ghost();
    };
    // Complete deferred thermodynamic regrids with synchronized ghost zones
    // and conservative prolongation before the first output or hydro step.
    const int deferred_initial_passes = amr_ctrl.tree->ConsumeDeferredInitialRefinement();
    if (deferred_initial_passes > 0) {
        std::cout << "[Dispatch] Performing initial AMR refinement loop..." << std::endl;
        for (int pass = 0; pass < deferred_initial_passes; ++pass) {
            synchronize_fluid_ghosts();
            if (!amr_ctrl.tree->Regrid(config)) break;
            std::cout << "           -> Refining initial condition (Pass " << pass + 1 << ")..." << std::endl;
        }
        synchronize_fluid_ghosts();
    }

    auto initial_candidate =
        topology_registry.stage_adoption(observe_topology());
    std::unique_ptr<StateResidencyLedger> staged_initial_ledger;
    std::vector<amr::BlockHandle> staged_initial_handles;
    const auto initial_topology = topology_registry.commit_after_success(
        std::move(initial_candidate), [&](const auto& proposed) {
            auto replacement =
                std::make_unique<StateResidencyLedger>(proposed.epoch);
            const arch::scheduler::PublicationWitness initial_witness =
                scheduler_clock.next_publication();
            for (const amr::BlockHandle handle
                 : proposed.handles_in_observation_order) {
                replacement->register_block(handle, initial_witness.version,
                                            initial_witness.completion);
            }
            // A restart can deliberately skip the first regrid and initial
            // output. Establish real host ghost data and publish it here so
            // diffusion/hydro never starts from an interior-only Current slot.
#pragma omp parallel for schedule(dynamic, 1)
            for (size_t i = 0;
                 i < amr_ctrl.tree->GetActiveBlocks().size(); ++i) {
                amr::Block& block = amr_ctrl.pool->GetBlock(
                    amr_ctrl.tree->GetActiveBlocks()[i]);
                bc_handler.apply(block.fluid_state, block.grid);
            }
            amr_ctrl.ghost_exchange.ExecuteExchange(
                amr_ctrl.pool, amr_ctrl.tree, config.grid.dim,
                &amr::Block::fluid_state);
            StageExecutionContext staged_context{
                ExecutionSide::Host, *replacement, scheduler_clock};
            (void)arch::scheduler::complete_boundary(
                staged_context, proposed.handles_in_observation_order,
                StateSlot::Current, initial_witness.version,
                [](StateSlot, arch::state::StateVersion,
                   arch::state::CompletionToken token) { return token; });
            staged_initial_handles = proposed.handles_in_observation_order;
            staged_initial_ledger = std::move(replacement);
        });
    stage_handles = std::move(staged_initial_handles);
    residency_ledger = std::move(staged_initial_ledger);
    if (initial_topology.handles_in_observation_order != stage_handles)
        throw std::logic_error("initial topology commit result mismatch");

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

    double dt_burn_global = start_state.has_timestep_state
        ? start_state.dt_burn
        : ((config.io.restart && config.physics.burn.use_burn)
               ? config.GetCustomParam("dt_init", 1e-16)
               : 1e99);
    const auto write_checkpoint = [&](bool resume_after_regrid) {
        write_chk(amr_ctrl, ctrl.chk_file_index++, ctrl.plt_file_index,
                  ctrl.step_count, ctrl.t_current, ctrl.dt_old,
                  dt_burn_global, resume_after_regrid, config);
    };
    double cfl = config.numerics.cfl;
    bool reported_composite_diffusion = false;

    // Emit the initial state only for a fresh run. A step-zero restart already
    // represents that state and must retain the checkpoint's next-file indices.
    if (ctrl.should_write_initial_output())
    {
        synchronize_fluid_ghosts();
        write_plt(amr_ctrl, p_func, t_func, gamma1_func, &eos, ctrl.plt_file_index++, ctrl.t_current, config, specs);
        write_checkpoint(false);
    }
    ctrl.print_header(has_burn, has_diff);

    // Main Time Loop (Method of Lines)
    bool skip_regrid_once = start_state.resume_after_regrid;
    bool advanced_any_step = false;
    while (!ctrl.is_finished())
    {
        // Step A: IO Routine & AMR Regrid
        if (skip_regrid_once) {
            skip_regrid_once = false;
        } else if (ctrl.step_count % config.amr.regrid_interval == 0) {
            const auto pre_commit_topology = observe_topology();
            topology_registry.validate_committed_snapshot(
                pre_commit_topology);
            synchronize_fluid_ghosts();
            const bool mesh_changed = amr_ctrl.tree->Regrid(config);
            auto topology_candidate =
                topology_registry.stage_reconciliation(observe_topology());
            if (topology_candidate.reconciliation().topology_changed
                != mesh_changed) {
                throw std::logic_error(
                    "CPU regrid result disagrees with logical topology");
            }

            // Regrid creates (or restricts into) blocks whose ghost zones have
            // not participated in the pre-regrid exchange. RK stage 1 reads
            // those zones immediately, so synchronize the new hierarchy before
            // any reconstruction can use a reset halo value.
            if (mesh_changed) {
                std::unique_ptr<StateResidencyLedger> staged_ledger;
                std::vector<amr::BlockHandle> staged_handles;
                const auto reconciliation =
                    topology_registry.commit_after_success(
                        std::move(topology_candidate),
                        [&](const auto& proposed) {
                            auto replacement =
                                std::make_unique<StateResidencyLedger>(
                                    proposed.epoch);
                            const arch::scheduler::PublicationWitness
                                topology_witness =
                                    scheduler_clock.next_publication();
                            for (const amr::BlockHandle handle
                                 : proposed.handles_in_observation_order) {
                                replacement->register_block(
                                    handle, topology_witness.version,
                                    topology_witness.completion);
                            }

#pragma omp parallel for schedule(dynamic, 1)
                            for (size_t i = 0;
                                 i < amr_ctrl.tree->GetActiveBlocks().size();
                                 ++i) {
                                amr::Block& b = amr_ctrl.pool->GetBlock(
                                    amr_ctrl.tree->GetActiveBlocks()[i]);
                                bc_handler.apply(b.fluid_state, b.grid);
                            }
                            amr_ctrl.ghost_exchange.ExecuteExchange(
                                amr_ctrl.pool, amr_ctrl.tree,
                                config.grid.dim, &amr::Block::fluid_state);
                            StageExecutionContext staged_context{
                                ExecutionSide::Host, *replacement,
                                scheduler_clock};
                            (void)arch::scheduler::complete_boundary(
                                staged_context,
                                proposed.handles_in_observation_order,
                                StateSlot::Current,
                                topology_witness.version,
                                [](StateSlot, arch::state::StateVersion,
                                   arch::state::CompletionToken token) {
                                    return token;
                                });
                            staged_handles =
                                proposed.handles_in_observation_order;
                            staged_ledger = std::move(replacement);
                        });
                stage_handles = std::move(staged_handles);
                residency_ledger = std::move(staged_ledger);
                if (reconciliation.handles_in_observation_order
                    != stage_handles) {
                    throw std::logic_error(
                        "topology commit result and staged handles disagree");
                }
            } else {
                const auto reconciliation =
                    topology_registry.commit_after_success(
                        std::move(topology_candidate), [](const auto&) {});
                stage_handles = reconciliation.handles_in_observation_order;
            }
        }

        bool do_plt, do_chk;
        ctrl.check_io(do_plt, do_chk);

        if (do_plt) {
            synchronize_fluid_ghosts();
            write_plt(amr_ctrl, p_func, t_func, gamma1_func, &eos, ctrl.plt_file_index++, ctrl.t_current, config, specs);
        }
        if (do_chk) {
            write_checkpoint(true);
        }

        // Step B: Calculate Time Step (CFL Condition)
        const auto& active_blocks = amr_ctrl.tree->GetActiveBlocks();
        if (stage_handles.size() != active_blocks.size())
            throw std::logic_error(
                "active topology and scheduler handles disagree");
        std::vector<arch::reduction::ReductionCandidate> hydro_dt_candidates;
        hydro_dt_candidates.reserve(active_blocks.size());
        for (int block_id : active_blocks) {
            amr::Block& b = amr_ctrl.pool->GetBlock(block_id);
            double dt_b = adaptive_dt(b.fluid_state, eos, b.grid, cfl);
            hydro_dt_candidates.push_back({
                dt_b,
                DriverReduction::make_block_reduction_key(
                    b.level, b.morton_code, b.logical_x1, b.logical_x2,
                    b.logical_x3,
                    DriverReduction::BlockReductionComponent::Hydro),
                true});
        }
        double dt_hydro = DriverReduction::reduce_block_minimum(
            1e99, hydro_dt_candidates);

        // The diffusion operator reports a forward-Euler stability step.  STS
        // removes that O(dx^2) restriction from the macro step, except when
        // the configured RKL stage cap is genuinely exhausted.
        double dt_diff_fe = 1e99;
        double dt_diff_sts_limit = 1e99;
        if (has_diff) {
            std::vector<arch::reduction::ReductionCandidate>
                diffusion_dt_candidates;
            diffusion_dt_candidates.reserve(active_blocks.size());
            for (const int block_id : active_blocks) {
                const amr::Block& block = amr_ctrl.pool->GetBlock(block_id);
                const double block_dt = DiffFlux::adaptive_dt_diff(
                    block.fluid_state, eos, block.grid, config, 1.0);
                diffusion_dt_candidates.push_back({
                    block_dt,
                    DriverReduction::make_block_reduction_key(
                        block.level, block.morton_code, block.logical_x1,
                        block.logical_x2, block.logical_x3,
                        DriverReduction::BlockReductionComponent::Diffusion),
                    true});
            }
            dt_diff_fe = DriverReduction::reduce_block_minimum(
                1e99, diffusion_dt_candidates);
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

        // Step C: Symmetric Strang update
        // B(dt/2) D(dt/2) H(dt) D(dt/2) B(dt/2)
        StageExecutionContext stage_context{
            ExecutionSide::Host, *residency_ledger, scheduler_clock};
        ScopedStageBinding stage_binding(stage_context, stage_handles);

        const auto advance_diffusion = [&](double diffusion_dt) {
            if (!has_diff || diffusion_dt <= 0.0) return;

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
                              << " STS with stage ghost synchronization and reflux."
                              << std::endl;
                    reported_composite_diffusion = true;
                }
                if (rkl1) {
                    Numerics::Diffusion::advance_amr_rkl1(
                        amr_ctrl, diffusion_dt, dt_diff_fe, bc_handler, eos, config);
                } else {
                    Numerics::Diffusion::advance_amr_rkl2(
                        amr_ctrl, diffusion_dt, dt_diff_fe, bc_handler, eos, config);
                }
            } else {
                Numerics::Diffusion::dispatch_diffusion(config, [&](auto& integrator) {
                    amr::Block& block = amr_ctrl.pool->GetBlock(active_blocks.front());
                    integrator.integrate(block, eos, block.grid, config,
                                         diffusion_dt, dt_diff_fe, bc_handler);
                });
            }
        };

        // C1. Burn Step (1/2 dt)
        if (has_burn) {
            (void)arch::scheduler::execute_burn_first_lane(
                stage_context, stage_handles,
                [&](arch::state::CompletionToken token) {
            std::vector<double> dt_burn_by_block(active_blocks.size(), 1e99);
            #pragma omp parallel for schedule(dynamic, 1)
            for (size_t i = 0; i < active_blocks.size(); ++i) {
                amr::Block& b = amr_ctrl.pool->GetBlock(active_blocks[i]);
                bc_handler.apply(b.fluid_state, b.grid);
                execute_burn_step(b.fluid_state, 0.5 * dt, eos, burn, b.grid, config, dt_burn_by_block[i]);
            }
            std::vector<arch::reduction::ReductionCandidate>
                burn_dt_candidates;
            burn_dt_candidates.reserve(active_blocks.size() + 1);
            burn_dt_candidates.push_back({
                dt_burn_global,
                DriverReduction::make_accumulator_reduction_key(
                    DriverReduction::BlockReductionComponent::BurnFirstHalf),
                true});
            for (size_t i = 0; i < dt_burn_by_block.size(); ++i) {
                const amr::Block& block = amr_ctrl.pool->GetBlock(
                    active_blocks[i]);
                burn_dt_candidates.push_back({
                    dt_burn_by_block[i],
                    DriverReduction::make_block_reduction_key(
                        block.level, block.morton_code, block.logical_x1,
                        block.logical_x2, block.logical_x3,
                        DriverReduction::BlockReductionComponent::BurnFirstHalf),
                    true});
            }
            dt_burn_global = DriverReduction::reduce_block_minimum(
                1e99, burn_dt_candidates);
                    return token;
                });
        }

        // C2. Diffusion Step (1/2 dt)
        advance_diffusion(0.5 * dt);

        // C3. Hydrodynamics Step (dt)
        synchronize_fluid_ghosts();

        integrator_solve(amr_ctrl, dt, bc_handler, gravity, hydro, num_cfg);

        // C4. Diffusion Step (1/2 dt)
        advance_diffusion(0.5 * dt);

        // C5. Burn Step (1/2 dt)
        if (has_burn) {
            (void)arch::scheduler::execute_burn_second_lane(
                stage_context, stage_handles,
                [&](arch::state::CompletionToken token) {
            std::vector<double> dt_burn_by_block(active_blocks.size(), 1e99);
            #pragma omp parallel for schedule(dynamic, 1)
            for (size_t i = 0; i < active_blocks.size(); ++i) {
                amr::Block& b = amr_ctrl.pool->GetBlock(active_blocks[i]);
                bc_handler.apply(b.fluid_state, b.grid);
                execute_burn_step(b.fluid_state, 0.5 * dt, eos, burn, b.grid, config, dt_burn_by_block[i]);
            }
            std::vector<arch::reduction::ReductionCandidate>
                burn_dt_candidates;
            burn_dt_candidates.reserve(active_blocks.size() + 1);
            burn_dt_candidates.push_back({
                dt_burn_global,
                DriverReduction::make_accumulator_reduction_key(
                    DriverReduction::BlockReductionComponent::BurnSecondHalf),
                true});
            for (size_t i = 0; i < dt_burn_by_block.size(); ++i) {
                const amr::Block& block = amr_ctrl.pool->GetBlock(
                    active_blocks[i]);
                burn_dt_candidates.push_back({
                    dt_burn_by_block[i],
                    DriverReduction::make_block_reduction_key(
                        block.level, block.morton_code, block.logical_x1,
                        block.logical_x2, block.logical_x3,
                        DriverReduction::BlockReductionComponent::BurnSecondHalf),
                    true});
            }
            dt_burn_global = DriverReduction::reduce_block_minimum(
                1e99, burn_dt_candidates);
                    return token;
                });
        }

        // Step D: Advance Counters
        ctrl.advance(dt);
        advanced_any_step = true;
        ctrl.print_step(dt, dt_hydro, has_burn ? dt / 2.0 : 0.0, dt_diff_fe, has_burn, has_diff);
    }

    // Final Output (Force output at t_max)
    if (advanced_any_step && ctrl.reached_target_time())
    {
        std::cout << ">>> Target Time Reached. Forcing final output..." << std::endl;
        synchronize_fluid_ghosts();
        write_plt(amr_ctrl, p_func, t_func, gamma1_func, &eos, ctrl.plt_file_index++, ctrl.t_current, config, specs);
        write_checkpoint(false);
    }

    std::cout << ">>> Simulation Done. Total Steps: " << ctrl.step_count
              << " | Final Time: " << ctrl.t_current << std::endl;
}
