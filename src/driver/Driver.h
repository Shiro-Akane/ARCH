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
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

// Driver-local orchestration.
#include "DriverBurn.h"
#include "DriverControl.h"
#include "DriverUtils.h"
#include "ComputeBackend.h"
#include "StageScheduler.h"
#include "TopologyIdentityRegistry.h"
#include "dispatch/BackendCapabilities.h"
#include "dispatch/ResolvedExecutionPlan.h"

// AMR and I/O services.
#include "../amr/AMRControl.h"
#include "../amr/TopologyTransaction.h"
#include "../io/IO.h"

// Numerical and physical policy interfaces.
#include "../numerics/burnsolver/BurnerHandle.h"
#include "../numerics/burnsolver/Networks.h"
#include "../numerics/diffusion/DiffDispatch.h"
#include "../numerics/diffusion/DiffFunction.h"
#include "../numerics/integrator/IHydroSolver.h"
#include "../physics/gravity/IGravityPolicy.h"

#if ARCH_CUDA_BUILD_ENABLED
#include "../cuda/common/CudaLaunchConfig.h"
#include "../cuda/runtime/CudaBackend.h"
#endif

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
                    const RunState &start_state,
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
    // Initialize output scheduling, time-step control, and run counters.
    SimulationController ctrl(config, start_state);

    const NumericsConfig &num_cfg = config.numerics;
    BCHandler bc_handler{config};

    const auto prepare_regrid = [&](int step, double time) {
        (void)step;
        (void)time;
        return amr_ctrl.tree->PrepareRegrid(config);
    };

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
    std::uint64_t next_amr_transaction_id = 1;
    std::unique_ptr<StateResidencyLedger> residency_ledger;
    std::vector<amr::BlockHandle> stage_handles;
    std::unique_ptr<arch::backend::ComputeBackend> compute_backend;
    arch::backend::StorageGenerationIssuer storage_generation_issuer;
    std::vector<arch::backend::StorageGeneration> backend_storage;
    const bool use_cuda = backend_resolution->resolved_backend
        == arch::dispatch::ComputeBackend::Cuda;

#if !ARCH_CUDA_BUILD_ENABLED
    if (use_cuda)
        throw std::logic_error("CUDA backend selected by a CPU-only build");
#endif

    const auto observe_blocks = [&](std::span<const int> active) {
        std::vector<TopologyObservation> observations;
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
    const auto observe_topology = [&] {
        const auto& active = amr_ctrl.tree->GetActiveBlocks();
        return observe_blocks(active);
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
    const auto host_transfer_view = [](FluidState& state) {
        const std::size_t cells = state.rho.size();
        const std::size_t species = static_cast<std::size_t>(
            state.GetNumSpecies());
        return arch::backend::HostStateTransferView{
            state.rho.data(), state.mom_u.data(), state.mom_v.data(),
            state.mom_w.data(), state.eng.data(), state.enuc_rate.data(),
            species == 0 ? nullptr : state.mass_fractions.data(), cells,
            species, species == 0 ? 0 : cells};
    };
    const auto backend_access = [&](std::size_t block_index, StateSlot slot) {
        if (!compute_backend)
            throw std::logic_error("CUDA backend is not constructed");
        if (block_index >= stage_handles.size()
            || block_index >= backend_storage.size())
            throw std::out_of_range("CUDA backend block index is invalid");
        return arch::backend::BackendStateAccess{
            stage_handles[block_index], backend_storage[block_index], slot};
    };
    const auto trace_backend_operation = [&] (
        arch::backend::BackendOperation operation, StateSlot slot,
        const arch::backend::BackendCounters& before) {
        const auto after = compute_backend->counters();
        for (std::size_t index = 0; index < stage_handles.size(); ++index) {
            compute_backend->append_trace({
                static_cast<std::uint64_t>(ctrl.step_count), operation,
                stage_handles[index], backend_storage[index], slot,
                residency_ledger->inspect({stage_handles[index], slot}),
                index == 0 ? after.bytes_h2d - before.bytes_h2d : 0,
                index == 0 ? after.bytes_d2h - before.bytes_d2h : 0,
                index == 0 ? after.kernel_count - before.kernel_count : 0,
                index == 0
                    ? after.stream_sync_count - before.stream_sync_count : 0});
        }
    };
    const auto execute_device_boundary = [&] (
        StateSlot requested, arch::state::StateVersion version,
        arch::state::CompletionToken token) {
        std::vector<arch::backend::BackendStateAccess> accesses;
        accesses.reserve(stage_handles.size());
        for (std::size_t index = 0; index < stage_handles.size(); ++index) {
            const auto access = backend_access(index, requested);
            (void)compute_backend->execute_physical_boundary(
                access, version, token);
            accesses.push_back(access);
        }
        const auto plan = amr_ctrl.ghost_exchange.BuildSameLevelPlan(
            amr_ctrl.pool, amr_ctrl.tree, config.grid.dim, stage_handles);
        return compute_backend->execute_same_level_exchange(
            accesses, plan, requested, version, token);
    };
    const auto complete_device_boundary = [&](StateSlot slot) {
        if (stage_handles.empty())
            throw std::logic_error("CUDA boundary requires active blocks");
        const auto version = residency_ledger->inspect(
            {stage_handles.front(), slot}).interior.version;
        for (const auto handle : stage_handles) {
            residency_ledger->require_readable(
                {handle, slot},
                {ExecutionSide::Device, version, true, false});
        }
        const auto before = compute_backend->counters();
        StageExecutionContext context{
            ExecutionSide::Device, *residency_ledger, scheduler_clock};
        (void)arch::scheduler::complete_boundary(
            context, stage_handles, slot, version,
            [&](StateSlot requested, arch::state::StateVersion version,
                arch::state::CompletionToken token) {
                return execute_device_boundary(requested, version, token);
            });
        trace_backend_operation(
            arch::backend::BackendOperation::PhysicalBoundary, slot, before);
    };
    // Keep one synchronization boundary for regrid, hydro, checkpoint, and
    // derivative output. CPU executes physical faces plus neighbor exchange;
    // CUDA completes the device boundary and materializes accepted Current
    // only when a host consumer actually needs it.
    const auto synchronize_fluid_ghosts = [&] {
        if (compute_backend) {
            bool needs_device_ghosts = false;
            for (const auto handle : stage_handles) {
                const auto coherence = residency_ledger->inspect(
                    {handle, StateSlot::Current});
                needs_device_ghosts = needs_device_ghosts
                    || !arch::state::side_can_read(
                        coherence.ghost.residency, ExecutionSide::Device)
                    || coherence.ghost.version != coherence.interior.version
                    || coherence.ghost_source_version
                        != coherence.interior.version;
            }
            if (needs_device_ghosts)
                complete_device_boundary(StateSlot::Current);
            const auto& active = amr_ctrl.tree->GetActiveBlocks();
            for (std::size_t index = 0; index < stage_handles.size(); ++index) {
                const auto coherence = residency_ledger->inspect(
                    {stage_handles[index], StateSlot::Current});
                const bool host_current = arch::state::side_can_read(
                        coherence.interior.residency, ExecutionSide::Host)
                    && arch::state::side_can_read(
                        coherence.ghost.residency, ExecutionSide::Host)
                    && coherence.ghost.version == coherence.interior.version
                    && coherence.ghost_source_version
                        == coherence.interior.version;
                if (!host_current) {
                    amr::Block& block = amr_ctrl.pool->GetBlock(active[index]);
                    (void)arch::backend::transfer_state_regions(
                        *compute_backend, *residency_ledger, scheduler_clock,
                        backend_access(index, StateSlot::Current),
                        host_transfer_view(block.fluid_state),
                        arch::state::PendingTransferPhase::PendingD2H,
                        static_cast<std::uint64_t>(ctrl.step_count),
                        arch::backend::BackendOperation::Materialize);
                }
            }
            return;
        }
#pragma omp parallel for schedule(dynamic, 1)
        for (size_t i = 0; i < amr_ctrl.tree->GetActiveBlocks().size(); ++i) {
            amr::Block& block = amr_ctrl.pool->GetBlock(amr_ctrl.tree->GetActiveBlocks()[i]);
            bc_handler.apply(block.fluid_state, block.grid);
        }
        amr_ctrl.ghost_exchange.ExecuteExchange(amr_ctrl.pool, amr_ctrl.tree,
                                                config.grid.dim,
                                                &amr::Block::fluid_state,
                                                stage_handles);
        if (residency_ledger && !stage_handles.empty())
            publish_current_ghost();
    };
    const int deferred_initial_passes =
        amr_ctrl.tree->ConsumeDeferredInitialRefinement();

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
    amr_ctrl.BindActiveHandles(stage_handles);

    const auto perform_host_regrid = [&](int step, double time) {
        topology_registry.validate_committed_snapshot(observe_topology());

        // Freeze one complete, readable Host source topology before staging.
        // This is also the rollback authority for the old hierarchy.
        synchronize_fluid_ghosts();
        const std::vector<int> old_active(
            amr_ctrl.tree->GetActiveBlocks().begin(),
            amr_ctrl.tree->GetActiveBlocks().end());

        auto prepared = prepare_regrid(step, time);
        auto topology_candidate = topology_registry.stage_reconciliation(
            observe_blocks(prepared.proposed_active_blocks()));
        const auto& proposed = topology_candidate.reconciliation();
        if (proposed.topology_changed != prepared.topology_changed())
            throw std::logic_error(
                "staged AMR topology disagrees with identity reconciliation");

        if (!prepared.topology_changed()) {
            const auto reconciliation = topology_registry.commit_after_success(
                std::move(topology_candidate),
                [&](const auto&) { prepared.PublishNoChangeNoexcept(); });
            stage_handles = reconciliation.handles_in_observation_order;
            amr_ctrl.BindActiveHandles(stage_handles);
            return false;
        }

        if (stage_handles.empty())
            throw std::logic_error("AMR regrid has no source handles");
        if (next_amr_transaction_id
            == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("AMR transaction ID exhausted");
        const amr::AmrPlanScope scope{
            next_amr_transaction_id++, stage_handles.front().epoch,
            proposed.epoch};
        prepared.BuildMigrationPlans(
            stage_handles, proposed.handles_in_observation_order, scope);

        amr::TopologyTransaction transaction(
            scope.transaction_id, scope.from_epoch, scope.to_epoch);
        transaction.begin_migration();
        transaction.require_scope(prepared.prolongation_plan());
        transaction.require_scope(prepared.restriction_plan());
        prepared.ExecuteMigration();
        transaction.mark_ready();

        struct HostStateBackup {
            int pool_index = -1;
            FluidState state;
        };
        struct RegridPublication {
            std::unique_ptr<StateResidencyLedger> ledger;
            std::vector<amr::BlockHandle> handles;
            arch::scheduler::PublicationWitness topology_witness{};
            std::vector<HostStateBackup> source_backups;
        };
        static_assert(std::is_nothrow_swappable_v<FluidState>);

        std::unique_ptr<StateResidencyLedger> staged_ledger;
        std::vector<amr::BlockHandle> staged_handles;
        try {
            (void)topology_registry.commit_after_success(
                    std::move(topology_candidate),
                    [&](const auto& committed_topology) {
                        transaction.commit_after_success(
                            [&](const amr::AmrPlanScope& transaction_scope) {
                                if (transaction_scope != scope
                                    || committed_topology.epoch
                                        != transaction_scope.to_epoch)
                                    throw std::logic_error(
                                        "AMR publication scope drifted");
                                RegridPublication payload;
                                payload.ledger =
                                    std::make_unique<StateResidencyLedger>(
                                        committed_topology.epoch);
                                payload.handles = committed_topology
                                    .handles_in_observation_order;
                                payload.topology_witness =
                                    scheduler_clock.next_publication();
                                for (const amr::BlockHandle handle
                                     : payload.handles) {
                                    payload.ledger->register_block(
                                        handle,
                                        payload.topology_witness.version,
                                        payload.topology_witness.completion);
                                }
                                payload.source_backups.reserve(
                                    old_active.size());
                                for (const int pool_index : old_active) {
                                    payload.source_backups.push_back({
                                        pool_index,
                                        amr_ctrl.pool->GetBlock(pool_index)
                                            .fluid_state});
                                }
                                return payload;
                            },
                            [&](const amr::AmrPlanScope& transaction_scope,
                                RegridPublication& payload) {
                                const auto restore_source_states = [&]() noexcept {
                                    for (auto& backup
                                         : payload.source_backups) {
                                        using std::swap;
                                        swap(amr_ctrl.pool
                                                 ->GetBlock(backup.pool_index)
                                                 .fluid_state,
                                             backup.state);
                                    }
                                };
                                try {
                                    if (transaction_scope != scope)
                                        throw std::logic_error(
                                            "AMR finalizer scope drifted");
                                    prepared.ActivateForFinalization();
#pragma omp parallel for schedule(dynamic, 1)
                                    for (std::size_t index = 0;
                                         index < amr_ctrl.tree
                                                     ->GetActiveBlocks()
                                                     .size();
                                         ++index) {
                                        amr::Block& block =
                                            amr_ctrl.pool->GetBlock(
                                                amr_ctrl.tree
                                                    ->GetActiveBlocks()[index]);
                                        bc_handler.apply(
                                            block.fluid_state, block.grid);
                                    }
                                    amr_ctrl.ghost_exchange.ExecuteExchange(
                                        amr_ctrl.pool, amr_ctrl.tree,
                                        config.grid.dim,
                                        &amr::Block::fluid_state,
                                        payload.handles);
                                    StageExecutionContext staged_context{
                                        ExecutionSide::Host, *payload.ledger,
                                        scheduler_clock};
                                    (void)arch::scheduler::complete_boundary(
                                        staged_context, payload.handles,
                                        StateSlot::Current,
                                        payload.topology_witness.version,
                                        [](StateSlot,
                                           arch::state::StateVersion,
                                           arch::state::CompletionToken token) {
                                            return token;
                                        });
                                    for (const amr::BlockHandle handle
                                         : payload.handles) {
                                        payload.ledger->require_readable(
                                            {handle, StateSlot::Current},
                                            {ExecutionSide::Host,
                                             payload.topology_witness.version,
                                             true, true});
                                    }
                                } catch (...) {
                                    restore_source_states();
                                    prepared.AbortNoexcept();
                                    throw;
                                }
                            },
                            [&](RegridPublication&& payload) noexcept {
                                prepared.PublishNoexcept();
                                staged_ledger = std::move(payload.ledger);
                                staged_handles = std::move(payload.handles);
                            });
                    });
        } catch (...) {
            if (transaction.state()
                != amr::TopologyTransactionState::Committed) {
                transaction.abort(
                    [&]() noexcept { prepared.AbortNoexcept(); });
            }
            throw;
        }

        stage_handles = std::move(staged_handles);
        residency_ledger = std::move(staged_ledger);
        amr_ctrl.BindActiveHandles(stage_handles);
        prepared.ReleaseRetired();
        return true;
    };

    // Complete deferred thermodynamic regrids through the same transaction
    // coordinator used by production regrids before the first output/step.
    if (deferred_initial_passes > 0) {
        std::cout << "[Dispatch] Performing initial AMR refinement loop..."
                  << std::endl;
        for (int pass = 0; pass < deferred_initial_passes; ++pass) {
            if (!perform_host_regrid(ctrl.step_count, ctrl.t_current)) break;
            std::cout << "           -> Refining initial condition (Pass "
                      << pass + 1 << ")..." << std::endl;
        }
        synchronize_fluid_ghosts();
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

    if (use_cuda) {
#if ARCH_CUDA_BUILD_ENABLED
        const auto& active = amr_ctrl.tree->GetActiveBlocks();
        if (active.empty() || stage_handles.size() != active.size()
            || execution_requirements->amr) {
            throw std::logic_error(
                "CUDA F requires a static uniform active topology");
        }
        bool needs_host_ghosts = false;
        for (const auto handle : stage_handles) {
            const auto coherence = residency_ledger->inspect(
                {handle, StateSlot::Current});
            needs_host_ghosts = needs_host_ghosts
                || !arch::state::side_can_read(
                    coherence.ghost.residency, ExecutionSide::Host)
                || coherence.ghost.version != coherence.interior.version
                || coherence.ghost_source_version != coherence.interior.version;
        }
        if (needs_host_ghosts)
            synchronize_fluid_ghosts();

        std::vector<arch::cuda::CudaBlockBinding> bindings;
        bindings.reserve(active.size());
        backend_storage.clear();
        backend_storage.reserve(active.size());
        for (std::size_t index = 0; index < active.size(); ++index) {
            const auto storage = storage_generation_issuer.issue();
            backend_storage.push_back(storage);
            bindings.push_back({
                &amr_ctrl.pool->GetBlock(active[index]), stage_handles[index],
                storage, &bc_handler.logical_plan()});
        }
        compute_backend = arch::cuda::make_cuda_backend(
            bindings,
            backend_resolution->device.ordinal,
            arch::cuda::make_cuda_launch_config(*resolved_plan, config),
            specs, eos);
        startup_order->record(arch::dispatch::StartupEvent::Constructed);
        startup_order->record(arch::dispatch::StartupEvent::Allocated);
        for (std::size_t index = 0; index < active.size(); ++index) {
            amr::Block& block = amr_ctrl.pool->GetBlock(active[index]);
            (void)arch::backend::transfer_state_regions(
                *compute_backend, *residency_ledger, scheduler_clock,
                backend_access(index, StateSlot::Current),
                host_transfer_view(block.fluid_state),
                arch::state::PendingTransferPhase::PendingH2D, 0,
                arch::backend::BackendOperation::InitialUpload);
        }
        startup_order->record(arch::dispatch::StartupEvent::Running);
#else
        throw std::logic_error("CUDA backend is unavailable");
#endif
    } else {
        startup_order->record(arch::dispatch::StartupEvent::Constructed);
        startup_order->record(arch::dispatch::StartupEvent::Allocated);
        startup_order->record(arch::dispatch::StartupEvent::Running);
    }

    // Main Time Loop (Method of Lines)
    bool skip_regrid_once = start_state.resume_after_regrid;
    bool advanced_any_step = false;
    while (!ctrl.is_finished())
    {
        // Step A: IO Routine & AMR Regrid
        if (skip_regrid_once) {
            skip_regrid_once = false;
        } else if (!compute_backend
                   && ctrl.step_count % config.amr.regrid_interval == 0) {
            (void)perform_host_regrid(ctrl.step_count, ctrl.t_current);
        }

        bool do_plt, do_chk;
        ctrl.check_io(do_plt, do_chk);

        if (do_plt || (compute_backend && do_chk)) {
            synchronize_fluid_ghosts();
        }
        if (do_plt) {
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
        if (compute_backend) {
            for (std::size_t index = 0; index < active_blocks.size(); ++index) {
                const amr::Block& b = amr_ctrl.pool->GetBlock(
                    active_blocks[index]);
                hydro_dt_candidates.push_back({
                    compute_backend->compute_hydro_dt(
                        backend_access(index, StateSlot::Current), cfl),
                    DriverReduction::make_block_reduction_key(
                        b.level, b.morton_code, b.logical_x1, b.logical_x2,
                        b.logical_x3,
                        DriverReduction::BlockReductionComponent::Hydro),
                    true});
            }
        } else {
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
            for (std::size_t index = 0; index < active_blocks.size(); ++index) {
                const int block_id = active_blocks[index];
                const amr::Block& block = amr_ctrl.pool->GetBlock(block_id);
                const double block_dt = compute_backend
                    ? compute_backend->compute_diffusion_dt(
                        backend_access(index, StateSlot::Current))
                    : DiffFlux::adaptive_dt_diff(
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
            const bool rkl1 = resolved_plan->diffusion_integrator
                == arch::dispatch::DiffusionIntegratorId::Rkl1;
            const bool rkl2 = resolved_plan->diffusion_integrator
                == arch::dispatch::DiffusionIntegratorId::Rkl2;
            if (!rkl1 && !rkl2) {
                throw std::logic_error(
                    "enabled diffusion received an invalid resolved route");
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
            compute_backend ? ExecutionSide::Device : ExecutionSide::Host,
            *residency_ledger, scheduler_clock};
        ScopedStageBinding stage_binding(stage_context, stage_handles);

        const auto advance_diffusion = [&](double diffusion_dt) {
            if (!has_diff || diffusion_dt <= 0.0) return;

            if (compute_backend) {
                complete_device_boundary(StateSlot::Current);
                const auto copy_one = [&](StateSlot destination) {
                    const auto before = compute_backend->counters();
                    (void)arch::scheduler::copy_slot(
                        stage_context, stage_handles, StateSlot::Current,
                        destination, [&] {
                            for (std::size_t index = 0;
                                 index < stage_handles.size(); ++index) {
                                compute_backend->copy_state_slot(
                                    backend_access(index, StateSlot::Current),
                                    backend_access(index, destination));
                            }
                        });
                    trace_backend_operation(
                        arch::backend::BackendOperation::DiffusionCopy,
                        destination, before);
                };
                copy_one(StateSlot::Scratch);
                copy_one(StateSlot::Next);

                const bool rkl1 = resolved_plan->diffusion_integrator
                    == arch::dispatch::DiffusionIntegratorId::Rkl1;
                const bool rkl2 = resolved_plan->diffusion_integrator
                    == arch::dispatch::DiffusionIntegratorId::Rkl2;
                if (!rkl1 && !rkl2)
                    throw std::logic_error(
                        "CUDA diffusion received an invalid resolved route");
                const DiffFunction::RKLOrder order = rkl1
                    ? DiffFunction::RKLOrder::First
                    : DiffFunction::RKLOrder::Second;
                const int stages = DiffFunction::compute_stages(
                    order, diffusion_dt, dt_diff_fe,
                    config.physics.diffusion.diff_cfl,
                    config.physics.diffusion.max_stages);
                const auto before = compute_backend->counters();
                const auto executor = [&] (
                    const arch::scheduler::RklPlan& plan,
                    const arch::scheduler::RklStageDescriptor& descriptor,
                    arch::state::CompletionToken token) {
                    for (std::size_t index = 0;
                         index < stage_handles.size(); ++index) {
                        (void)compute_backend->execute_diffusion_stage(
                            backend_access(index, StateSlot::Current), plan,
                            descriptor, diffusion_dt, dt_diff_fe, token);
                    }
                    return token;
                };
                const auto reflux = [] (
                    const arch::scheduler::RklPlan&,
                    const arch::scheduler::RklStageDescriptor&,
                    arch::state::CompletionToken token) { return token; };
                const auto boundary = [&] (
                    StateSlot slot, arch::state::StateVersion version,
                    arch::state::CompletionToken token) {
                    return execute_device_boundary(slot, version, token);
                };
                const auto rotation = [&] (arch::state::SlotRotation value) {
                    for (std::size_t index = 0;
                         index < stage_handles.size(); ++index) {
                        compute_backend->rotate_slots(
                            backend_access(index, StateSlot::Current), value);
                    }
                };
                if (rkl1) {
                    (void)arch::scheduler::execute_single_rkl1_lane(
                        stage_context, stage_handles, stages, executor,
                        reflux, boundary, rotation);
                } else {
                    (void)arch::scheduler::execute_single_rkl2_lane(
                        stage_context, stage_handles, stages, executor,
                        reflux, boundary, rotation);
                }
                trace_backend_operation(
                    arch::backend::BackendOperation::DiffusionStage,
                    StateSlot::Current, before);
                return;
            }

            if (active_blocks.size() > 1) {
                const bool rkl1 = resolved_plan->diffusion_integrator
                    == arch::dispatch::DiffusionIntegratorId::Rkl1;
                const bool rkl2 = resolved_plan->diffusion_integrator
                    == arch::dispatch::DiffusionIntegratorId::Rkl2;
                if (!rkl1 && !rkl2) {
                    throw std::logic_error(
                        "enabled diffusion received an invalid resolved route");
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
                const auto execute_single = [&](auto& integrator) {
                    amr::Block& block = amr_ctrl.pool->GetBlock(active_blocks.front());
                    integrator.integrate(block, eos, block.grid, config,
                                         diffusion_dt, dt_diff_fe, bc_handler);
                };
                Numerics::Diffusion::dispatch_diffusion(
                    config, resolved_plan->diffusion_integrator,
                    execute_single);
            }
        };

        // C1. Burn Step (1/2 dt)
        if (has_burn) {
            (void)arch::scheduler::execute_burn_first_lane(
                stage_context, stage_handles,
                [&](arch::state::CompletionToken token) {
            if (compute_backend) {
                const auto before = compute_backend->counters();
                std::vector<arch::reduction::ReductionCandidate>
                    burn_dt_candidates;
                burn_dt_candidates.reserve(active_blocks.size() + 1);
                burn_dt_candidates.push_back({
                    dt_burn_global,
                    DriverReduction::make_accumulator_reduction_key(
                        DriverReduction::BlockReductionComponent::BurnFirstHalf),
                    true});
                for (std::size_t index = 0;
                     index < stage_handles.size(); ++index) {
                    const auto result = compute_backend->execute_burn(
                        backend_access(index, StateSlot::Current),
                        0.5 * dt, token);
                    if (!arch::state::is_complete(result.completion)
                        || result.completion.value != token.value
                        || result.status != 0 || result.failed_cells != 0) {
                        throw std::runtime_error("CUDA first burn failed");
                    }
                    const amr::Block& block = amr_ctrl.pool->GetBlock(
                        active_blocks[index]);
                    burn_dt_candidates.push_back({
                        result.dt_recommended,
                        DriverReduction::make_block_reduction_key(
                            block.level, block.morton_code,
                            block.logical_x1, block.logical_x2,
                            block.logical_x3,
                            DriverReduction::BlockReductionComponent::BurnFirstHalf),
                        true});
                }
                dt_burn_global = DriverReduction::reduce_block_minimum(
                    DriverBurn::INACTIVE_LIMITER_CANDIDATE,
                    burn_dt_candidates);
                trace_backend_operation(
                    arch::backend::BackendOperation::Burn,
                    StateSlot::Current, before);
                return token;
            }
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
        if (compute_backend) {
            complete_device_boundary(StateSlot::Current);
            const auto before = compute_backend->counters();
            const auto executor = [&] (
                const arch::scheduler::StageDescriptor& descriptor,
                arch::state::CompletionToken token) {
                for (std::size_t index = 0;
                     index < stage_handles.size(); ++index) {
                    (void)compute_backend->execute_hydro_stage(
                        backend_access(index, StateSlot::Current),
                        descriptor, dt, token);
                }
                return token;
            };
            const auto boundary = [&] (
                StateSlot slot, arch::state::StateVersion version,
                arch::state::CompletionToken token) {
                return execute_device_boundary(slot, version, token);
            };
            const auto rotation = [&] (arch::state::SlotRotation value) {
                for (std::size_t index = 0;
                     index < stage_handles.size(); ++index) {
                    compute_backend->rotate_slots(
                        backend_access(index, StateSlot::Current), value);
                }
            };
            const auto reflux = [] (const arch::scheduler::HydroPlan&,
                                    StateSlot,
                                    arch::state::CompletionToken token) {
                return token;
            };
            switch (resolved_plan->time_integrator) {
            case arch::dispatch::TimeIntegratorId::Euler:
                (void)arch::scheduler::execute_euler_lane(
                    stage_context, stage_handles, executor, boundary,
                    rotation, reflux);
                break;
            case arch::dispatch::TimeIntegratorId::Rk2:
                (void)arch::scheduler::execute_rk2_lane(
                    stage_context, stage_handles, executor, boundary,
                    rotation, reflux);
                break;
            case arch::dispatch::TimeIntegratorId::Rk3:
                (void)arch::scheduler::execute_rk3_lane(
                    stage_context, stage_handles, executor, boundary,
                    rotation, reflux);
                break;
            default:
                throw std::logic_error(
                    "CUDA Hydro received an invalid time integrator");
            }
            trace_backend_operation(
                arch::backend::BackendOperation::HydroStage,
                StateSlot::Current, before);
        } else {
            synchronize_fluid_ghosts();
            integrator_solve(
                amr_ctrl, dt, bc_handler, gravity, hydro, num_cfg);
        }

        // C4. Diffusion Step (1/2 dt)
        advance_diffusion(0.5 * dt);

        // C5. Burn Step (1/2 dt)
        if (has_burn) {
            (void)arch::scheduler::execute_burn_second_lane(
                stage_context, stage_handles,
                [&](arch::state::CompletionToken token) {
            if (compute_backend) {
                const auto before = compute_backend->counters();
                std::vector<arch::reduction::ReductionCandidate>
                    burn_dt_candidates;
                burn_dt_candidates.reserve(active_blocks.size() + 1);
                burn_dt_candidates.push_back({
                    dt_burn_global,
                    DriverReduction::make_accumulator_reduction_key(
                        DriverReduction::BlockReductionComponent::BurnSecondHalf),
                    true});
                for (std::size_t index = 0;
                     index < stage_handles.size(); ++index) {
                    const auto result = compute_backend->execute_burn(
                        backend_access(index, StateSlot::Current),
                        0.5 * dt, token);
                    if (!arch::state::is_complete(result.completion)
                        || result.completion.value != token.value
                        || result.status != 0 || result.failed_cells != 0) {
                        throw std::runtime_error("CUDA second burn failed");
                    }
                    const amr::Block& block = amr_ctrl.pool->GetBlock(
                        active_blocks[index]);
                    burn_dt_candidates.push_back({
                        result.dt_recommended,
                        DriverReduction::make_block_reduction_key(
                            block.level, block.morton_code,
                            block.logical_x1, block.logical_x2,
                            block.logical_x3,
                            DriverReduction::BlockReductionComponent::BurnSecondHalf),
                        true});
                }
                dt_burn_global = DriverReduction::reduce_block_minimum(
                    DriverBurn::INACTIVE_LIMITER_CANDIDATE,
                    burn_dt_candidates);
                trace_backend_operation(
                    arch::backend::BackendOperation::Burn,
                    StateSlot::Current, before);
                return token;
            }
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

    if (compute_backend) {
        const std::filesystem::path directory = config.Get<std::string>(
            "log_dir", config.io.out_dir);
        std::filesystem::create_directories(directory);
        const std::filesystem::path trace_path = directory
            / (config.io.base_name + "_backend_trace.tsv");
        std::ofstream trace_output(trace_path, std::ios::trunc);
        if (!trace_output)
            throw std::runtime_error("cannot write CUDA backend trace");
        trace_output
            << "macro_step\toperation\tuid\tepoch\tstorage_generation\tslot"
               "\tinterior_residency\tinterior_version\tinterior_pending"
               "\tinterior_token\tinterior_token_state\tghost_residency"
               "\tghost_version\tghost_source_version\tghost_pending"
               "\tghost_token\tghost_token_state\tbytes_h2d\tbytes_d2h"
               "\tkernel_count\tstream_sync_count\n";
        for (const auto& record : compute_backend->trace_snapshot()) {
            const auto& interior = record.coherence.interior;
            const auto& ghost = record.coherence.ghost;
            trace_output
                << record.macro_step << '\t'
                << static_cast<unsigned int>(record.operation) << '\t'
                << record.block.uid.value << '\t'
                << record.block.epoch.value << '\t'
                << record.storage.value << '\t'
                << static_cast<unsigned int>(record.slot) << '\t'
                << static_cast<unsigned int>(interior.residency) << '\t'
                << interior.version.value << '\t'
                << static_cast<unsigned int>(interior.pending_transfer) << '\t'
                << interior.completion.value << '\t'
                << static_cast<unsigned int>(interior.completion.state) << '\t'
                << static_cast<unsigned int>(ghost.residency) << '\t'
                << ghost.version.value << '\t'
                << record.coherence.ghost_source_version.value << '\t'
                << static_cast<unsigned int>(ghost.pending_transfer) << '\t'
                << ghost.completion.value << '\t'
                << static_cast<unsigned int>(ghost.completion.state) << '\t'
                << record.bytes_h2d << '\t' << record.bytes_d2h << '\t'
                << record.kernel_count << '\t'
                << record.stream_sync_count << '\n';
        }
        if (!trace_output)
            throw std::runtime_error("failed writing CUDA backend trace");
    }

    std::cout << ">>> Simulation Done. Total Steps: " << ctrl.step_count
              << " | Final Time: " << ctrl.t_current << std::endl;
}
