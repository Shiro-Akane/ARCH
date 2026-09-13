/**
 * @file CudaBackendMicrophysicsControl.cpp
 * @brief Bind shared burn and RKL schedules to CUDA runtime execution.
 *
 * Resolve the active slots/EOS, validate stage descriptors and dispatch typed
 * launches on the backend stream. This layer owns completion/status checks and
 * summary downloads; shared burn/diffusion policies own numerical updates.
 */

#include "cuda/runtime/control/CudaBackendInternal.h"
#include "cuda/runtime/burn/CudaBackendBurn.h"
#include "driver/DriverBurnPolicy.h"

namespace arch::cuda {
namespace {

constexpr bool same_block_handle(
    amr::BlockHandle left, amr::BlockHandle right) noexcept
{
    return left.uid.value == right.uid.value
        && left.epoch.value == right.epoch.value;
}

constexpr bool same_storage_generation(
    backend::StorageGeneration left,
    backend::StorageGeneration right) noexcept
{
    return left.value == right.value;
}

bool same_rkl_descriptor(
    const scheduler::RklStageDescriptor& left,
    const scheduler::RklStageDescriptor& right) noexcept
{
    return left.stage == right.stage
        && left.state_n_slot == right.state_n_slot
        && left.previous_slot == right.previous_slot
        && left.older_slot == right.older_slot
        && left.output_slot == right.output_slot
        && left.reflux_before_publish == right.reflux_before_publish
        && left.refresh_ghost_after == right.refresh_ghost_after;
}

} // namespace

double CudaBackend::compute_diffusion_dt(
    backend::BackendStateAccess current)
{
    return compute_diffusion_dt_batch({&current, 1}).front();
}

std::vector<double> CudaBackend::compute_diffusion_dt_batch(
    std::span<const backend::BackendStateAccess> currents)
{
    validate_hydro_batch_accesses(currents);
    std::vector<double> dt(currents.size());
    if (currents.empty()) return dt;
    impl_->select_device();
    auto& scratch = impl_->diffusion_batch;
    scratch.ensure_capacity(currents.size());
    std::uint64_t kernels = 0;
    CudaQuiescenceGuard work_guard{*impl_};
    for (std::size_t index = 0; index < currents.size(); ++index) {
        auto& block = impl_->require_block(currents[index]);
        const CudaBackendDiffusionWorkspace workspace{
            block.face_flux.view(), block.diffusion_dt_candidates.get(),
            scratch.dt->get() + index, scratch.status->get() + index,
            impl_->species_workspace};
        CudaBackendLaunchResult result{};
        visit_eos(impl_->eos, [&](const auto& eos) {
            result = launch_cuda_backend_diffusion_dt(
                block.require_access(currents[index]), eos, impl_->species_view, block.grid,
                impl_->launch.diffusion, workspace, impl_->stream.get());
        });
        check_cuda(result.error, "launch diffusion dt batch");
        kernels += result.kernels_launched;
    }
    check_cuda(cudaMemcpyAsync(
                   scratch.host_status.data(), scratch.status->get(), currents.size() * sizeof(int),
                   cudaMemcpyDeviceToHost, impl_->stream.get()),
               "download diffusion batch status");
    check_cuda(cudaMemcpyAsync(
                   dt.data(), scratch.dt->get(), dt.size() * sizeof(double),
                   cudaMemcpyDeviceToHost, impl_->stream.get()),
               "download diffusion batch dt");
    quiesce();
    work_guard.completed = true;
    impl_->runtime_counters.kernel_count += kernels;
    impl_->runtime_counters.bytes_d2h += currents.size() * (sizeof(int) + sizeof(double));
    for (std::size_t index = 0; index < currents.size(); ++index)
        if (scratch.host_status[index] != 0)
            throw std::runtime_error("diffusion dt candidate failed: block="
                + std::to_string(currents[index].block.uid.value));
    return dt;
}

void CudaBackend::copy_state_slot(
    backend::BackendStateAccess source,
    backend::BackendStateAccess destination)
{
    if (!same_block_handle(source.block, destination.block)
        || !same_storage_generation(source.storage, destination.storage))
        throw std::invalid_argument("state copy crosses CUDA blocks");
    auto& block = impl_->require_block(source);
    const DeviceStateView from = block.require_access(source);
    const DeviceStateView to = block.require_access(destination);
    if (source.slot == destination.slot)
        throw std::invalid_argument("state copy aliases one logical slot");
    CudaQuiescenceGuard work_guard{*impl_};
    check_cuda(copy_cuda_backend_state_slot(from, to, impl_->stream.get()),
               "copy state slot");
    quiesce();
    work_guard.completed = true;
}

void CudaBackend::copy_state_slot_batch(
    std::span<const backend::BackendStateAccess> sources, state::StateSlot destination)
{
    validate_hydro_batch_accesses(sources);
    if (destination != state::StateSlot::Next && destination != state::StateSlot::Scratch)
        throw std::invalid_argument("Batch Current copy needs a distinct destination");
    // Bind and validate ALL views before the first write, including late entries.
    std::vector<std::pair<DeviceStateView, DeviceStateView>> bindings;
    bindings.reserve(sources.size());
    for (const auto source : sources) {
        auto& block = impl_->require_block(source);
        bindings.emplace_back(block.require_access(source),
            block.require_access({source.block, source.storage, destination}));
    }
    if (bindings.empty()) return;
    CudaQuiescenceGuard work_guard{*impl_};
    for (const auto& [from, to] : bindings)
        check_cuda(copy_cuda_backend_state_slot(from, to, impl_->stream.get()),
                   "copy state slot batch");
    quiesce();
    work_guard.completed = true;
}

state::CompletionToken CudaBackend::execute_diffusion_stage(
    backend::BackendStateAccess current, const scheduler::RklPlan& plan,
    const scheduler::RklStageDescriptor& descriptor,
    double dt, double dt_fe, state::CompletionToken expected)
{
    return execute_diffusion_stage_batch({&current, 1}, plan, descriptor, dt, dt_fe, expected);
}

state::CompletionToken CudaBackend::execute_diffusion_stage_batch(
    std::span<const backend::BackendStateAccess> currents, const scheduler::RklPlan& plan,
    const scheduler::RklStageDescriptor& descriptor,
    double dt, double dt_fe, state::CompletionToken expected)
{
    validate_hydro_batch_accesses(currents);
    const bool frozen_rkl1 = impl_->launch.plan.diffusion_integrator
        == dispatch::DiffusionIntegratorId::Rkl1;
    const bool frozen_rkl2 = impl_->launch.plan.diffusion_integrator
        == dispatch::DiffusionIntegratorId::Rkl2;
    if (!complete_token(expected)
        || !impl_->launch.diffusion.use_diffusion
        || (!frozen_rkl1 && !frozen_rkl2)
        || plan.second_order != frozen_rkl2
        || descriptor.stage <= 0
        || descriptor.stage > static_cast<int>(plan.stages.size())
        || !same_rkl_descriptor(
            descriptor, plan.stages[descriptor.stage - 1])
        || !(dt > 0.0) || !(dt_fe > 0.0))
        throw std::invalid_argument("invalid diffusion stage contract");
    if (currents.empty()) return expected;
    impl_->select_device();
    auto& scratch = impl_->diffusion_batch;
    scratch.ensure_capacity(currents.size());
    std::uint64_t kernels = 0;
    CudaQuiescenceGuard work_guard{*impl_};
    for (std::size_t index = 0; index < currents.size(); ++index) {
        const auto current = currents[index];
        auto& block = impl_->require_block(current);
        const DeviceStateView state_n =
            block.slots[slot_index(descriptor.state_n_slot)];
        const DeviceStateView previous =
            block.slots[slot_index(descriptor.previous_slot)];
        const DeviceStateView older =
            block.slots[slot_index(descriptor.older_slot)];
        const DeviceStateView output =
            block.slots[slot_index(descriptor.output_slot)];
        const CudaBackendDiffusionWorkspace workspace{
            block.face_flux.view(), block.diffusion_dt_candidates.get(),
            block.diffusion_dt_result.get(), scratch.status->get() + index,
            impl_->species_workspace};
        const auto amr_routes = make_cuda_amr_route_views(
            impl_->active_amr_flux.get(), current.block);
        CudaBackendLaunchResult operation{};
        visit_eos(impl_->eos, [&](const auto& eos) {
            operation = launch_cuda_backend_diffusion_stage(
                plan, descriptor, state_n, previous, older, output,
                block.diffusion_delta.view(),
                block.diffusion_initial_delta.view(), eos, impl_->species_view,
                block.grid, impl_->launch.diffusion, workspace,
                amr_routes.data(), dt,
                impl_->stream.get());
        });
        check_cuda(operation.error, "launch diffusion operator");
        kernels += operation.kernels_launched;
    }
    check_cuda(cudaMemcpyAsync(
                   scratch.host_status.data(), scratch.status->get(), currents.size() * sizeof(int),
                   cudaMemcpyDeviceToHost, impl_->stream.get()),
               "download diffusion stage status");
    quiesce();
    work_guard.completed = true;
    impl_->runtime_counters.kernel_count +=
        kernels;
    impl_->runtime_counters.bytes_d2h += currents.size() * sizeof(int);
    for (std::size_t index = 0; index < currents.size(); ++index)
        if (scratch.host_status[index] != 0)
            throw std::runtime_error("diffusion stage failed: block="
                + std::to_string(currents[index].block.uid.value));
    return expected;
}

backend::BurnExecutionResult CudaBackend::execute_burn(
    backend::BackendStateAccess current, double dt,
    state::CompletionToken expected)
{
    return execute_burn_batch({&current, 1}, dt, expected).front();
}

std::vector<backend::BurnExecutionResult> CudaBackend::execute_burn_batch(
    std::span<const backend::BackendStateAccess> currents, double dt,
    state::CompletionToken expected)
{
    validate_hydro_batch_accesses(currents);
    if (!complete_token(expected)
        || !(dt > 0.0))
        throw std::invalid_argument("invalid burn contract");
    std::vector<backend::BurnExecutionResult> results(currents.size());
    std::vector<DeviceBurnSummary> summaries(currents.size());
    if (currents.empty()) return results;
    impl_->select_device();
    auto& scratch = impl_->burn_batch_summaries;
    scratch.reserve(currents.size());
    std::uint64_t batch_kernels = 0;
    CudaQuiescenceGuard summary_guard{*impl_};
    for (std::size_t index = 0; index < currents.size(); ++index) {
        auto& block = impl_->require_block(currents[index]);
        if (!impl_->launch.burn.use_burn) {
            check_cuda(cudaMemsetAsync(
                           block.slots[slot_index(state::StateSlot::Current)].enuc_rate,
                           0,
                           static_cast<std::size_t>(block.grid.total_size)
                               * sizeof(double),
                           impl_->stream.get()),
                       "clear disabled burn diagnostic");
            results[index] = {DriverBurn::INACTIVE_LIMITER_CANDIDATE, 0, 0, expected};
            continue;
        }
        DeviceStateView selected =
            block.slots[slot_index(state::StateSlot::Current)];
        check_cuda(cudaMemsetAsync(
                       selected.enuc_rate, 0,
                       static_cast<std::size_t>(block.grid.total_size)
                           * sizeof(double),
                       impl_->stream.get()),
                   "clear burn diagnostic");
        std::uint64_t burn_kernels = 0;
        if (impl_->launch.plan.linear_solver == dispatch::LinearSolverId::CuDss) {
#if ARCH_HAS_CUDSS_PROVIDER
            if (!impl_->sparse_burn_owner) {
                visit_eos(impl_->eos, [&](const auto& eos) {
                    impl_->sparse_burn_owner = make_cuda_sparse_burn_owner(
                        impl_->launch.plan, eos, block.grid.active_cell_count(), impl_->stream.get());
                });
                if (!impl_->sparse_burn_owner)
                    throw std::runtime_error("CUDA sparse burn factory returned no owner");
                const auto initial = impl_->sparse_burn_owner->construction_counters();
                impl_->runtime_counters.kernel_count += initial.kernels;
                impl_->runtime_counters.bytes_d2h += initial.bytes_d2h;
                impl_->runtime_counters.bytes_h2d += initial.bytes_h2d;
                impl_->runtime_counters.stream_sync_count += initial.synchronizations;
                impl_->immutable_owner_constructions += initial.immutable_owner_constructions;
            }
            const auto counters = impl_->sparse_burn_owner->execute(
                selected, block.grid, dt, impl_->launch.burn,
                block.burn_candidates.get(), block.burn_statuses.get(), scratch.get() + index);
            burn_kernels = counters.kernels;
            impl_->runtime_counters.bytes_d2h += counters.bytes_d2h;
            impl_->runtime_counters.bytes_h2d += counters.bytes_h2d;
            impl_->runtime_counters.stream_sync_count += counters.synchronizations;
#else
            throw std::runtime_error("CUDA sparse burn production route is unavailable in this build");
#endif
        } else {
            const bool had_network_owner = static_cast<bool>(impl_->dense_network_owner);
            cudaError_t launch_error = cudaErrorInvalidValue;
            visit_eos(impl_->eos, [&](const auto& eos) {
                launch_error = launch_cuda_burn_route(
                    impl_->launch.plan, selected, block.grid,
                    block.burn_workspace_storage.get(), block.burn_candidates.get(),
                    block.burn_statuses.get(), scratch.get() + index, dt, eos,
                    CudaBurnArguments{impl_->launch.burn, &impl_->dense_network_owner}, impl_->stream.get());
            });
            if (!had_network_owner && impl_->dense_network_owner) {
                impl_->runtime_counters.bytes_h2d += impl_->dense_network_owner->bytes();
                impl_->runtime_counters.stream_sync_count += impl_->dense_network_owner->synchronization_count();
                ++impl_->immutable_owner_constructions;
            }
            check_cuda(launch_error, "launch burn route");
            burn_kernels = 2;
        }
        batch_kernels += burn_kernels;
    }
    if (impl_->launch.burn.use_burn) {
        check_cuda(cudaMemcpyAsync(
                       summaries.data(), scratch.get(), summaries.size() * sizeof(DeviceBurnSummary),
                       cudaMemcpyDeviceToHost, impl_->stream.get()),
                   "download burn summary");
        impl_->runtime_counters.bytes_d2h += summaries.size() * sizeof(DeviceBurnSummary);
    }
    quiesce();
    summary_guard.completed = true;
    impl_->runtime_counters.kernel_count += batch_kernels;
    if (impl_->launch.burn.use_burn)
        for (std::size_t index = 0; index < summaries.size(); ++index) {
            const auto& summary = summaries[index];
            results[index] = {summary.limiter, summary.failed_cells, summary.status, expected};
        }
    return results;
}


} // namespace arch::cuda
