/** @file DriverStages.h
 * @brief Bind split physical stages to the shared runtime and selected EOS.
 * Scratch lists retain capacity; topology and field versions belong to DriverRuntime.
 */
#pragma once
#include "driver/runtime/DriverRuntime.h"
#include "driver/stages/DriverBurn.h"
#include "driver/schedule/DriverControl.h"
#include "driver/io/DriverIO.h"
#include "driver/DriverUtils.h"
#include "driver/dispatch/capability/ResolvedExecutionPlan.h"
#include "driver/dispatch/capability/BackendCapabilities.h"
#include "amr/AMRControl.h"
#include "numerics/burnsolver/BurnerHandle.h"
#include "numerics/diffusion/DiffDispatch.h"
#include "numerics/diffusion/DiffFunction.h"
#include "numerics/integrator/IHydroSolver.h"
#if ARCH_CUDA_BUILD_ENABLED
#include "cuda/common/CudaLaunchConfig.h"
#include "cuda/runtime/CudaBackend.h"
#endif
namespace arch::driver {
using state::StateSlot;
using scheduler::StageExecutionContext;
struct DriverStageWorkspace {
    bool reported_composite_diffusion = false;
    std::uint64_t cuda_diffusion_cache_generation = 0;
    std::vector<CudaDiffusionScheduleRecord> cuda_diffusion_schedule;
    std::vector<reduction::ReductionCandidate> hydro_dt_candidates, diffusion_dt_candidates;
    std::vector<backend::BackendStateAccess> hydro_currents, microphysics_currents;
    std::span<const backend::BackendStateAccess> current_accesses(DriverRuntime& runtime) {
        microphysics_currents.clear();
        microphysics_currents.reserve(runtime.handles().size());
        for (std::size_t i = 0; i < runtime.handles().size(); ++i)
            microphysics_currents.push_back(runtime.backend_access(i, StateSlot::Current));
        return microphysics_currents;
    }
};
struct TimestepCandidates {
    double hydro = 1e99, diffusion_forward_euler = 1e99, diffusion_sts = 1e99;
};
template<class EosPolicy>
TimestepCandidates calculate_timestep_candidates(DriverRuntime& runtime,
    DriverStageWorkspace& workspace, const EosPolicy& eos,
    const dispatch::ResolvedExecutionPlan* resolved_plan)
{
    auto& amr_ctrl = runtime.control();
    const auto& config = runtime.configuration();
    const auto& active_blocks = amr_ctrl.tree->GetActiveBlocks();
    const auto& stage_handles = runtime.handles();
    auto* compute_backend = runtime.backend();
    const bool has_diff = config.physics.diffusion.use_diffusion;
    const double cfl = config.numerics.cfl;
    if (stage_handles.size() != active_blocks.size())
        throw std::logic_error(
            "active topology and scheduler handles disagree");
    workspace.hydro_dt_candidates.clear();
    workspace.hydro_dt_candidates.reserve(active_blocks.size());
    if (compute_backend) {
        auto& currents = workspace.hydro_currents;
        currents.clear();
        currents.reserve(active_blocks.size());
        for (std::size_t index = 0; index < active_blocks.size(); ++index)
            currents.push_back(runtime.backend_access(index, StateSlot::Current));
        const auto block_dt = compute_backend->compute_hydro_dt_batch(currents, cfl);
        if (block_dt.size() != currents.size())
            throw std::logic_error("Hydro batch lost a required block result");
        for (std::size_t index = 0; index < active_blocks.size(); ++index) {
            const amr::Block& b = amr_ctrl.pool->GetBlock(
                active_blocks[index]);
            workspace.hydro_dt_candidates.push_back({
                block_dt[index],
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
            workspace.hydro_dt_candidates.push_back({
                dt_b,
                DriverReduction::make_block_reduction_key(
                    b.level, b.morton_code, b.logical_x1, b.logical_x2,
                    b.logical_x3,
                    DriverReduction::BlockReductionComponent::Hydro),
                true});
        }
    }
    double dt_hydro = DriverReduction::reduce_block_minimum(
        1e99, workspace.hydro_dt_candidates);

    // The diffusion operator reports a forward-Euler stability step.  STS
    // removes that O(dx^2) restriction from the macro step, except when
    // the configured RKL stage cap is genuinely exhausted.
    double dt_diff_fe = 1e99;
    double dt_diff_sts_limit = 1e99;
    if (has_diff) {
        // Face transport depends on the neighbouring rho/T/composition.
        // Reuse backend-local exchange; CUDA does not materialize Host fields.
        runtime.ensure_fluid_ghosts();
        workspace.diffusion_dt_candidates.clear();
        workspace.diffusion_dt_candidates.reserve(active_blocks.size());
        const auto device_diffusion_dt = compute_backend
            ? compute_backend->compute_diffusion_dt_batch(workspace.current_accesses(runtime))
            : std::vector<double>{};
        if (compute_backend && device_diffusion_dt.size() != active_blocks.size())
            throw std::logic_error("Diffusion batch lost a required block result");
        for (std::size_t index = 0; index < active_blocks.size(); ++index) {
            const int block_id = active_blocks[index];
            const amr::Block& block = amr_ctrl.pool->GetBlock(block_id);
            const double block_dt = compute_backend
                ? device_diffusion_dt[index]
                : DiffFlux::adaptive_dt_diff(
                    block.fluid_state, eos, block.grid, config, 1.0);
            workspace.diffusion_dt_candidates.push_back({
                block_dt,
                DriverReduction::make_block_reduction_key(
                    block.level, block.morton_code, block.logical_x1,
                    block.logical_x2, block.logical_x3,
                    DriverReduction::BlockReductionComponent::Diffusion),
                true});
        }
        dt_diff_fe = DriverReduction::reduce_block_minimum(
            1e99, workspace.diffusion_dt_candidates);
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

    return {dt_hydro, dt_diff_fe, dt_diff_sts_limit};
}
template<class EosPolicy>
void advance_diffusion(DriverRuntime& runtime, DriverStageWorkspace& workspace,
    StageExecutionContext& stage_context, const EosPolicy& eos,
    const dispatch::ResolvedExecutionPlan* resolved_plan, std::uint64_t macro_step,
    double diffusion_dt, double dt_diff_fe)
{
    auto& amr_ctrl = runtime.control();
    auto& bc_handler = runtime.boundaries();
    const auto& config = runtime.configuration();
    const auto& active_blocks = amr_ctrl.tree->GetActiveBlocks();
    const auto& stage_handles = runtime.handles();
    auto* compute_backend = runtime.backend();
    const bool has_diff = config.physics.diffusion.use_diffusion;
    if (!has_diff || diffusion_dt <= 0.0) return;

    if (compute_backend) {
        runtime.ensure_fluid_ghosts(StateSlot::Current);
        const auto copy_one = [&](StateSlot destination) {
            const auto before = compute_backend->counters();
            (void)arch::scheduler::copy_slot(
                stage_context, stage_handles, StateSlot::Current,
                destination, [&] {
                    compute_backend->copy_state_slot_batch(
                        workspace.current_accesses(runtime), destination);
                });
            runtime.trace_backend_operation(
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
        int negative_gamma_stages = 0;
        for (int stage = 1; stage <= stages; ++stage) {
            const auto coefficients = DiffFunction::get_rkl_coeffs(
                order, stage, stages);
            if (!std::isfinite(coefficients.gamma))
                throw std::runtime_error(
                    "CUDA RKL schedule produced non-finite gamma");
            if (coefficients.gamma < 0.0)
                ++negative_gamma_stages;
        }
        workspace.cuda_diffusion_schedule.push_back({
            macro_step,
            ++workspace.cuda_diffusion_cache_generation,
            rkl1 ? 1 : 2, stages, negative_gamma_stages, rkl2,
            diffusion_dt, dt_diff_fe});
        const auto before = compute_backend->counters();
        const auto executor = [&] (
            const arch::scheduler::RklPlan& plan,
            const arch::scheduler::RklStageDescriptor& descriptor,
            arch::state::CompletionToken token) {
            (void)compute_backend->clear_amr_flux_register(token);
            return compute_backend->execute_diffusion_stage_batch(
                workspace.current_accesses(runtime), plan,
                descriptor, diffusion_dt, dt_diff_fe, token);
        };
        const auto reflux = [&] (
            const arch::scheduler::RklPlan&,
            const arch::scheduler::RklStageDescriptor& descriptor,
            arch::state::CompletionToken token) {
            return compute_backend->execute_amr_reflux(
                descriptor.output_slot, diffusion_dt, token);
        };
        const auto boundary = [&] (
            StateSlot slot, arch::state::StateVersion version,
            arch::state::CompletionToken token) {
            return runtime.execute_device_boundary(slot, version, token);
        };
        const auto rotation = [&] (arch::state::SlotRotation value) {
            for (std::size_t index = 0;
                 index < stage_handles.size(); ++index) {
                compute_backend->rotate_slots(
                    runtime.backend_access(index, StateSlot::Current), value);
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
        runtime.trace_backend_operation(
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
        if (!workspace.reported_composite_diffusion) {
            std::cout << "[Diffusion] multi-block AMR uses composite "
                      << (rkl1 ? "RKL1" : "RKL2")
                      << " STS with stage ghost synchronization and reflux."
                      << std::endl;
            workspace.reported_composite_diffusion = true;
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
}
enum class BurnHalf { First, Second };
template<class EosPolicy>
state::CompletionToken execute_burn_half(DriverRuntime& runtime,
    DriverStageWorkspace& workspace, const EosPolicy& eos,
    const BurnerHandle<EosPolicy>& burn, BurnHalf half, double burn_dt,
    double& dt_burn_global, state::CompletionToken token)
{
    auto& amr_ctrl = runtime.control();
    auto& bc_handler = runtime.boundaries();
    const auto& config = runtime.configuration();
    const auto& active_blocks = amr_ctrl.tree->GetActiveBlocks();
    const auto& stage_handles = runtime.handles();
    auto* compute_backend = runtime.backend();
    const auto component = half == BurnHalf::First
        ? DriverReduction::BlockReductionComponent::BurnFirstHalf
        : DriverReduction::BlockReductionComponent::BurnSecondHalf;
    if (compute_backend) {
        const auto before = compute_backend->counters();
        std::vector<arch::reduction::ReductionCandidate>
            burn_dt_candidates;
        burn_dt_candidates.reserve(active_blocks.size() + 1);
        burn_dt_candidates.push_back({
            dt_burn_global,
            DriverReduction::make_accumulator_reduction_key(
                component),
            true});
        const auto burn_results = compute_backend->execute_burn_batch(
            workspace.current_accesses(runtime), burn_dt, token);
        if (burn_results.size() != stage_handles.size())
            throw std::logic_error("Burn batch lost a required block result");
        for (std::size_t index = 0;
             index < stage_handles.size(); ++index) {
            const auto& result = burn_results[index];
            if (!arch::state::is_complete(result.completion)
                || result.completion.value != token.value
                || result.status != 0 || result.failed_cells != 0) {
                throw std::runtime_error(half == BurnHalf::First ? "CUDA first burn failed" : "CUDA second burn failed");
            }
            const amr::Block& block = amr_ctrl.pool->GetBlock(
                active_blocks[index]);
            burn_dt_candidates.push_back({
                result.dt_recommended,
                DriverReduction::make_block_reduction_key(
                    block.level, block.morton_code,
                    block.logical_x1, block.logical_x2,
                    block.logical_x3,
                    component),
                true});
        }
        dt_burn_global = DriverReduction::reduce_block_minimum(
            DriverBurn::INACTIVE_LIMITER_CANDIDATE,
            burn_dt_candidates);
        runtime.trace_backend_operation(
            arch::backend::BackendOperation::Burn,
            StateSlot::Current, before);
        return token;
    }
    std::vector<double> dt_burn_by_block(active_blocks.size(), 1e99);
    #pragma omp parallel for schedule(dynamic, 1)
    for (size_t i = 0; i < active_blocks.size(); ++i) {
        amr::Block& b = amr_ctrl.pool->GetBlock(active_blocks[i]);
        bc_handler.apply(b.fluid_state, b.grid);
        execute_burn_step(b.fluid_state, burn_dt, eos, burn, b.grid, config, dt_burn_by_block[i]);
    }
    std::vector<arch::reduction::ReductionCandidate>
        burn_dt_candidates;
    burn_dt_candidates.reserve(active_blocks.size() + 1);
    burn_dt_candidates.push_back({
        dt_burn_global,
        DriverReduction::make_accumulator_reduction_key(
            component),
        true});
    for (size_t i = 0; i < dt_burn_by_block.size(); ++i) {
        const amr::Block& block = amr_ctrl.pool->GetBlock(
            active_blocks[i]);
        burn_dt_candidates.push_back({
            dt_burn_by_block[i],
            DriverReduction::make_block_reduction_key(
                block.level, block.morton_code, block.logical_x1,
                block.logical_x2, block.logical_x3,
                component),
            true});
    }
    dt_burn_global = DriverReduction::reduce_block_minimum(
        1e99, burn_dt_candidates);
    return token;
}
using IntegratorSolve = void (*)(amr::AMRControl&, double, BCHandler&,
    const Physical::Gravity::IGravityPolicy*, const Numerics::IHydroSolver*, const NumericsConfig&);
inline void advance_hydro(DriverRuntime& runtime, DriverStageWorkspace& workspace,
    StageExecutionContext& stage_context, const dispatch::ResolvedExecutionPlan* resolved_plan,
    double dt, IntegratorSolve integrator_solve,
    const Physical::Gravity::IGravityPolicy* gravity, const Numerics::IHydroSolver* hydro)
{
    auto& amr_ctrl = runtime.control();
    auto& bc_handler = runtime.boundaries();
    const auto& config = runtime.configuration();
    const auto& active_blocks = amr_ctrl.tree->GetActiveBlocks();
    const auto& stage_handles = runtime.handles();
    auto* compute_backend = runtime.backend();
    const auto& num_cfg = config.numerics;
    if (compute_backend) {
        runtime.ensure_fluid_ghosts(StateSlot::Current);
        auto& currents = workspace.hydro_currents;
        currents.clear();
        currents.reserve(stage_handles.size());
        for (std::size_t index = 0; index < stage_handles.size(); ++index)
            currents.push_back(runtime.backend_access(index, StateSlot::Current));
        const auto before = compute_backend->counters();
        const auto executor = [&] (
            const arch::scheduler::StageDescriptor& descriptor,
            arch::state::CompletionToken token) {
            if (descriptor.stage == 1)
                (void)compute_backend->clear_amr_flux_register(token);
            return compute_backend->execute_hydro_stage_batch(
                currents, descriptor, dt, token);
        };
        const auto boundary = [&] (
            StateSlot slot, arch::state::StateVersion version,
            arch::state::CompletionToken token) {
            return runtime.execute_device_boundary(slot, version, token);
        };
        const auto rotation = [&] (arch::state::SlotRotation value) {
            for (std::size_t index = 0;
                 index < stage_handles.size(); ++index) {
                compute_backend->rotate_slots(
                    runtime.backend_access(index, StateSlot::Current), value);
            }
        };
        const auto reflux = [&] (const arch::scheduler::HydroPlan&,
                                StateSlot slot,
                                arch::state::CompletionToken token) {
            return compute_backend->execute_amr_reflux(
                slot, dt, token);
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
        runtime.trace_backend_operation(
            arch::backend::BackendOperation::HydroStage,
            StateSlot::Current, before);
    } else {
        runtime.ensure_fluid_ghosts();
        integrator_solve(
            amr_ctrl, dt, bc_handler, gravity, hydro, num_cfg);
    }

}
template<class EosPolicy>
void start_compute_backend(DriverRuntime& runtime, const EosPolicy& eos,
    const dispatch::ResolvedExecutionPlan& plan,
    const dispatch::BackendResolution& resolution, dispatch::StartupOrder& order)
{
    if (resolution.resolved_backend == dispatch::ComputeBackend::Cuda) {
#if ARCH_CUDA_BUILD_ENABLED
        const auto topology = runtime.prepare_backend_bindings();
        std::vector<cuda::CudaBlockBinding> bindings;
        bindings.reserve(topology.size());
        for (const auto& block : topology)
            bindings.push_back({block.block, block.handle, block.storage, block.physical_boundary});
        runtime.install_backend(cuda::make_cuda_backend(bindings, resolution.device.ordinal,
            cuda::make_cuda_launch_config(plan, runtime.configuration()), runtime.species(), eos));
        order.record(dispatch::StartupEvent::Constructed);
        order.record(dispatch::StartupEvent::Allocated);
        runtime.upload_initial_state();
#else
        throw std::logic_error("CUDA backend is unavailable");
#endif
    } else {
        order.record(dispatch::StartupEvent::Constructed);
        order.record(dispatch::StartupEvent::Allocated);
    }
    order.record(dispatch::StartupEvent::Running);
}
} // namespace arch::driver
