/**
 * @file Driver.h
 * @brief Shared simulation orchestration for CPU and CUDA execution.
 *
 * The driver owns time/output control, state-residency tracking and AMR
 * transaction coordination. It orders split burn, diffusion and hydro stages
 * through the selected interfaces; topology decisions and numerical policies
 * remain shared while backend owners manage execution and data visibility.
 * Workflow:
 * 1. Read the resolved run state and active AMR topology.
 * 2. Order burn, diffusion, hydro and gravity through the shared stage scheduler.
 * 3. Publish completed state, output and checkpoint evidence at accepted step boundaries.
 */

#pragma once

#include "driver/schedule/DriverControl.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/stages/DriverStages.h"
#include "driver/stages/GravityStage.h"
#include "driver/io/DriverIO.h"
#include "amr/refinement/RefinementThermodynamics.h"
#include "numerics/state/StateAdmissibility.h"
#include <iostream>
#include <limits>
#ifdef _OPENMP
#include <omp.h>
#endif

/**
 * @brief Executes the main simulation loop.
 * @tparam EosPolicy The equation of state (e.g., Ideal Gas).
 * Hydro, gravity and burn operations enter through the supplied interfaces;
 * resolved execution inputs determine the concrete backend and policy routes.
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
                    const RunState &start_state,
                    const io::CheckpointProvenance &checkpoint_provenance,
                    const arch::dispatch::ResolvedExecutionPlan* resolved_plan = nullptr,
                    const arch::dispatch::ExecutionRequirements* execution_requirements = nullptr,
                    const arch::dispatch::BackendResolution* backend_resolution = nullptr,
                    arch::dispatch::StartupOrder* startup_order = nullptr)
{
    if (resolved_plan == nullptr || execution_requirements == nullptr
        || backend_resolution == nullptr || startup_order == nullptr) {
        throw std::invalid_argument(
            "resolved execution inputs are required by the Driver");
    }
#if !ARCH_CUDA_BUILD_ENABLED
    if (backend_resolution->resolved_backend == arch::dispatch::ComputeBackend::Cuda)
        throw std::logic_error("CUDA backend selected by a CPU-only build");
#endif
    arch::config::ValidateControls(config, specs.count());
    using namespace arch::driver;
    using arch::scheduler::ScopedStageBinding;
    SimulationController ctrl(config, start_state);
    BCHandler bc_handler{config};
    if (!config.io.restart)
        for (int id : amr_ctrl.tree->GetActiveBlocks()) ctrl.repairs.combine(amr_ctrl.pool->GetBlock(id).fluid_state.stage_repairs);
    DriverRuntime runtime(amr_ctrl, bc_handler, config, specs, ctrl);
    amr::BindRefinementThermodynamics(*amr_ctrl.tree, eos);
    const auto p_func = [](const FluidVector& U, const double* Xi, const void* context) -> double {
        return static_cast<const EosPolicy*>(context)->get_pressure(U, Xi);
    };
    const auto t_func = [](const FluidVector& U, const double* Xi, const void* context) -> double {
        return static_cast<const EosPolicy*>(context)->get_temperature(
            U.rho, arch::state::recover(U).internal, Xi);
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
    DriverIO output(runtime, ctrl, checkpoint_provenance, p_func, t_func, gamma1_func, &eos);
    CpuStageTimings cpu_stages;
    const bool time_cpu_stages = backend_resolution->resolved_backend
        == arch::dispatch::ComputeBackend::Cpu;
    const int deferred_initial_passes = amr_ctrl.tree->ConsumeDeferredInitialRefinement();
    runtime.initialize_topology();
    // Complete deferred thermodynamic regrids through the same transaction
    // coordinator used by production regrids before the first output/step.
    if (deferred_initial_passes > 0) {
        std::cout << "[Dispatch] Performing initial AMR refinement loop..."
                  << std::endl;
        for (int pass = 0; pass < deferred_initial_passes; ++pass) {
            CpuStageTimer timed(cpu_stages, CpuStage::Regrid, time_cpu_stages);
            if (!runtime.perform_regrid(ctrl.step_count, ctrl.t_current)) break;
            std::cout << "           -> Refining initial condition (Pass "
                      << pass + 1 << ")..." << std::endl;
        }
        runtime.ensure_fluid_ghosts();
    }

    std::cout << ">>> Simulation Started | Solver: " << integrator_name
              << " | Entropy Fix Coeff: " << config.numerics.entropy_fix_coeff;
#ifdef _OPENMP
    std::cout << " | OpenMP: ON (max threads=" << omp_get_max_threads() << ")";
#else
    std::cout << " | OpenMP: OFF";
#endif
    std::cout << std::endl;

    bool has_burn = config.physics.burn.use_burn;
    bool has_diff = config.physics.diffusion.use_diffusion;

    start_compute_backend(runtime, eos, *resolved_plan, *backend_resolution, *startup_order);
    GravityStage gravity_stage(runtime, gravity);
    {
        CpuStageTimer timed(cpu_stages, CpuStage::Gravity, time_cpu_stages);
        gravity_stage.prepare_current(ctrl.t_current, true);
    }
    double dt_burn_global = start_state.has_timestep_state
        ? start_state.dt_burn
        : ((config.io.restart && config.physics.burn.use_burn)
               ? config.numerics.dt_init
               : 1e99);
    if (ctrl.should_write_initial_output()) {
        output.write_plot(gravity_stage.plot_fields());
        output.write_checkpoint(dt_burn_global, false);
    }
    ctrl.print_header(has_burn, has_diff);
    DriverStageWorkspace workspace;
    bool skip_regrid_once = start_state.resume_after_regrid;
    const bool dynamic_amr_enabled =
        config.amr.lrefinemax > config.amr.lrefinemin;
    bool advanced_any_step = false;
    while (!ctrl.is_finished()) {
        if (skip_regrid_once) skip_regrid_once = false;
        else if (dynamic_amr_enabled
                 && ctrl.step_count % config.amr.regrid_interval == 0) {
            CpuStageTimer timed(cpu_stages, CpuStage::Regrid, time_cpu_stages);
            (void)runtime.perform_regrid(ctrl.step_count, ctrl.t_current);
        }

        bool do_plt, do_chk;
        ctrl.check_io(do_plt, do_chk);
        {
            CpuStageTimer timed(cpu_stages, CpuStage::Gravity, time_cpu_stages);
            // First advance and each checkpoint boundary must be independent
            // of solver history absent from the checkpoint. Between durable
            // boundaries, reuse only accepted potentials as initial guesses.
            gravity_stage.prepare_current(ctrl.t_current,
                                          !advanced_any_step || do_chk);
        }
        if (do_plt) output.write_plot(gravity_stage.plot_fields());
        if (do_chk) output.write_checkpoint(dt_burn_global, true);

        const auto candidates = [&] {
            CpuStageTimer timed(cpu_stages, CpuStage::Timestep, time_cpu_stages);
            return calculate_timestep_candidates(runtime, workspace, eos, resolved_plan);
        }();
        const double dt_computed = ctrl.calculate_next_dt(
            std::min({candidates.hydro, candidates.diffusion_sts, gravity_stage.timestep()}), dt_burn_global);
        dt_burn_global = 1e99;
        const double dt = ctrl.sync_dt(dt_computed);
        auto stage_context = runtime.stage_context();
        stage_context.hydro_preparation = gravity_stage.active() ? &gravity_stage : nullptr;
        stage_context.step_start_time = ctrl.t_current;
        stage_context.step_dt = dt;
        ScopedStageBinding stage_binding(stage_context, runtime.handles());

        // Symmetric split: Burn(dt/2), Diffusion(dt/2), Hydro(dt), Diffusion(dt/2), Burn(dt/2).
        if (has_burn) {
            CpuStageTimer timed(cpu_stages, CpuStage::BurnFirst, time_cpu_stages);
            (void)arch::scheduler::execute_burn_first_lane(stage_context, runtime.handles(),
                [&](arch::state::CompletionToken token) {
                    return execute_burn_half(runtime, workspace, eos, burn, BurnHalf::First,
                                             0.5 * dt, dt_burn_global, token);
                });
        }
        {
            CpuStageTimer timed(cpu_stages, CpuStage::Diffusion, time_cpu_stages);
            advance_diffusion(runtime, workspace, stage_context, eos, resolved_plan,
                              ctrl.step_count, 0.5 * dt, candidates.diffusion_forward_euler);
        }
        {
            CpuStageTimer timed(cpu_stages, CpuStage::Hydro, time_cpu_stages);
            advance_hydro(runtime, workspace, stage_context, resolved_plan, dt,
                          integrator_solve, gravity, hydro);
        }
        {
            CpuStageTimer timed(cpu_stages, CpuStage::Diffusion, time_cpu_stages);
            advance_diffusion(runtime, workspace, stage_context, eos, resolved_plan,
                              ctrl.step_count, 0.5 * dt, candidates.diffusion_forward_euler);
        }
        if (has_burn) {
            CpuStageTimer timed(cpu_stages, CpuStage::BurnSecond, time_cpu_stages);
            (void)arch::scheduler::execute_burn_second_lane(stage_context, runtime.handles(),
                [&](arch::state::CompletionToken token) {
                    return execute_burn_half(runtime, workspace, eos, burn, BurnHalf::Second,
                                             0.5 * dt, dt_burn_global, token);
                });
        }
        gravity_stage.invalidate();
        ctrl.advance(dt);
        advanced_any_step = true;
        ctrl.print_step(dt, candidates.hydro, has_burn ? dt / 2.0 : 0.0,
                        candidates.diffusion_forward_euler, has_burn, has_diff);
    }
    if (advanced_any_step && (ctrl.reached_target_time() || ctrl.reached_step_limit())) {
        {
            CpuStageTimer timed(cpu_stages, CpuStage::Gravity, time_cpu_stages);
            gravity_stage.prepare_current(ctrl.t_current, false);
        }
        std::cout << ">>> Terminal time/step limit reached. Forcing final output..." << std::endl;
        output.write_plot(gravity_stage.plot_fields());
        output.write_checkpoint(dt_burn_global, false);
    }
    output.write_measurements(workspace.cuda_diffusion_schedule, cpu_stages);
    std::cout << ">>> Simulation Done. Total Steps: " << ctrl.step_count
              << " | Final Time: " << ctrl.t_current << std::endl;
}
